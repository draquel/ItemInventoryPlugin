#include "Components/InventoryComponent.h"
#include "ItemInventoryPlugin.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Storage/ItemStorageSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Stackable.h"
#include "Net/UnrealNetwork.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Before any replication: on a client the first state arrives with the actor, before BeginPlay, and its callbacks fire.
	InventorySlots.OwningComponent = this;
	SetupReplicationCallbacks();
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// The slots are the authority's: it allocates them and marks each for replication, so they reach clients with their
	// IDs in slot order. A client keeps the array the server sent, which can already hold items (an inventory that
	// becomes relevant, a player joining). When a client built its own, the server's slots landed behind them and the
	// occupancy of what had arrived was cleared (G102).
	if (HasMutationAuthority())
	{
		InventorySlots.Items.SetNum(MaxSlots);
		for (int32 i = 0; i < MaxSlots; ++i)
		{
			InventorySlots.Items[i].SlotIndex = i;
			InventorySlots.Items[i].bIsOccupied = false;
			InventorySlots.MarkItemDirty(InventorySlots.Items[i]);
		}
	}

	// Register with storage subsystem for auto-save
	if (bAutoRegisterStorage && !StorageOwnerId.IsEmpty())
	{
		if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			if (UItemStorageSubsystem* StorageSS = GI->GetSubsystem<UItemStorageSubsystem>())
			{
				StorageSS->RegisterInventory(this, StorageOwnerId);
			}
		}
	}
}

void UInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from storage subsystem (will save if dirty)
	if (bAutoRegisterStorage && !StorageOwnerId.IsEmpty())
	{
		if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			if (UItemStorageSubsystem* StorageSS = GI->GetSubsystem<UItemStorageSubsystem>())
			{
				StorageSS->UnregisterInventory(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInventoryComponent, InventorySlots);
}

// ===========================================================================
// Operations
// ===========================================================================

EInventoryOperationResult UInventoryComponent::TryAddItem(const FItemInstance& Item, int32 PreferredSlot)
{
	// A client never hands the server an item: there is no add request (G91). Items enter on the server.
	if (!HasMutationAuthority())
	{
		UE_LOG(LogIIP, Warning, TEXT("TryAddItem on a client (%s): adding items is server-only."), *GetNameSafe(GetOwner()));
		return EInventoryOperationResult::NotAuthorized;
	}

	FInventoryOperationData OpData;
	if (!ValidateAddItem(Item, PreferredSlot, OpData))
	{
		return OpData.Result;
	}

	// Execute: try stacking first, then fill empty slots
	Internal_AddItemStacked(Item, PreferredSlot);
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TryRemoveItem(const FGuid& InstanceId, int32 Count)
{
	FInventoryOperationData OpData;
	if (!ValidateRemoveItem(InstanceId, Count, OpData))
	{
		return OpData.Result;
	}

	if (!HasMutationAuthority())
	{
		ServerRPC_RequestRemoveItem(InstanceId, Count);
		return EInventoryOperationResult::Pending;
	}

	Internal_RemoveItem(InstanceId, Count);
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TryMoveItem(const FGuid& InstanceId,
	UInventoryComponent* TargetInventory, int32 TargetSlot)
{
	FInventoryOperationData OpData;
	if (!ValidateMoveItem(InstanceId, TargetInventory, TargetSlot, OpData))
	{
		return OpData.Result;
	}

	if (!HasMutationAuthority())
	{
		ServerRPC_RequestMoveItem(InstanceId, TargetInventory, TargetSlot);
		return EInventoryOperationResult::Pending;
	}

	Internal_MoveItem(InstanceId, TargetInventory, TargetSlot);
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TryReplaceItem(const FGuid& InstanceId, const FItemInstance& NewItem)
{
	// Takes a whole item, so authority only, like TryAddItem (G91).
	if (!HasMutationAuthority())
	{
		UE_LOG(LogIIP, Warning, TEXT("TryReplaceItem on a client (%s): replacing items is server-only."), *GetNameSafe(GetOwner()));
		return EInventoryOperationResult::NotAuthorized;
	}

	FInventoryOperationData OpData;
	if (!ValidateReplaceItem(InstanceId, NewItem, OpData))
	{
		return OpData.Result;
	}

	const int32 SlotIndex = OpData.ResolvedSlot;
	const FItemInstance RemovedItem = InventorySlots.Items[SlotIndex].Item;
	Internal_ClearSlot(SlotIndex);
	OnItemRemoved.Broadcast(RemovedItem, SlotIndex);
	Internal_AddItem(NewItem, SlotIndex);
	MarkDirty();
	BroadcastChanged();
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TrySplitStack(const FGuid& InstanceId, int32 SplitCount)
{
	if (SplitCount <= 0)
	{
		return EInventoryOperationResult::Failed;
	}

	FInventorySlot* SourceSlot = FindSlotByInstanceId(InstanceId);
	if (!SourceSlot || !SourceSlot->bIsOccupied)
	{
		return EInventoryOperationResult::ItemNotFound;
	}

	if (SourceSlot->Item.StackCount <= SplitCount)
	{
		return EInventoryOperationResult::Failed;
	}

	// Need an empty slot for the new stack
	int32 EmptySlot = FindFirstEmptySlot();
	if (EmptySlot == INDEX_NONE)
	{
		return EInventoryOperationResult::InsufficientSpace;
	}

	if (!HasMutationAuthority())
	{
		ServerRPC_RequestSplitStack(InstanceId, SplitCount);
		return EInventoryOperationResult::Pending;
	}

	// Create new instance for the split portion
	FItemInstance NewInstance;
	NewInstance.InstanceId = FGuid::NewGuid();
	NewInstance.ItemDefinitionId = SourceSlot->Item.ItemDefinitionId;
	NewInstance.StackCount = SplitCount;

	// Deep copy instance fragments
	for (UItemInstanceFragment* Fragment : SourceSlot->Item.InstanceFragments)
	{
		if (Fragment)
		{
			UItemInstanceFragment* FragCopy = DuplicateObject<UItemInstanceFragment>(Fragment, GetTransientPackage());
			NewInstance.InstanceFragments.Add(FragCopy);
		}
	}

	// Decrement source
	SourceSlot->Item.StackCount -= SplitCount;
	InventorySlots.MarkItemDirty(*SourceSlot);
	OnItemUpdated.Broadcast(SourceSlot->Item, SourceSlot->SlotIndex);

	// Place new stack
	Internal_AddItem(NewInstance, EmptySlot);

	MarkDirty();
	BroadcastChanged();
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TryMergeStacks(const FGuid& SourceId, const FGuid& TargetId)
{
	FInventorySlot* SourceSlot = FindSlotByInstanceId(SourceId);
	FInventorySlot* TargetSlot = FindSlotByInstanceId(TargetId);

	if (!SourceSlot || !SourceSlot->bIsOccupied)
	{
		return EInventoryOperationResult::ItemNotFound;
	}
	if (!TargetSlot || !TargetSlot->bIsOccupied)
	{
		return EInventoryOperationResult::ItemNotFound;
	}

	// Must be same item type
	if (SourceSlot->Item.ItemDefinitionId != TargetSlot->Item.ItemDefinitionId)
	{
		return EInventoryOperationResult::Failed;
	}

	int32 MaxStack = GetMaxStackSize(TargetSlot->Item);
	int32 CombinedCount = SourceSlot->Item.StackCount + TargetSlot->Item.StackCount;

	if (CombinedCount > MaxStack)
	{
		return EInventoryOperationResult::StackFull;
	}

	if (!HasMutationAuthority())
	{
		ServerRPC_RequestMergeStacks(SourceId, TargetId);
		return EInventoryOperationResult::Pending;
	}

	// Merge: add source count to target, clear source
	FItemInstance RemovedItem = SourceSlot->Item;
	TargetSlot->Item.StackCount = CombinedCount;
	InventorySlots.MarkItemDirty(*TargetSlot);

	Internal_ClearSlot(SourceSlot->SlotIndex);

	OnItemRemoved.Broadcast(RemovedItem, SourceSlot->SlotIndex);
	OnItemUpdated.Broadcast(TargetSlot->Item, TargetSlot->SlotIndex);
	MarkDirty();
	BroadcastChanged();
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TrySwapSlots(int32 SlotA, int32 SlotB)
{
	if (SlotA < 0 || SlotA >= InventorySlots.Items.Num() ||
		SlotB < 0 || SlotB >= InventorySlots.Items.Num())
	{
		return EInventoryOperationResult::InvalidSlot;
	}

	if (SlotA == SlotB)
	{
		return EInventoryOperationResult::Success;
	}

	if (!HasMutationAuthority())
	{
		ServerRPC_RequestSwapSlots(SlotA, SlotB);
		return EInventoryOperationResult::Pending;
	}

	FInventorySlot& A = InventorySlots.Items[SlotA];
	FInventorySlot& B = InventorySlots.Items[SlotB];

	// Swap item data and occupied state
	Swap(A.Item, B.Item);
	Swap(A.bIsOccupied, B.bIsOccupied);

	InventorySlots.MarkItemDirty(A);
	InventorySlots.MarkItemDirty(B);

	if (A.bIsOccupied)
	{
		OnItemMoved.Broadcast(A.Item, SlotB, SlotA);
	}
	if (B.bIsOccupied)
	{
		OnItemMoved.Broadcast(B.Item, SlotA, SlotB);
	}

	MarkDirty();
	BroadcastChanged();
	return EInventoryOperationResult::Success;
}

// ===========================================================================
// Queries
// ===========================================================================

bool UInventoryComponent::HasItem(FPrimaryAssetId ItemDefId, int32 RequiredCount) const
{
	return GetItemCount(ItemDefId) >= RequiredCount;
}

int32 UInventoryComponent::GetItemCount(FPrimaryAssetId ItemDefId) const
{
	int32 Total = 0;
	for (const FInventorySlot& Slot : InventorySlots.Items)
	{
		if (Slot.bIsOccupied && Slot.Item.ItemDefinitionId == ItemDefId)
		{
			Total += Slot.Item.StackCount;
		}
	}
	return Total;
}

TArray<FItemInstance> UInventoryComponent::FindItemsByTag(const FGameplayTagContainer& Tags) const
{
	TArray<FItemInstance> Result;
	UItemDatabaseSubsystem* DB = GetItemDatabase();
	if (!DB)
	{
		return Result;
	}

	for (const FInventorySlot& Slot : InventorySlots.Items)
	{
		if (Slot.bIsOccupied)
		{
			UItemDefinition* Def = DB->GetDefinition(Slot.Item.ItemDefinitionId);
			if (Def && Def->ItemTags.HasAny(Tags))
			{
				Result.Add(Slot.Item);
			}
		}
	}
	return Result;
}

FItemInstance UInventoryComponent::GetItemInSlot(int32 SlotIndex) const
{
	if (SlotIndex >= 0 && SlotIndex < InventorySlots.Items.Num())
	{
		const FInventorySlot& Slot = InventorySlots.Items[SlotIndex];
		if (Slot.bIsOccupied)
		{
			return Slot.Item;
		}
	}
	return FItemInstance();
}

float UInventoryComponent::GetCurrentWeight() const
{
	float TotalWeight = 0.0f;
	UItemDatabaseSubsystem* DB = GetItemDatabase();
	if (!DB)
	{
		return TotalWeight;
	}

	for (const FInventorySlot& Slot : InventorySlots.Items)
	{
		if (Slot.bIsOccupied)
		{
			UItemDefinition* Def = DB->GetDefinition(Slot.Item.ItemDefinitionId);
			if (Def)
			{
				TotalWeight += Def->Weight * Slot.Item.StackCount;
			}
		}
	}
	return TotalWeight;
}

int32 UInventoryComponent::GetFilledSlotCount() const
{
	int32 Count = 0;
	for (const FInventorySlot& Slot : InventorySlots.Items)
	{
		if (Slot.bIsOccupied)
		{
			++Count;
		}
	}
	return Count;
}

bool UInventoryComponent::CanAcceptItem(const FItemInstance& Item) const
{
	FInventoryOperationData OpData;
	return ValidateAddItem(Item, -1, OpData);
}

int32 UInventoryComponent::FindFirstEmptySlot() const
{
	for (const FInventorySlot& Slot : InventorySlots.Items)
	{
		if (!Slot.bIsOccupied)
		{
			return Slot.SlotIndex;
		}
	}
	return INDEX_NONE;
}

FInventorySlot* UInventoryComponent::FindSlotByInstanceId(const FGuid& InstanceId)
{
	for (FInventorySlot& Slot : InventorySlots.Items)
	{
		if (Slot.bIsOccupied && Slot.Item.InstanceId == InstanceId)
		{
			return &Slot;
		}
	}
	return nullptr;
}

const FInventorySlot* UInventoryComponent::FindSlotByInstanceId(const FGuid& InstanceId) const
{
	for (const FInventorySlot& Slot : InventorySlots.Items)
	{
		if (Slot.bIsOccupied && Slot.Item.InstanceId == InstanceId)
		{
			return &Slot;
		}
	}
	return nullptr;
}

int32 UInventoryComponent::FindSlotIndexByInstanceId(const FGuid& InstanceId) const
{
	const FInventorySlot* Slot = FindSlotByInstanceId(InstanceId);
	return Slot ? Slot->SlotIndex : INDEX_NONE;
}

// ===========================================================================
// Validation
// ===========================================================================

bool UInventoryComponent::ValidateAddItem(const FItemInstance& Item, int32 PreferredSlot,
	FInventoryOperationData& OutData) const
{
	if (!Item.IsValid())
	{
		OutData.Result = EInventoryOperationResult::InvalidItem;
		return false;
	}

	if (!PassesTagFilter(Item))
	{
		OutData.Result = EInventoryOperationResult::FilterRejected;
		return false;
	}

	// Weight check
	if (MaxWeight > 0.0f)
	{
		float ItemWeight = GetItemWeight(Item) * Item.StackCount;
		if (GetCurrentWeight() + ItemWeight > MaxWeight)
		{
			OutData.Result = EInventoryOperationResult::WeightExceeded;
			return false;
		}
	}

	// Check if we can fit the full quantity (stacking + empty slots)
	int32 Remaining = Item.StackCount;
	int32 MaxStack = GetMaxStackSize(Item);

	// Count space in existing stacks
	if (MaxStack > 1)
	{
		for (const FInventorySlot& Slot : InventorySlots.Items)
		{
			if (Slot.bIsOccupied && Slot.Item.ItemDefinitionId == Item.ItemDefinitionId)
			{
				int32 Space = MaxStack - Slot.Item.StackCount;
				Remaining -= FMath::Min(Space, Remaining);
				if (Remaining <= 0) break;
			}
		}
	}

	// Count empty slots for remaining quantity
	if (Remaining > 0)
	{
		// Check preferred slot first
		if (PreferredSlot >= 0 && PreferredSlot < InventorySlots.Items.Num()
			&& !InventorySlots.Items[PreferredSlot].bIsOccupied)
		{
			Remaining -= FMath::Min(MaxStack, Remaining);
		}

		// Then other empty slots
		if (Remaining > 0)
		{
			for (const FInventorySlot& Slot : InventorySlots.Items)
			{
				if (!Slot.bIsOccupied && Slot.SlotIndex != PreferredSlot)
				{
					Remaining -= FMath::Min(MaxStack, Remaining);
					if (Remaining <= 0) break;
				}
			}
		}
	}

	if (Remaining > 0)
	{
		OutData.Result = EInventoryOperationResult::InsufficientSpace;
		return false;
	}

	OutData.ResolvedSlot = PreferredSlot;
	OutData.Result = EInventoryOperationResult::Success;
	return true;
}

bool UInventoryComponent::ValidateRemoveItem(const FGuid& InstanceId, int32 Count,
	FInventoryOperationData& OutData) const
{
	const FInventorySlot* Slot = FindSlotByInstanceId(InstanceId);
	if (!Slot || !Slot->bIsOccupied)
	{
		OutData.Result = EInventoryOperationResult::ItemNotFound;
		return false;
	}

	if (Count > 0 && Count > Slot->Item.StackCount)
	{
		OutData.Result = EInventoryOperationResult::Failed;
		return false;
	}

	OutData.ResolvedSlot = Slot->SlotIndex;
	OutData.Result = EInventoryOperationResult::Success;
	return true;
}

bool UInventoryComponent::ValidateAddItemAtSlot(const FItemInstance& Item, int32 Slot, bool bCountWeight,
	FInventoryOperationData& OutData) const
{
	if (!Item.IsValid())
	{
		OutData.Result = EInventoryOperationResult::InvalidItem;
		return false;
	}

	if (Slot < 0 || Slot >= InventorySlots.Items.Num())
	{
		OutData.Result = EInventoryOperationResult::InvalidSlot;
		return false;
	}

	if (!PassesTagFilter(Item))
	{
		OutData.Result = EInventoryOperationResult::FilterRejected;
		return false;
	}

	if (bCountWeight && MaxWeight > 0.0f && GetCurrentWeight() + GetItemWeight(Item) * Item.StackCount > MaxWeight)
	{
		OutData.Result = EInventoryOperationResult::WeightExceeded;
		return false;
	}

	// The slot takes the whole count or nothing: empty and the count is one stack, or the same item with room for all of it.
	const FInventorySlot& Target = InventorySlots.Items[Slot];
	const int32 MaxStack = GetMaxStackSize(Item);
	if (Target.bIsOccupied)
	{
		if (Target.Item.ItemDefinitionId != Item.ItemDefinitionId)
		{
			OutData.Result = EInventoryOperationResult::SlotOccupied;
			return false;
		}
		if (Target.Item.StackCount + Item.StackCount > MaxStack)
		{
			OutData.Result = EInventoryOperationResult::StackFull;
			return false;
		}
	}
	else if (Item.StackCount > MaxStack)
	{
		OutData.Result = EInventoryOperationResult::StackFull;
		return false;
	}

	OutData.ResolvedSlot = Slot;
	OutData.Result = EInventoryOperationResult::Success;
	return true;
}

bool UInventoryComponent::ValidateMoveItem(const FGuid& InstanceId, const UInventoryComponent* Target, int32 TargetSlot,
	FInventoryOperationData& OutData) const
{
	if (!Target)
	{
		OutData.Result = EInventoryOperationResult::Failed;
		return false;
	}

	if (!ValidateRemoveItem(InstanceId, -1, OutData))
	{
		return false;
	}
	const int32 SourceSlot = OutData.ResolvedSlot;
	const FItemInstance& Item = InventorySlots.Items[SourceSlot].Item;

	if (TargetSlot < 0)
	{
		return Target->ValidateAddItem(Item, TargetSlot, OutData);
	}

	// Onto its own slot: nothing to do, and nothing to refuse.
	if (Target == this && TargetSlot == SourceSlot)
	{
		OutData.ResolvedSlot = TargetSlot;
		OutData.Result = EInventoryOperationResult::Success;
		return true;
	}

	// Within one inventory the weight does not change.
	return Target->ValidateAddItemAtSlot(Item, TargetSlot, /*bCountWeight=*/Target != this, OutData);
}

bool UInventoryComponent::ValidateReplaceItem(const FGuid& InstanceId, const FItemInstance& NewItem,
	FInventoryOperationData& OutData) const
{
	if (!ValidateRemoveItem(InstanceId, -1, OutData))
	{
		return false;
	}
	const int32 SlotIndex = OutData.ResolvedSlot;
	const FItemInstance& Outgoing = InventorySlots.Items[SlotIndex].Item;

	if (!NewItem.IsValid())
	{
		OutData.Result = EInventoryOperationResult::InvalidItem;
		return false;
	}

	if (!PassesTagFilter(NewItem))
	{
		OutData.Result = EInventoryOperationResult::FilterRejected;
		return false;
	}

	if (NewItem.StackCount > GetMaxStackSize(NewItem))
	{
		OutData.Result = EInventoryOperationResult::StackFull;
		return false;
	}

	if (MaxWeight > 0.0f)
	{
		const float WeightAfter = GetCurrentWeight() - GetItemWeight(Outgoing) * Outgoing.StackCount
			+ GetItemWeight(NewItem) * NewItem.StackCount;
		if (WeightAfter > MaxWeight)
		{
			OutData.Result = EInventoryOperationResult::WeightExceeded;
			return false;
		}
	}

	OutData.ResolvedSlot = SlotIndex;
	OutData.Result = EInventoryOperationResult::Success;
	return true;
}

bool UInventoryComponent::HasMutationAuthority() const
{
	const AActor* Owner = GetOwner();
	return !Owner || Owner->HasAuthority();
}

namespace
{
	/** The actor at the top of an owner chain: a player's controller for its pawn, player state and controller alike. */
	const AActor* GetOwnerChainRoot(const AActor* Actor)
	{
		while (Actor && Actor->GetOwner())
		{
			Actor = Actor->GetOwner();
		}
		return Actor;
	}
}

bool UInventoryComponent::SetOpenTo(AActor* Requester, bool bOpen)
{
	if (!HasMutationAuthority())
	{
		UE_LOG(LogIIP, Warning, TEXT("SetOpenTo on a client (%s): opening an inventory is server-only."), *GetNameSafe(GetOwner()));
		return false;
	}
	if (!Requester)
	{
		UE_LOG(LogIIP, Warning, TEXT("SetOpenTo on %s: no requester."), *GetNameSafe(GetOwner()));
		return false;
	}
	const AActor* Player = GetOwnerChainRoot(Requester);
	OpenToRequesters.RemoveAll([Player](const TWeakObjectPtr<const AActor>& Opened)
	{
		return !Opened.IsValid() || GetOwnerChainRoot(Opened.Get()) == Player;
	});
	if (bOpen)
	{
		OpenToRequesters.Add(Requester);
	}
	return true;
}

bool UInventoryComponent::IsReachableBy(const AActor* Requester) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Requester)
	{
		return false;
	}
	const AActor* Player = GetOwnerChainRoot(Requester);
	if (GetOwnerChainRoot(Owner) == Player)
	{
		return true;
	}
	for (const TWeakObjectPtr<const AActor>& Opened : OpenToRequesters)
	{
		if (Opened.IsValid() && GetOwnerChainRoot(Opened.Get()) == Player)
		{
			return true;
		}
	}
	return false;
}

bool UInventoryComponent::PassesTagFilter(const FItemInstance& Item) const
{
	UItemDatabaseSubsystem* DB = GetItemDatabase();
	if (!DB)
	{
		return true;
	}

	UItemDefinition* Def = DB->GetDefinition(Item.ItemDefinitionId);
	if (!Def)
	{
		return true;
	}

	// Denied tags: reject if item has any
	if (DeniedItemTags.Num() > 0 && Def->ItemTags.HasAny(DeniedItemTags))
	{
		return false;
	}

	// Allowed tags: if set, item must have at least one
	if (AllowedItemTags.Num() > 0 && !Def->ItemTags.HasAny(AllowedItemTags))
	{
		return false;
	}

	return true;
}

int32 UInventoryComponent::GetMaxStackSize(const FItemInstance& Item) const
{
	UItemDatabaseSubsystem* DB = GetItemDatabase();
	if (!DB)
	{
		return 1;
	}

	UItemDefinition* Def = DB->GetDefinition(Item.ItemDefinitionId);
	if (!Def)
	{
		return 1;
	}

	// Stackable fragment overrides definition MaxStackSize
	UItemFragment_Stackable* StackFrag = Def->FindFragment<UItemFragment_Stackable>();
	if (StackFrag)
	{
		return StackFrag->MaxStackSize;
	}

	return Def->MaxStackSize;
}

float UInventoryComponent::GetItemWeight(const FItemInstance& Item) const
{
	UItemDatabaseSubsystem* DB = GetItemDatabase();
	if (!DB)
	{
		return 0.0f;
	}

	UItemDefinition* Def = DB->GetDefinition(Item.ItemDefinitionId);
	return Def ? Def->Weight : 0.0f;
}

// ===========================================================================
// Internal Mutation
// ===========================================================================

void UInventoryComponent::Internal_AddItem(const FItemInstance& Item, int32 TargetSlot)
{
	if (TargetSlot < 0 || TargetSlot >= InventorySlots.Items.Num())
	{
		return;
	}

	FInventorySlot& Slot = InventorySlots.Items[TargetSlot];
	Slot.Item = Item;
	Slot.bIsOccupied = true;
	InventorySlots.MarkItemDirty(Slot);

	OnItemAdded.Broadcast(Item, TargetSlot);
}

void UInventoryComponent::Internal_AddItemStacked(const FItemInstance& Item, int32 PreferredSlot)
{
	int32 Remaining = Item.StackCount;
	int32 MaxStack = GetMaxStackSize(Item);

	// Phase 1: Fill existing stacks of same type
	if (MaxStack > 1)
	{
		for (FInventorySlot& Slot : InventorySlots.Items)
		{
			if (Remaining <= 0) break;
			if (Slot.bIsOccupied && Slot.Item.ItemDefinitionId == Item.ItemDefinitionId)
			{
				int32 Space = MaxStack - Slot.Item.StackCount;
				if (Space > 0)
				{
					int32 ToAdd = FMath::Min(Space, Remaining);
					Slot.Item.StackCount += ToAdd;
					Remaining -= ToAdd;
					InventorySlots.MarkItemDirty(Slot);
					OnItemUpdated.Broadcast(Slot.Item, Slot.SlotIndex);
				}
			}
		}
	}

	// Phase 2: Place remaining in empty slots (preferred slot first)
	bool bUsedPreferred = false;
	while (Remaining > 0)
	{
		int32 EmptySlot = INDEX_NONE;

		// Try preferred slot first if not yet used
		if (!bUsedPreferred && PreferredSlot >= 0 && PreferredSlot < InventorySlots.Items.Num()
			&& !InventorySlots.Items[PreferredSlot].bIsOccupied)
		{
			EmptySlot = PreferredSlot;
			bUsedPreferred = true;
		}
		else
		{
			EmptySlot = FindFirstEmptySlot();
		}

		if (EmptySlot == INDEX_NONE)
		{
			break; // Validation should have prevented this
		}

		int32 ToPlace = FMath::Min(MaxStack, Remaining);

		FItemInstance NewStack;
		NewStack.ItemDefinitionId = Item.ItemDefinitionId;
		NewStack.StackCount = ToPlace;

		// First new slot gets the original GUID; subsequent ones get new GUIDs
		if (Remaining == Item.StackCount && ToPlace == Item.StackCount)
		{
			// Entire item fits in one slot — use original instance
			NewStack.InstanceId = Item.InstanceId;
			NewStack.InstanceFragments = Item.InstanceFragments;
		}
		else
		{
			NewStack.InstanceId = (Remaining == Item.StackCount) ? Item.InstanceId : FGuid::NewGuid();
			// Copy fragments for new stacks
			for (UItemInstanceFragment* Fragment : Item.InstanceFragments)
			{
				if (Fragment)
				{
					NewStack.InstanceFragments.Add(
						DuplicateObject<UItemInstanceFragment>(Fragment, GetTransientPackage()));
				}
			}
		}

		Internal_AddItem(NewStack, EmptySlot);
		Remaining -= ToPlace;
	}

	MarkDirty();
	BroadcastChanged();
}

void UInventoryComponent::Internal_AddItemAtSlot(const FItemInstance& Item, int32 Slot)
{
	if (Slot < 0 || Slot >= InventorySlots.Items.Num())
	{
		return;
	}

	FInventorySlot& Target = InventorySlots.Items[Slot];
	if (Target.bIsOccupied)
	{
		// The same item (validated): its count joins the stack there, and the moved instance id ends with it.
		Target.Item.StackCount += Item.StackCount;
		InventorySlots.MarkItemDirty(Target);
		OnItemUpdated.Broadcast(Target.Item, Slot);
	}
	else
	{
		Internal_AddItem(Item, Slot);
	}

	MarkDirty();
	BroadcastChanged();
}

void UInventoryComponent::Internal_MoveItem(const FGuid& InstanceId, UInventoryComponent* Target, int32 TargetSlot)
{
	const FInventorySlot* SourceSlot = FindSlotByInstanceId(InstanceId);
	if (!Target || !SourceSlot)
	{
		return;
	}
	if (Target == this && TargetSlot == SourceSlot->SlotIndex)
	{
		return;
	}

	// Atomic (both sides were validated): remove from the source, then add to the target.
	const FItemInstance ItemCopy = SourceSlot->Item;
	Internal_RemoveItem(InstanceId, -1);
	if (TargetSlot >= 0)
	{
		Target->Internal_AddItemAtSlot(ItemCopy, TargetSlot);
	}
	else
	{
		Target->Internal_AddItemStacked(ItemCopy);
	}
}

void UInventoryComponent::Internal_RemoveItem(const FGuid& InstanceId, int32 Count)
{
	FInventorySlot* Slot = FindSlotByInstanceId(InstanceId);
	if (!Slot || !Slot->bIsOccupied)
	{
		return;
	}

	FItemInstance RemovedItem = Slot->Item;

	if (Count < 0 || Count >= Slot->Item.StackCount)
	{
		// Remove entire stack
		Internal_ClearSlot(Slot->SlotIndex);
		OnItemRemoved.Broadcast(RemovedItem, Slot->SlotIndex);
	}
	else
	{
		// Partial removal
		Slot->Item.StackCount -= Count;
		InventorySlots.MarkItemDirty(*Slot);
		OnItemUpdated.Broadcast(Slot->Item, Slot->SlotIndex);
	}

	MarkDirty();
	BroadcastChanged();
}

void UInventoryComponent::Internal_ClearSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= InventorySlots.Items.Num())
	{
		return;
	}

	FInventorySlot& Slot = InventorySlots.Items[SlotIndex];
	Slot.Item = FItemInstance();
	Slot.bIsOccupied = false;
	InventorySlots.MarkItemDirty(Slot);
}

void UInventoryComponent::MarkDirty()
{
	bIsDirty = true;
}

void UInventoryComponent::BroadcastChanged()
{
	OnInventoryChanged.Broadcast();
}

// ===========================================================================
// Server RPCs
// ===========================================================================

void UInventoryComponent::ServerRPC_RequestRemoveItem_Implementation(const FGuid& InstanceId, int32 Count)
{
	FInventoryOperationData OpData;
	if (!ValidateRemoveItem(InstanceId, Count, OpData))
	{
		ClientRPC_OperationFailed(OpData.Result);
		return;
	}
	Internal_RemoveItem(InstanceId, Count);
}

void UInventoryComponent::ServerRPC_RequestMoveItem_Implementation(const FGuid& InstanceId,
	UInventoryComponent* Target, int32 TargetSlot)
{
	// G96: the client may name only an inventory its player can reach (its own, or one opened to it).
	if (Target && !Target->IsReachableBy(GetOwner()))
	{
		UE_LOG(LogIIP, Warning, TEXT("ServerRPC_RequestMoveItem: %s named an inventory it cannot reach (%s)."),
			*GetNameSafe(GetOwner()), *GetNameSafe(Target->GetOwner()));
		ClientRPC_OperationFailed(EInventoryOperationResult::NotAuthorized);
		return;
	}

	FInventoryOperationData OpData;
	if (!ValidateMoveItem(InstanceId, Target, TargetSlot, OpData))
	{
		ClientRPC_OperationFailed(OpData.Result);
		return;
	}
	Internal_MoveItem(InstanceId, Target, TargetSlot);
}

void UInventoryComponent::ServerRPC_RequestSplitStack_Implementation(const FGuid& InstanceId, int32 SplitCount)
{
	if (SplitCount <= 0)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::Failed);
		return;
	}

	FInventorySlot* SourceSlot = FindSlotByInstanceId(InstanceId);
	if (!SourceSlot || !SourceSlot->bIsOccupied)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::ItemNotFound);
		return;
	}

	if (SourceSlot->Item.StackCount <= SplitCount)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::Failed);
		return;
	}

	int32 EmptySlot = FindFirstEmptySlot();
	if (EmptySlot == INDEX_NONE)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::InsufficientSpace);
		return;
	}

	// Create new instance for the split portion
	FItemInstance NewInstance;
	NewInstance.InstanceId = FGuid::NewGuid();
	NewInstance.ItemDefinitionId = SourceSlot->Item.ItemDefinitionId;
	NewInstance.StackCount = SplitCount;

	for (UItemInstanceFragment* Fragment : SourceSlot->Item.InstanceFragments)
	{
		if (Fragment)
		{
			NewInstance.InstanceFragments.Add(
				DuplicateObject<UItemInstanceFragment>(Fragment, GetTransientPackage()));
		}
	}

	SourceSlot->Item.StackCount -= SplitCount;
	InventorySlots.MarkItemDirty(*SourceSlot);
	OnItemUpdated.Broadcast(SourceSlot->Item, SourceSlot->SlotIndex);

	Internal_AddItem(NewInstance, EmptySlot);

	MarkDirty();
	BroadcastChanged();
}

void UInventoryComponent::ServerRPC_RequestMergeStacks_Implementation(const FGuid& SourceId, const FGuid& TargetId)
{
	FInventorySlot* SourceSlot = FindSlotByInstanceId(SourceId);
	FInventorySlot* TargetSlot = FindSlotByInstanceId(TargetId);

	if (!SourceSlot || !SourceSlot->bIsOccupied || !TargetSlot || !TargetSlot->bIsOccupied)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::ItemNotFound);
		return;
	}

	if (SourceSlot->Item.ItemDefinitionId != TargetSlot->Item.ItemDefinitionId)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::Failed);
		return;
	}

	int32 MaxStack = GetMaxStackSize(TargetSlot->Item);
	int32 CombinedCount = SourceSlot->Item.StackCount + TargetSlot->Item.StackCount;

	if (CombinedCount > MaxStack)
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::StackFull);
		return;
	}

	FItemInstance RemovedItem = SourceSlot->Item;
	TargetSlot->Item.StackCount = CombinedCount;
	InventorySlots.MarkItemDirty(*TargetSlot);

	Internal_ClearSlot(SourceSlot->SlotIndex);

	OnItemRemoved.Broadcast(RemovedItem, SourceSlot->SlotIndex);
	OnItemUpdated.Broadcast(TargetSlot->Item, TargetSlot->SlotIndex);
	MarkDirty();
	BroadcastChanged();
}

void UInventoryComponent::ServerRPC_RequestSwapSlots_Implementation(int32 SlotA, int32 SlotB)
{
	if (SlotA < 0 || SlotA >= InventorySlots.Items.Num() ||
		SlotB < 0 || SlotB >= InventorySlots.Items.Num())
	{
		ClientRPC_OperationFailed(EInventoryOperationResult::InvalidSlot);
		return;
	}

	if (SlotA == SlotB)
	{
		return;
	}

	FInventorySlot& A = InventorySlots.Items[SlotA];
	FInventorySlot& B = InventorySlots.Items[SlotB];

	Swap(A.Item, B.Item);
	Swap(A.bIsOccupied, B.bIsOccupied);

	InventorySlots.MarkItemDirty(A);
	InventorySlots.MarkItemDirty(B);

	if (A.bIsOccupied)
	{
		OnItemMoved.Broadcast(A.Item, SlotB, SlotA);
	}
	if (B.bIsOccupied)
	{
		OnItemMoved.Broadcast(B.Item, SlotA, SlotB);
	}

	MarkDirty();
	BroadcastChanged();
}

// ===========================================================================
// Client RPC
// ===========================================================================

void UInventoryComponent::ClientRPC_OperationFailed_Implementation(EInventoryOperationResult Result)
{
	OnOperationFailed.Broadcast(Result);
}

// ===========================================================================
// Replication Callbacks
// ===========================================================================

void UInventoryComponent::SetupReplicationCallbacks()
{
	InventorySlots.OnSlotAddedCallback = [this](const FInventorySlot& Slot)
	{
		// A slot arriving on a client: an item only if it holds one (the empty slots arrive with the inventory).
		if (Slot.bIsOccupied)
		{
			OnItemAdded.Broadcast(Slot.Item, Slot.SlotIndex);
		}
		OnInventoryChanged.Broadcast();
	};

	InventorySlots.OnSlotRemovedCallback = [this](const FInventorySlot& Slot)
	{
		OnItemRemoved.Broadcast(Slot.Item, Slot.SlotIndex);
		OnInventoryChanged.Broadcast();
	};

	InventorySlots.OnSlotChangedCallback = [this](const FInventorySlot& Slot)
	{
		OnItemUpdated.Broadcast(Slot.Item, Slot.SlotIndex);
		OnInventoryChanged.Broadcast();
	};
}

// ===========================================================================
// Helpers
// ===========================================================================

UItemDatabaseSubsystem* UInventoryComponent::GetItemDatabase() const
{
	if (!CachedItemDatabase)
	{
		UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (GI)
		{
			CachedItemDatabase = GI->GetSubsystem<UItemDatabaseSubsystem>();
		}
	}
	return CachedItemDatabase;
}

// ===========================================================================
// Storage Operations
// ===========================================================================

bool UInventoryComponent::ManualSave()
{
	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UItemStorageSubsystem* StorageSS = GI->GetSubsystem<UItemStorageSubsystem>())
		{
			return StorageSS->SaveInventory(this);
		}
	}
	UE_LOG(LogIIP, Warning, TEXT("InventoryComponent: ManualSave on '%s' found no ItemStorageSubsystem"), *GetNameSafe(GetOwner()));
	return false;
}

bool UInventoryComponent::ManualLoad()
{
	if (StorageOwnerId.IsEmpty())
	{
		UE_LOG(LogIIP, Warning, TEXT("InventoryComponent: ManualLoad on '%s' refused: no StorageOwnerId"), *GetNameSafe(GetOwner()));
		return false;
	}

	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UItemStorageSubsystem* StorageSS = GI->GetSubsystem<UItemStorageSubsystem>())
		{
			return StorageSS->LoadInventory(this, StorageOwnerId);
		}
	}
	UE_LOG(LogIIP, Warning, TEXT("InventoryComponent: ManualLoad on '%s' found no ItemStorageSubsystem"), *GetNameSafe(GetOwner()));
	return false;
}
