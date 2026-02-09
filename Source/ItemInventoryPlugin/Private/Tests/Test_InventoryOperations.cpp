#include "Misc/AutomationTest.h"
#include "Components/InventoryComponent.h"
#include "Types/CGFItemTypes.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Helper: create a component with pre-allocated slots (no world needed)
// Root objects to prevent GC collection during test execution.
// ---------------------------------------------------------------------------
namespace InventoryTestHelpers
{
	UInventoryComponent* CreateTestInventory(int32 Slots = 10, float Weight = 0.0f)
	{
		UInventoryComponent* Comp = NewObject<UInventoryComponent>();
		Comp->AddToRoot();
		Comp->MaxSlots = Slots;
		Comp->MaxWeight = Weight;

		// Reproduce BeginPlay slot pre-allocation
		Comp->InventorySlots.Items.SetNum(Slots);
		for (int32 i = 0; i < Slots; ++i)
		{
			Comp->InventorySlots.Items[i].SlotIndex = i;
			Comp->InventorySlots.Items[i].bIsOccupied = false;
		}
		Comp->InventorySlots.OwningComponent = Comp;
		return Comp;
	}

	void CleanupInventory(UInventoryComponent* Comp)
	{
		if (Comp) { Comp->RemoveFromRoot(); }
	}

	FItemInstance CreateTestItem(const FString& DefName = TEXT("TestItem"), int32 Stack = 1)
	{
		FItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), FName(*DefName));
		Item.StackCount = Stack;
		return Item;
	}

	// Directly place an item in a slot (bypasses TryAddItem for stacking tests)
	void PlaceItemInSlot(UInventoryComponent* Comp, const FItemInstance& Item, int32 Slot)
	{
		FInventorySlot& S = Comp->InventorySlots.Items[Slot];
		S.Item = Item;
		S.bIsOccupied = true;
	}
}

// ===========================================================================
// TryAddItem
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAdd_Basic,
	"ItemInventory.Operations.Add.BasicAdd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAdd_Basic::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);
	FItemInstance Item = InventoryTestHelpers::CreateTestItem();

	EInventoryOperationResult Result = Comp->TryAddItem(Item);
	TestEqual("Add succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("Filled slots", Comp->GetFilledSlotCount(), 1);
	TestTrue("Item in slot 0", Comp->GetItemInSlot(0).IsValid());
	TestEqual("Instance ID matches", Comp->GetItemInSlot(0).InstanceId, Item.InstanceId);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAdd_PreferredSlot,
	"ItemInventory.Operations.Add.PreferredSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAdd_PreferredSlot::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);
	FItemInstance Item = InventoryTestHelpers::CreateTestItem();

	EInventoryOperationResult Result = Comp->TryAddItem(Item, 3);
	TestEqual("Add succeeds", Result, EInventoryOperationResult::Success);
	TestTrue("Item in slot 3", Comp->GetItemInSlot(3).IsValid());
	TestFalse("Slot 0 empty", Comp->GetItemInSlot(0).IsValid());

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAdd_FullInventory,
	"ItemInventory.Operations.Add.FullInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAdd_FullInventory::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(3);

	// Fill all slots
	for (int32 i = 0; i < 3; ++i)
	{
		FItemInstance Item = InventoryTestHelpers::CreateTestItem(FString::Printf(TEXT("Item%d"), i));
		TestEqual("Add succeeds", Comp->TryAddItem(Item), EInventoryOperationResult::Success);
	}

	// Try to add one more
	FItemInstance Extra = InventoryTestHelpers::CreateTestItem(TEXT("Extra"));
	EInventoryOperationResult Result = Comp->TryAddItem(Extra);
	TestEqual("Full inventory rejects", Result, EInventoryOperationResult::InsufficientSpace);
	TestEqual("Still 3 items", Comp->GetFilledSlotCount(), 3);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAdd_InvalidItem,
	"ItemInventory.Operations.Add.InvalidItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAdd_InvalidItem::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Invalid; // No GUID, no definition
	EInventoryOperationResult Result = Comp->TryAddItem(Invalid);
	TestEqual("Invalid item rejected", Result, EInventoryOperationResult::InvalidItem);
	TestEqual("Nothing added", Comp->GetFilledSlotCount(), 0);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAdd_MultipleItems,
	"ItemInventory.Operations.Add.MultipleItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAdd_MultipleItems::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(10);

	for (int32 i = 0; i < 5; ++i)
	{
		FItemInstance Item = InventoryTestHelpers::CreateTestItem(FString::Printf(TEXT("Item%d"), i));
		TestEqual(FString::Printf(TEXT("Add %d succeeds"), i),
			Comp->TryAddItem(Item), EInventoryOperationResult::Success);
	}

	TestEqual("5 filled slots", Comp->GetFilledSlotCount(), 5);
	TestEqual("5 empty remaining", Comp->FindFirstEmptySlot(), 5);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// TryRemoveItem
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRemove_Basic,
	"ItemInventory.Operations.Remove.BasicRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryRemove_Basic::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);
	FItemInstance Item = InventoryTestHelpers::CreateTestItem();
	Comp->TryAddItem(Item);

	EInventoryOperationResult Result = Comp->TryRemoveItem(Item.InstanceId);
	TestEqual("Remove succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("Inventory empty", Comp->GetFilledSlotCount(), 0);
	TestFalse("Slot 0 empty", Comp->GetItemInSlot(0).IsValid());

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRemove_NotFound,
	"ItemInventory.Operations.Remove.NotFound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryRemove_NotFound::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FGuid FakeId = FGuid::NewGuid();
	EInventoryOperationResult Result = Comp->TryRemoveItem(FakeId);
	TestEqual("Not found", Result, EInventoryOperationResult::ItemNotFound);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRemove_Partial,
	"ItemInventory.Operations.Remove.PartialStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryRemove_Partial::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	// Place a stacked item directly (bypasses add stacking logic)
	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Stackable"), 10);
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	// Remove 3 from the stack
	EInventoryOperationResult Result = Comp->TryRemoveItem(Item.InstanceId, 3);
	TestEqual("Partial remove succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("Stack reduced to 7", Comp->GetItemInSlot(0).StackCount, 7);
	TestEqual("Still 1 slot filled", Comp->GetFilledSlotCount(), 1);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRemove_ExactStack,
	"ItemInventory.Operations.Remove.ExactStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryRemove_ExactStack::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Stackable"), 5);
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	// Remove exactly 5 — should clear slot
	EInventoryOperationResult Result = Comp->TryRemoveItem(Item.InstanceId, 5);
	TestEqual("Remove succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("Inventory empty", Comp->GetFilledSlotCount(), 0);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRemove_OverCount,
	"ItemInventory.Operations.Remove.OverCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryRemove_OverCount::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Stackable"), 3);
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	// Try to remove more than exists
	EInventoryOperationResult Result = Comp->TryRemoveItem(Item.InstanceId, 10);
	TestEqual("Over-remove fails", Result, EInventoryOperationResult::Failed);
	TestEqual("Item still there", Comp->GetFilledSlotCount(), 1);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// TrySwapSlots
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySwap_BothOccupied,
	"ItemInventory.Operations.Swap.BothOccupied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySwap_BothOccupied::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance ItemA = InventoryTestHelpers::CreateTestItem(TEXT("A"));
	FItemInstance ItemB = InventoryTestHelpers::CreateTestItem(TEXT("B"));
	InventoryTestHelpers::PlaceItemInSlot(Comp, ItemA, 0);
	InventoryTestHelpers::PlaceItemInSlot(Comp, ItemB, 2);

	EInventoryOperationResult Result = Comp->TrySwapSlots(0, 2);
	TestEqual("Swap succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("A now in slot 2", Comp->GetItemInSlot(2).InstanceId, ItemA.InstanceId);
	TestEqual("B now in slot 0", Comp->GetItemInSlot(0).InstanceId, ItemB.InstanceId);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySwap_OneEmpty,
	"ItemInventory.Operations.Swap.OneEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySwap_OneEmpty::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem();
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 1);

	EInventoryOperationResult Result = Comp->TrySwapSlots(1, 4);
	TestEqual("Swap succeeds", Result, EInventoryOperationResult::Success);
	TestFalse("Slot 1 now empty", Comp->GetItemInSlot(1).IsValid());
	TestEqual("Item now in slot 4", Comp->GetItemInSlot(4).InstanceId, Item.InstanceId);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySwap_SameSlot,
	"ItemInventory.Operations.Swap.SameSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySwap_SameSlot::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);
	FItemInstance Item = InventoryTestHelpers::CreateTestItem();
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	EInventoryOperationResult Result = Comp->TrySwapSlots(0, 0);
	TestEqual("Same slot = success (no-op)", Result, EInventoryOperationResult::Success);
	TestEqual("Item unchanged", Comp->GetItemInSlot(0).InstanceId, Item.InstanceId);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySwap_InvalidSlot,
	"ItemInventory.Operations.Swap.InvalidSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySwap_InvalidSlot::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	EInventoryOperationResult Result = Comp->TrySwapSlots(0, 99);
	TestEqual("Invalid slot rejected", Result, EInventoryOperationResult::InvalidSlot);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// TryMoveItem (cross-inventory)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMove_Basic,
	"ItemInventory.Operations.Move.BasicMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMove_Basic::RunTest(const FString& Parameters)
{
	auto* Source = InventoryTestHelpers::CreateTestInventory(5);
	auto* Target = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem();
	InventoryTestHelpers::PlaceItemInSlot(Source, Item, 0);

	EInventoryOperationResult Result = Source->TryMoveItem(Item.InstanceId, Target);
	TestEqual("Move succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("Source empty", Source->GetFilledSlotCount(), 0);
	TestEqual("Target has item", Target->GetFilledSlotCount(), 1);
	TestEqual("Same instance ID", Target->GetItemInSlot(0).InstanceId, Item.InstanceId);

	InventoryTestHelpers::CleanupInventory(Source);
	InventoryTestHelpers::CleanupInventory(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMove_TargetFull,
	"ItemInventory.Operations.Move.TargetFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMove_TargetFull::RunTest(const FString& Parameters)
{
	auto* Source = InventoryTestHelpers::CreateTestInventory(5);
	auto* Target = InventoryTestHelpers::CreateTestInventory(2);

	// Fill target
	for (int32 i = 0; i < 2; ++i)
	{
		InventoryTestHelpers::PlaceItemInSlot(Target,
			InventoryTestHelpers::CreateTestItem(FString::Printf(TEXT("Fill%d"), i)), i);
	}

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Source"));
	InventoryTestHelpers::PlaceItemInSlot(Source, Item, 0);

	EInventoryOperationResult Result = Source->TryMoveItem(Item.InstanceId, Target);
	TestEqual("Move fails - target full", Result, EInventoryOperationResult::InsufficientSpace);
	TestEqual("Source still has item", Source->GetFilledSlotCount(), 1);

	InventoryTestHelpers::CleanupInventory(Source);
	InventoryTestHelpers::CleanupInventory(Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMove_NullTarget,
	"ItemInventory.Operations.Move.NullTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMove_NullTarget::RunTest(const FString& Parameters)
{
	auto* Source = InventoryTestHelpers::CreateTestInventory(5);
	FItemInstance Item = InventoryTestHelpers::CreateTestItem();
	InventoryTestHelpers::PlaceItemInSlot(Source, Item, 0);

	EInventoryOperationResult Result = Source->TryMoveItem(Item.InstanceId, nullptr);
	TestEqual("Null target fails", Result, EInventoryOperationResult::Failed);

	InventoryTestHelpers::CleanupInventory(Source);
	return true;
}

// ===========================================================================
// TrySplitStack
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySplit_Basic,
	"ItemInventory.Operations.Split.BasicSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySplit_Basic::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Stackable"), 10);
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	EInventoryOperationResult Result = Comp->TrySplitStack(Item.InstanceId, 3);
	TestEqual("Split succeeds", Result, EInventoryOperationResult::Success);
	TestEqual("Source reduced to 7", Comp->GetItemInSlot(0).StackCount, 7);
	TestEqual("2 slots filled", Comp->GetFilledSlotCount(), 2);

	// New stack should be in slot 1 (first empty)
	FItemInstance NewStack = Comp->GetItemInSlot(1);
	TestTrue("New stack valid", NewStack.IsValid());
	TestEqual("New stack count = 3", NewStack.StackCount, 3);
	TestNotEqual("Different GUID", NewStack.InstanceId, Item.InstanceId);
	TestEqual("Same definition", NewStack.ItemDefinitionId, Item.ItemDefinitionId);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySplit_NoRoom,
	"ItemInventory.Operations.Split.NoRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySplit_NoRoom::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(1);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Stackable"), 10);
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	EInventoryOperationResult Result = Comp->TrySplitStack(Item.InstanceId, 3);
	TestEqual("Split fails - no room", Result, EInventoryOperationResult::InsufficientSpace);
	TestEqual("Stack unchanged", Comp->GetItemInSlot(0).StackCount, 10);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySplit_InvalidCount,
	"ItemInventory.Operations.Split.InvalidCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventorySplit_InvalidCount::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Stackable"), 5);
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	// Try to split more than or equal to the stack
	TestEqual("Split all fails", Comp->TrySplitStack(Item.InstanceId, 5), EInventoryOperationResult::Failed);
	TestEqual("Split more fails", Comp->TrySplitStack(Item.InstanceId, 10), EInventoryOperationResult::Failed);
	TestEqual("Split zero fails", Comp->TrySplitStack(Item.InstanceId, 0), EInventoryOperationResult::Failed);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// TryMergeStacks
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMerge_DifferentDefs,
	"ItemInventory.Operations.Merge.DifferentDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMerge_DifferentDefs::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance A = InventoryTestHelpers::CreateTestItem(TEXT("TypeA"), 3);
	FItemInstance B = InventoryTestHelpers::CreateTestItem(TEXT("TypeB"), 3);
	InventoryTestHelpers::PlaceItemInSlot(Comp, A, 0);
	InventoryTestHelpers::PlaceItemInSlot(Comp, B, 1);

	EInventoryOperationResult Result = Comp->TryMergeStacks(A.InstanceId, B.InstanceId);
	TestEqual("Different defs rejected", Result, EInventoryOperationResult::Failed);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

// Note: TryMergeStacks for same-type items requires ItemDatabaseSubsystem
// to resolve MaxStackSize. Without the database, MaxStackSize defaults to 1,
// causing all merges to fail with StackFull. Full merge tests require PIE context.

// ===========================================================================
// Queries
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryQuery_HasItem,
	"ItemInventory.Queries.HasItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryQuery_HasItem::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance Item = InventoryTestHelpers::CreateTestItem(TEXT("Sword"));
	InventoryTestHelpers::PlaceItemInSlot(Comp, Item, 0);

	TestTrue("Has 1 sword", Comp->HasItem(Item.ItemDefinitionId, 1));
	TestFalse("Doesn't have 2 swords", Comp->HasItem(Item.ItemDefinitionId, 2));

	FPrimaryAssetId FakeId(TEXT("ItemDefinition"), TEXT("Nonexistent"));
	TestFalse("Doesn't have fake item", Comp->HasItem(FakeId));

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryQuery_GetItemCount,
	"ItemInventory.Queries.GetItemCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryQuery_GetItemCount::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(10);

	FPrimaryAssetId SwordId(TEXT("ItemDefinition"), TEXT("Sword"));

	// Place 3 swords in separate slots (simulating stacks)
	for (int32 i = 0; i < 3; ++i)
	{
		FItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.ItemDefinitionId = SwordId;
		Item.StackCount = 5;
		InventoryTestHelpers::PlaceItemInSlot(Comp, Item, i);
	}

	TestEqual("Total count = 15", Comp->GetItemCount(SwordId), 15);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryQuery_FindFirstEmpty,
	"ItemInventory.Queries.FindFirstEmptySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryQuery_FindFirstEmpty::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	TestEqual("All empty -> slot 0", Comp->FindFirstEmptySlot(), 0);

	InventoryTestHelpers::PlaceItemInSlot(Comp, InventoryTestHelpers::CreateTestItem(), 0);
	InventoryTestHelpers::PlaceItemInSlot(Comp, InventoryTestHelpers::CreateTestItem(), 1);

	TestEqual("First 2 filled -> slot 2", Comp->FindFirstEmptySlot(), 2);

	// Fill all
	for (int32 i = 2; i < 5; ++i)
	{
		InventoryTestHelpers::PlaceItemInSlot(Comp, InventoryTestHelpers::CreateTestItem(), i);
	}
	TestEqual("All full -> INDEX_NONE", Comp->FindFirstEmptySlot(), INDEX_NONE);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryQuery_SlotStability,
	"ItemInventory.Queries.SlotStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryQuery_SlotStability::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);

	FItemInstance A = InventoryTestHelpers::CreateTestItem(TEXT("A"));
	FItemInstance B = InventoryTestHelpers::CreateTestItem(TEXT("B"));
	FItemInstance C = InventoryTestHelpers::CreateTestItem(TEXT("C"));
	InventoryTestHelpers::PlaceItemInSlot(Comp, A, 0);
	InventoryTestHelpers::PlaceItemInSlot(Comp, B, 1);
	InventoryTestHelpers::PlaceItemInSlot(Comp, C, 2);

	// Remove middle item
	Comp->TryRemoveItem(B.InstanceId);

	// Slot indices should be stable — A stays at 0, C stays at 2, slot 1 is empty
	TestEqual("A still at slot 0", Comp->GetItemInSlot(0).InstanceId, A.InstanceId);
	TestFalse("Slot 1 is empty", Comp->GetItemInSlot(1).IsValid());
	TestEqual("C still at slot 2", Comp->GetItemInSlot(2).InstanceId, C.InstanceId);

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryQuery_DirtyTracking,
	"ItemInventory.Queries.DirtyTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryQuery_DirtyTracking::RunTest(const FString& Parameters)
{
	auto* Comp = InventoryTestHelpers::CreateTestInventory(5);
	TestFalse("Starts clean", Comp->IsDirty());

	FItemInstance Item = InventoryTestHelpers::CreateTestItem();
	Comp->TryAddItem(Item);
	TestTrue("Dirty after add", Comp->IsDirty());

	Comp->ClearDirty();
	TestFalse("Clean after clear", Comp->IsDirty());

	Comp->TryRemoveItem(Item.InstanceId);
	TestTrue("Dirty after remove", Comp->IsDirty());

	InventoryTestHelpers::CleanupInventory(Comp);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
