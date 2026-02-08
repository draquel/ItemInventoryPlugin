#include "Components/InventoryComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Stackable.h"
#include "Net/UnrealNetwork.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Pre-allocate slots
	InventorySlots.Items.SetNum(MaxSlots);
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		InventorySlots.Items[i].SlotIndex = i;
		InventorySlots.Items[i].bIsOccupied = false;
	}

	InventorySlots.OwningComponent = this;
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

	Internal_RemoveItem(InstanceId, Count);
	return EInventoryOperationResult::Success;
}

EInventoryOperationResult UInventoryComponent::TryMoveItem(const FGuid& InstanceId,
	UInventoryComponent* TargetInventory, int32 TargetSlot)
{
	if (!TargetInventory)
	{
		return EInventoryOperationResult::Failed;
	}

	// Find source item
	const FInventorySlot* SourceSlot = FindSlotByInstanceId(InstanceId);
	if (!SourceSlot || !SourceSlot->bIsOccupied)
	{
		return EInventoryOperationResult::ItemNotFound;
	}

	FItemInstance ItemCopy = SourceSlot->Item;

	// Validate target can accept
	FInventoryOperationData TargetOpData;
	if (!TargetInventory->ValidateAddItem(ItemCopy, TargetSlot, TargetOpData))
	{
		return TargetOpData.Result;
	}

	// Validate source can release
	FInventoryOperationData SourceOpData;
	if (!ValidateRemoveItem(InstanceId, -1, SourceOpData))
	{
		return SourceOpData.Result;
	}

	// Atomic: remove from source, add to target
	Internal_RemoveItem(InstanceId, -1);
	TargetInventory->Internal_AddItemStacked(ItemCopy);

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
