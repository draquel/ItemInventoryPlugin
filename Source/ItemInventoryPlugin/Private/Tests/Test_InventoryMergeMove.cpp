#include "Misc/AutomationTest.h"
#include "Components/InventoryComponent.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Testing/CGFTestGameInstance.h"
#include "Types/CGFItemTypes.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Merge and move successes (code review CR2, D-CR23 / FW-34). The other operation tests run with no world, where the item
// database is out of reach and every stack's maximum is 1, so a merge of two stacks of one definition could only return
// StackFull; here a standalone game instance holds a registered definition with a stack of 20, and the components sit on an
// actor in its world.
// ---------------------------------------------------------------------------
namespace InventoryMergeMoveTestHelpers
{
	/** A component on the owner with Slots pre-allocated slots (BeginPlay has not run: play has not begun). */
	UInventoryComponent* MakeInventory(AActor* Owner, int32 Slots)
	{
		UInventoryComponent* Comp = NewObject<UInventoryComponent>(Owner);
		Comp->MaxSlots = Slots;
		Comp->MaxWeight = 1000.0f;
		Comp->InventorySlots.Items.SetNum(Slots);
		for (int32 i = 0; i < Slots; ++i)
		{
			Comp->InventorySlots.Items[i].SlotIndex = i;
			Comp->InventorySlots.Items[i].bIsOccupied = false;
		}
		Comp->InventorySlots.OwningComponent = Comp;
		return Comp;
	}

	/** Place an item straight into a slot (TryAddItem would stack it onto a stack of the same definition). */
	void PlaceItemInSlot(UInventoryComponent* Comp, const FItemInstance& Item, int32 Slot)
	{
		FInventorySlot& S = Comp->InventorySlots.Items[Slot];
		S.Item = Item;
		S.bIsOccupied = true;
	}

	/** The slot holding an instance, or INDEX_NONE. */
	int32 SlotOf(const UInventoryComponent* Comp, const FGuid& InstanceId)
	{
		for (const FInventorySlot& S : Comp->InventorySlots.Items)
		{
			if (S.bIsOccupied && S.Item.InstanceId == InstanceId)
			{
				return S.SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	/** A registered definition with a stack of StackSize, named after the test. */
	FPrimaryAssetId RegisterDefinition(UItemDatabaseSubsystem* Database, const TCHAR* Name, int32 StackSize)
	{
		UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), Name);
		Definition->AddToRoot();
		Definition->MaxStackSize = StackSize;
		Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);
		return Definition->GetPrimaryAssetId();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMergeSuccessTest,
	"ItemInventory.Operations.Merge.Success",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMergeSuccessTest::RunTest(const FString& Parameters)
{
	using namespace InventoryMergeMoveTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	const FPrimaryAssetId Stackable = RegisterDefinition(Database, TEXT("MergeTestStackable"), 20);
	AActor* Owner = Game.SpawnOwner();
	UInventoryComponent* Inventory = MakeInventory(Owner, 10);

	// Two stacks of the same definition, 5 and 7: the merge leaves one stack of 12.
	const FItemInstance Source = Database->CreateItemInstance(Stackable, 5);
	const FItemInstance Target = Database->CreateItemInstance(Stackable, 7);
	PlaceItemInSlot(Inventory, Source, 0);
	PlaceItemInSlot(Inventory, Target, 3);
	const EInventoryOperationResult Merged = Inventory->TryMergeStacks(Source.InstanceId, Target.InstanceId);
	TestEqual(TEXT("Merging 5 onto 7 with a stack of 20 succeeds"), static_cast<int32>(Merged), static_cast<int32>(EInventoryOperationResult::Success));
	TestEqual(TEXT("The target holds 12"), Inventory->GetItemInSlot(3).StackCount, 12);
	TestEqual(TEXT("The source stack is gone"), SlotOf(Inventory, Source.InstanceId), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("The definition's count is 12"), Inventory->GetItemCount(Stackable), 12);

	// Over the stack: 15 onto 12 does not fit a stack of 20.
	const FItemInstance Big = Database->CreateItemInstance(Stackable, 15);
	PlaceItemInSlot(Inventory, Big, 5);
	const EInventoryOperationResult Full = Inventory->TryMergeStacks(Big.InstanceId, Target.InstanceId);
	TestEqual(TEXT("Merging past the stack's maximum is StackFull"), static_cast<int32>(Full), static_cast<int32>(EInventoryOperationResult::StackFull));
	TestEqual(TEXT("Both stacks keep their counts"), Inventory->GetItemInSlot(5).StackCount + Inventory->GetItemInSlot(3).StackCount, 27);

	if (UItemDefinition* Definition = Database->GetDefinition(Stackable))
	{
		Definition->RemoveFromRoot();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMoveAcrossTest,
	"ItemInventory.Operations.Move.AcrossInventories",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMoveAcrossTest::RunTest(const FString& Parameters)
{
	using namespace InventoryMergeMoveTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	const FPrimaryAssetId Stackable = RegisterDefinition(Database, TEXT("MoveTestStackable"), 20);
	AActor* Owner = Game.SpawnOwner();
	UInventoryComponent* Bag = MakeInventory(Owner, 10);
	UInventoryComponent* Chest = MakeInventory(Owner, 10);

	// A stack moves whole from the bag to an empty chest.
	const FItemInstance Stack = Database->CreateItemInstance(Stackable, 6);
	PlaceItemInSlot(Bag, Stack, 2);
	const EInventoryOperationResult Moved = Bag->TryMoveItem(Stack.InstanceId, Chest);
	TestEqual(TEXT("Moving to another inventory succeeds"), static_cast<int32>(Moved), static_cast<int32>(EInventoryOperationResult::Success));
	TestEqual(TEXT("The bag no longer holds it"), SlotOf(Bag, Stack.InstanceId), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("The bag's count is 0"), Bag->GetItemCount(Stackable), 0);
	TestEqual(TEXT("The chest's count is 6"), Chest->GetItemCount(Stackable), 6);

	// A second stack of the same definition moving in stacks onto the first (the target adds stacked).
	const FItemInstance More = Database->CreateItemInstance(Stackable, 4);
	PlaceItemInSlot(Bag, More, 0);
	const EInventoryOperationResult MovedMore = Bag->TryMoveItem(More.InstanceId, Chest);
	TestEqual(TEXT("Moving a second stack succeeds"), static_cast<int32>(MovedMore), static_cast<int32>(EInventoryOperationResult::Success));
	TestEqual(TEXT("The chest's count is 10"), Chest->GetItemCount(Stackable), 10);
	int32 ChestStacks = 0;
	for (const FInventorySlot& S : Chest->InventorySlots.Items)
	{
		ChestStacks += S.bIsOccupied ? 1 : 0;
	}
	TestEqual(TEXT("The chest holds one stack of 10"), ChestStacks, 1);

	// An unknown instance and a missing target.
	TestEqual(TEXT("Moving an instance the bag does not hold is ItemNotFound"), static_cast<int32>(Bag->TryMoveItem(FGuid::NewGuid(), Chest)),
		static_cast<int32>(EInventoryOperationResult::ItemNotFound));
	PlaceItemInSlot(Bag, Database->CreateItemInstance(Stackable, 1), 4);
	TestEqual(TEXT("Moving to no inventory is Failed"), static_cast<int32>(Bag->TryMoveItem(Bag->GetItemInSlot(4).InstanceId, nullptr)),
		static_cast<int32>(EInventoryOperationResult::Failed));

	if (UItemDefinition* Definition = Database->GetDefinition(Stackable))
	{
		Definition->RemoveFromRoot();
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
