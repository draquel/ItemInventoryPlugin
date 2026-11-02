#include "Storage/ItemStorageSubsystem.h"
#include "ItemInventoryPlugin.h"
#include "Storage/ItemSystemSettings.h"
#include "Storage/LocalItemStorage.h"
#include "Storage/RemoteItemStorage.h"
#include "Components/InventoryComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

// ---------------------------------------------------------------------------
// USubsystem
// ---------------------------------------------------------------------------

void UItemStorageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();

	// Create storage backend based on settings
	switch (Settings->StorageBackend)
	{
	case EItemStorageBackend::Remote:
		{
			URemoteItemStorage* Remote = NewObject<URemoteItemStorage>(this);
			StorageBackend.SetObject(Remote);
			StorageBackend.SetInterface(Cast<ICGFItemStorageInterface>(Remote));
			UE_LOG(LogIIP, Log, TEXT("ItemStorageSubsystem: Using Remote storage backend"));
		}
		break;

	case EItemStorageBackend::Local:
	default:
		{
			ULocalItemStorage* Local = NewObject<ULocalItemStorage>(this);
			StorageBackend.SetObject(Local);
			StorageBackend.SetInterface(Cast<ICGFItemStorageInterface>(Local));
			UE_LOG(LogIIP, Log, TEXT("ItemStorageSubsystem: Using Local storage backend"));
		}
		break;
	}

	// Start auto-save timer
	if (Settings->bAutoSaveEnabled && Settings->AutoSaveIntervalSeconds > 0.0f)
	{
		if (UWorld* World = GetGameInstance()->GetWorld())
		{
			World->GetTimerManager().SetTimer(AutoSaveTimerHandle, FTimerDelegate::CreateUObject(this, &UItemStorageSubsystem::OnAutoSaveTick),
				Settings->AutoSaveIntervalSeconds, true);
			UE_LOG(LogIIP, Log, TEXT("ItemStorageSubsystem: Auto-save enabled (interval: %.1fs)"),
				Settings->AutoSaveIntervalSeconds);
		}
	}
}

void UItemStorageSubsystem::Deinitialize()
{
	// Start saving every dirty inventory before shutdown. Nothing waits: a remote backend's requests keep their queue
	// alive and are still sent (G93).
	SaveAll();

	// Clear timer
	if (AutoSaveTimerHandle.IsValid())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UWorld* World = GI->GetWorld())
			{
				World->GetTimerManager().ClearTimer(AutoSaveTimerHandle);
			}
		}
	}

	RegisteredInventories.Empty();
	PendingLoads.Empty();
	StorageBackend = nullptr;

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void UItemStorageSubsystem::RegisterInventory(UInventoryComponent* Component, const FString& OwnerId)
{
	if (!Component || OwnerId.IsEmpty())
	{
		return;
	}

	// Avoid duplicate registration
	for (const FRegisteredInventory& Reg : RegisteredInventories)
	{
		if (Reg.Component.Get() == Component)
		{
			return;
		}
	}

	FRegisteredInventory Entry;
	Entry.Component = Component;
	Entry.OwnerId = OwnerId;
	RegisteredInventories.Add(MoveTemp(Entry));

	UE_LOG(LogIIP, Verbose, TEXT("ItemStorageSubsystem: Registered inventory '%s'"), *OwnerId);
}

void UItemStorageSubsystem::UnregisterInventory(UInventoryComponent* Component)
{
	if (!Component)
	{
		return;
	}

	for (int32 i = RegisteredInventories.Num() - 1; i >= 0; --i)
	{
		if (RegisteredInventories[i].Component.Get() == Component)
		{
			const FString OwnerId = RegisteredInventories[i].OwnerId;
			RegisteredInventories.RemoveAt(i);
			UE_LOG(LogIIP, Verbose, TEXT("ItemStorageSubsystem: Unregistered inventory '%s'"), *OwnerId);

			// Save if dirty (fire-and-forget: a failure can no longer be retried, the backend logs it). After the removal:
			// a backend that answers at once reports, and OnInventorySaved's listeners run, inside this call.
			if (Component->IsDirty() && StorageBackend.GetObject())
			{
				StartSave(Component, OwnerId);
			}
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Manual Operations
// ---------------------------------------------------------------------------

bool UItemStorageSubsystem::SaveInventory(UInventoryComponent* Component)
{
	if (!Component || !StorageBackend.GetObject())
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: SaveInventory refused: %s"),
			Component ? TEXT("no storage backend") : TEXT("no component"));
		return false;
	}

	// Find the owner ID for this component
	for (const FRegisteredInventory& Reg : RegisteredInventories)
	{
		if (Reg.Component.Get() == Component)
		{
			StartSave(Component, Reg.OwnerId);
			return true;
		}
	}

	UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: SaveInventory called for unregistered component"));
	return false;
}

bool UItemStorageSubsystem::LoadInventory(UInventoryComponent* Component, const FString& OwnerId)
{
	if (!Component || !StorageBackend.GetObject() || OwnerId.IsEmpty())
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: LoadInventory('%s') refused: no component, backend or owner id"), *OwnerId);
		return false;
	}

	// Loading puts whole items into the inventory: the server's storage only, never a client's (G91).
	const AActor* Owner = Component->GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: LoadInventory('%s') on a client refused: inventories load on the server."), *OwnerId);
		return false;
	}

	// Recorded before the call: a backend that answers at once reports before the call returns.
	PendingLoads.Add({ Component, OwnerId });
	FOnStorageLoadComplete Callback;
	Callback.BindDynamic(this, &UItemStorageSubsystem::HandleLoadComplete);
	ICGFItemStorageInterface::Execute_LoadInventoryAsync(StorageBackend.GetObject(), OwnerId, Callback);
	return true;
}

int32 UItemStorageSubsystem::SaveAll()
{
	if (!StorageBackend.GetObject())
	{
		return 0;
	}

	// Collected first: a backend that answers at once reports inside StartSave, and a listener may unregister.
	TArray<FRegisteredInventory> ToSave;
	for (const FRegisteredInventory& Reg : RegisteredInventories)
	{
		UInventoryComponent* Comp = Reg.Component.Get();
		if (Comp && Comp->IsDirty())
		{
			ToSave.Add(Reg);
		}
	}

	int32 SaveCount = 0;
	for (const FRegisteredInventory& Reg : ToSave)
	{
		if (UInventoryComponent* Comp = Reg.Component.Get())
		{
			StartSave(Comp, Reg.OwnerId);
			++SaveCount;
		}
	}

	if (SaveCount > 0)
	{
		UE_LOG(LogIIP, Verbose, TEXT("ItemStorageSubsystem: SaveAll started %d saves"), SaveCount);
	}
	return SaveCount;
}

bool UItemStorageSubsystem::SetStorageBackend(UObject* Backend)
{
	if (!Backend || !Backend->GetClass()->ImplementsInterface(UCGFItemStorageInterface::StaticClass()))
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: SetStorageBackend refused: '%s' does not implement ICGFItemStorageInterface"),
			*GetNameSafe(Backend));
		return false;
	}

	StorageBackend.SetObject(Backend);
	StorageBackend.SetInterface(Cast<ICGFItemStorageInterface>(Backend));
	return true;
}

// ---------------------------------------------------------------------------
// Auto-Save
// ---------------------------------------------------------------------------

void UItemStorageSubsystem::OnAutoSaveTick()
{
	if (!StorageBackend.GetObject())
	{
		return;
	}

	// Clean up stale entries, then start saving the dirty ones; the backend never makes this wait (G93).
	RegisteredInventories.RemoveAll([](const FRegisteredInventory& Reg) { return !Reg.Component.IsValid(); });
	SaveAll();
}

// ---------------------------------------------------------------------------
// Asynchronous saves and loads (G93)
// ---------------------------------------------------------------------------

void UItemStorageSubsystem::StartSave(UInventoryComponent* Component, const FString& OwnerId)
{
	// Cleared before the call: a change made while the save is in flight marks the inventory dirty again and is saved
	// by the next pass; a backend that answers at once may report (and re-dirty on failure) before the call returns.
	Component->ClearDirty();

	FOnStorageComplete Callback;
	Callback.BindDynamic(this, &UItemStorageSubsystem::HandleSaveComplete);
	ICGFItemStorageInterface::Execute_SaveInventoryAsync(StorageBackend.GetObject(), OwnerId,
		CollectItemsFromComponent(Component), Callback);
}

void UItemStorageSubsystem::HandleSaveComplete(const FString& OwnerId, bool bSuccess)
{
	if (!bSuccess)
	{
		// The backend has logged why; the next auto-save tries again.
		for (const FRegisteredInventory& Reg : RegisteredInventories)
		{
			if (Reg.OwnerId == OwnerId)
			{
				if (UInventoryComponent* Comp = Reg.Component.Get())
				{
					Comp->MarkDirty();
				}
			}
		}
	}

	OnInventorySaved.Broadcast(OwnerId, bSuccess);
}

void UItemStorageSubsystem::HandleLoadComplete(const FString& OwnerId, bool bSuccess, const TArray<FItemInstance>& Items)
{
	const int32 Index = PendingLoads.IndexOfByPredicate([&OwnerId](const FRegisteredInventory& Pending)
	{
		return Pending.OwnerId == OwnerId;
	});
	UInventoryComponent* Component = nullptr;
	if (Index != INDEX_NONE)
	{
		Component = PendingLoads[Index].Component.Get();
		PendingLoads.RemoveAt(Index);
	}

	if (!Component)
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: load of '%s' finished after its component went away"), *OwnerId);
		OnInventoryLoaded.Broadcast(OwnerId, false);
		return;
	}

	if (!bSuccess)
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemStorageSubsystem: load of '%s' failed; the inventory is left as it was"), *OwnerId);
		OnInventoryLoaded.Broadcast(OwnerId, false);
		return;
	}

	ApplyItemsToComponent(Component, Items);
	Component->ClearDirty();
	UE_LOG(LogIIP, Verbose, TEXT("ItemStorageSubsystem: Loaded %d items for '%s'"), Items.Num(), *OwnerId);
	OnInventoryLoaded.Broadcast(OwnerId, true);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

TArray<FItemInstance> UItemStorageSubsystem::CollectItemsFromComponent(UInventoryComponent* Component)
{
	TArray<FItemInstance> Items;
	if (!Component)
	{
		return Items;
	}

	for (const FInventorySlot& Slot : Component->InventorySlots.Items)
	{
		if (Slot.bIsOccupied && Slot.Item.IsValid())
		{
			Items.Add(Slot.Item);
		}
	}

	return Items;
}

void UItemStorageSubsystem::ApplyItemsToComponent(UInventoryComponent* Component, const TArray<FItemInstance>& Items)
{
	if (!Component)
	{
		return;
	}

	// Clear all existing slots
	for (int32 i = 0; i < Component->InventorySlots.Items.Num(); ++i)
	{
		FInventorySlot& Slot = Component->InventorySlots.Items[i];
		if (Slot.bIsOccupied)
		{
			Slot.Item = FItemInstance();
			Slot.bIsOccupied = false;
			Component->InventorySlots.MarkItemDirty(Slot);
		}
	}

	// Add loaded items sequentially
	for (const FItemInstance& Item : Items)
	{
		if (Item.IsValid())
		{
			Component->TryAddItem(Item);
		}
	}
}
