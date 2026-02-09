#include "Misc/AutomationTest.h"
#include "Components/InventoryComponent.h"
#include "Types/CGFItemTypes.h"
#include "GameplayTagsManager.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Reuse helper pattern from existing inventory tests
// Root objects to prevent GC collection during test execution.
// ---------------------------------------------------------------------------
namespace FilterTestHelpers
{
	UInventoryComponent* CreateTestInventory(int32 Slots = 10, float MaxWeight = 0.0f)
	{
		UInventoryComponent* Comp = NewObject<UInventoryComponent>();
		Comp->AddToRoot();
		Comp->MaxSlots = Slots;
		Comp->MaxWeight = MaxWeight;

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

	void PlaceItemInSlot(UInventoryComponent* Comp, const FItemInstance& Item, int32 Slot)
	{
		FInventorySlot& S = Comp->InventorySlots.Items[Slot];
		S.Item = Item;
		S.bIsOccupied = true;
	}
}

// ===========================================================================
// Weight Tracking
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_Weight_CurrentWeight,
	"ItemInventory.Filtering.Weight.CurrentWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_Weight_CurrentWeight::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(10, 0.0f);

	TestEqual("Empty inventory weight = 0", Comp->GetCurrentWeight(), 0.0f);

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// Tag Filtering — AllowedItemTags
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_Tags_NoFilter,
	"ItemInventory.Filtering.Tags.NoFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_Tags_NoFilter::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(5);

	FItemInstance Item = FilterTestHelpers::CreateTestItem();
	TestTrue("No filter = can accept", Comp->CanAcceptItem(Item));

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// Capacity Checks
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_Capacity_CanAccept_Full,
	"ItemInventory.Filtering.Capacity.FullInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_Capacity_CanAccept_Full::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(2);

	for (int32 i = 0; i < 2; ++i)
	{
		FilterTestHelpers::PlaceItemInSlot(Comp,
			FilterTestHelpers::CreateTestItem(FString::Printf(TEXT("Item%d"), i)), i);
	}

	FItemInstance NewItem = FilterTestHelpers::CreateTestItem(TEXT("Extra"));
	TestFalse("Full inventory rejects new item", Comp->CanAcceptItem(NewItem));

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_Capacity_CanAccept_HasRoom,
	"ItemInventory.Filtering.Capacity.HasRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_Capacity_CanAccept_HasRoom::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(5);

	FilterTestHelpers::PlaceItemInSlot(Comp, FilterTestHelpers::CreateTestItem(), 0);

	FItemInstance NewItem = FilterTestHelpers::CreateTestItem(TEXT("NewItem"));
	TestTrue("Room available = can accept", Comp->CanAcceptItem(NewItem));

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_Capacity_CanAccept_InvalidItem,
	"ItemInventory.Filtering.Capacity.InvalidItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_Capacity_CanAccept_InvalidItem::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(5);

	FItemInstance Invalid;
	TestFalse("Invalid item rejected", Comp->CanAcceptItem(Invalid));

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// FindItemsByTag (empty container — exercises the method)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_FindByTag_NoMatches,
	"ItemInventory.Filtering.FindItemsByTag.NoMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_FindByTag_NoMatches::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(5);
	FilterTestHelpers::PlaceItemInSlot(Comp, FilterTestHelpers::CreateTestItem(), 0);

	FGameplayTagContainer SearchTags;
	FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(TEXT("Item.Type.Weapon"), false);
	if (TestTag.IsValid())
	{
		SearchTags.AddTag(TestTag);
	}

	TArray<FItemInstance> Results = Comp->FindItemsByTag(SearchTags);
	TestEqual("No tagged matches", Results.Num(), 0);

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

// ===========================================================================
// Multiple operations on same inventory (integration-like)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilter_Integration_AddRemoveReset,
	"ItemInventory.Filtering.Integration.AddRemoveReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFilter_Integration_AddRemoveReset::RunTest(const FString& Parameters)
{
	auto* Comp = FilterTestHelpers::CreateTestInventory(3);

	// Add 3 items
	TArray<FGuid> ItemIds;
	for (int32 i = 0; i < 3; ++i)
	{
		FItemInstance Item = FilterTestHelpers::CreateTestItem(FString::Printf(TEXT("Item%d"), i));
		EInventoryOperationResult Result = Comp->TryAddItem(Item);
		TestEqual(FString::Printf(TEXT("Add %d"), i), Result, EInventoryOperationResult::Success);
		ItemIds.Add(Item.InstanceId);
	}

	TestEqual("3 slots filled", Comp->GetFilledSlotCount(), 3);

	// Remove middle item
	Comp->TryRemoveItem(ItemIds[1]);
	TestEqual("2 slots filled", Comp->GetFilledSlotCount(), 2);
	TestTrue("Can accept new item", Comp->CanAcceptItem(FilterTestHelpers::CreateTestItem()));

	// Fill the gap
	FItemInstance Replacement = FilterTestHelpers::CreateTestItem(TEXT("Replacement"));
	Comp->TryAddItem(Replacement);
	TestEqual("Back to 3", Comp->GetFilledSlotCount(), 3);
	TestFalse("Full again", Comp->CanAcceptItem(FilterTestHelpers::CreateTestItem()));

	FilterTestHelpers::CleanupInventory(Comp);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
