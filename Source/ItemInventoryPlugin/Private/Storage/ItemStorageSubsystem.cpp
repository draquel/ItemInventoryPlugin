#include "Storage/ItemStorageSubsystem.h"
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
			UE_LOG(LogTemp, Log, TEXT("ItemStorageSubsystem: Using Remote storage backend"));
		}
		break;

	case EItemStorageBackend::Local:
	default:
		{
			ULocalItemStorage* Local = NewObject<ULocalItemStorage>(this);
			StorageBackend.SetObject(Local);
			StorageBackend.SetInterface(Cast<ICGFItemStorageInterface>(Local));
			UE_LOG(LogTemp, Log, TEXT("ItemStorageSubsystem: Using Local storage backend"));
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
			UE_LOG(LogTemp, Log, TEXT("ItemStorageSubsystem: Auto-save enabled (interval: %.1fs)"),
				Settings->AutoSaveIntervalSeconds);
		}
	}
}

void UItemStorageSubsystem::Deinitialize()
{
	// Save all dirty inventories before shutdown
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

	UE_LOG(LogTemp, Verbose, TEXT("ItemStorageSubsystem: Registered inventory '%s'"), *OwnerId);
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
			// Save if dirty before unregistering
			if (Component->IsDirty() && StorageBackend.GetInterface())
			{
				TArray<FItemInstance> Items = CollectItemsFromComponent(Component);
				ICGFItemStorageInterface::Execute_SaveInventory(
					StorageBackend.GetObject(), RegisteredInventories[i].OwnerId, Items);
				Component->ClearDirty();
			}

			UE_LOG(LogTemp, Verbose, TEXT("ItemStorageSubsystem: Unregistered inventory '%s'"),
				*RegisteredInventories[i].OwnerId);
			RegisteredInventories.RemoveAt(i);
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Manual Operations
// ---------------------------------------------------------------------------

void UItemStorageSubsystem::SaveInventory(UInventoryComponent* Component)
{
	if (!Component || !StorageBackend.GetInterface())
	{
		return;
	}

	// Find the owner ID for this component
	for (const FRegisteredInventory& Reg : RegisteredInventories)
	{
		if (Reg.Component.Get() == Component)
		{
			TArray<FItemInstance> Items = CollectItemsFromComponent(Component);
			ICGFItemStorageInterface::Execute_SaveInventory(StorageBackend.GetObject(), Reg.OwnerId, Items);
			Component->ClearDirty();
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ItemStorageSubsystem: SaveInventory called for unregistered component"));
}

void UItemStorageSubsystem::LoadInventory(UInventoryComponent* Component, const FString& OwnerId)
{
	if (!Component || !StorageBackend.GetInterface() || OwnerId.IsEmpty())
	{
		return;
	}

	TArray<FItemInstance> Items = ICGFItemStorageInterface::Execute_LoadInventory(
		StorageBackend.GetObject(), OwnerId);

	ApplyItemsToComponent(Component, Items);
	Component->ClearDirty();

	UE_LOG(LogTemp, Verbose, TEXT("ItemStorageSubsystem: Loaded %d items for '%s'"), Items.Num(), *OwnerId);
}

void UItemStorageSubsystem::SaveAll()
{
	if (!StorageBackend.GetInterface())
	{
		return;
	}

	int32 SaveCount = 0;
	for (FRegisteredInventory& Reg : RegisteredInventories)
	{
		UInventoryComponent* Comp = Reg.Component.Get();
		if (Comp && Comp->IsDirty())
		{
			TArray<FItemInstance> Items = CollectItemsFromComponent(Comp);
			ICGFItemStorageInterface::Execute_SaveInventory(StorageBackend.GetObject(), Reg.OwnerId, Items);
			Comp->ClearDirty();
			++SaveCount;
		}
	}

	if (SaveCount > 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("ItemStorageSubsystem: SaveAll saved %d inventories"), SaveCount);
	}
}

// ---------------------------------------------------------------------------
// Auto-Save
// ---------------------------------------------------------------------------

void UItemStorageSubsystem::OnAutoSaveTick()
{
	if (!StorageBackend.GetInterface())
	{
		return;
	}

	// Clean up stale entries and save dirty ones
	for (int32 i = RegisteredInventories.Num() - 1; i >= 0; --i)
	{
		FRegisteredInventory& Reg = RegisteredInventories[i];
		UInventoryComponent* Comp = Reg.Component.Get();

		if (!Comp)
		{
			RegisteredInventories.RemoveAt(i);
			continue;
		}

		if (Comp->IsDirty())
		{
			TArray<FItemInstance> Items = CollectItemsFromComponent(Comp);
			ICGFItemStorageInterface::Execute_SaveInventory(StorageBackend.GetObject(), Reg.OwnerId, Items);
			Comp->ClearDirty();
		}
	}
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
