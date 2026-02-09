#include "Misc/AutomationTest.h"
#include "Types/CGFItemTypes.h"
#include "Types/CGFLootTypes.h"
#include "Data/LootEntry.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
// FItemInstance::IsValid()
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_IsValid_Complete,
	"ItemInventory.Types.ItemInstance.IsValid.Complete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_IsValid_Complete::RunTest(const FString& Parameters)
{
	FItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Sword"));
	Item.StackCount = 1;

	TestTrue("Complete item is valid", Item.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_IsValid_NoGuid,
	"ItemInventory.Types.ItemInstance.IsValid.NoGuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_IsValid_NoGuid::RunTest(const FString& Parameters)
{
	FItemInstance Item;
	Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Sword"));
	Item.StackCount = 1;

	TestFalse("No GUID = invalid", Item.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_IsValid_NoDefId,
	"ItemInventory.Types.ItemInstance.IsValid.NoDefinitionId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_IsValid_NoDefId::RunTest(const FString& Parameters)
{
	FItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.StackCount = 1;

	TestFalse("No definition ID = invalid", Item.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_IsValid_ZeroStack,
	"ItemInventory.Types.ItemInstance.IsValid.ZeroStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_IsValid_ZeroStack::RunTest(const FString& Parameters)
{
	FItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Sword"));
	Item.StackCount = 0;

	TestFalse("Zero stack = invalid", Item.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_IsValid_Default,
	"ItemInventory.Types.ItemInstance.IsValid.Default",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_IsValid_Default::RunTest(const FString& Parameters)
{
	FItemInstance Item;
	TestFalse("Default-constructed item is invalid", Item.IsValid());
	return true;
}

// ===========================================================================
// FItemInstance equality
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_Equality,
	"ItemInventory.Types.ItemInstance.Equality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_Equality::RunTest(const FString& Parameters)
{
	FItemInstance A;
	A.InstanceId = FGuid::NewGuid();
	A.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Sword"));
	A.StackCount = 1;

	FItemInstance B = A;
	TestTrue("Copy equals original", A == B);

	FItemInstance C;
	C.InstanceId = FGuid::NewGuid();
	C.ItemDefinitionId = A.ItemDefinitionId;
	C.StackCount = A.StackCount;
	TestTrue("Different GUID = not equal", A != C);

	return true;
}

// ===========================================================================
// FLootEntry
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootType_EntryValid_Item,
	"ItemInventory.Types.LootEntry.IsValid.WithItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLootType_EntryValid_Item::RunTest(const FString& Parameters)
{
	FLootEntry Entry;
	Entry.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Sword"));

	TestTrue("Entry with item ID is valid", Entry.IsValid());
	TestFalse("Not a nested table", Entry.IsNestedTable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootType_EntryValid_Empty,
	"ItemInventory.Types.LootEntry.IsValid.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLootType_EntryValid_Empty::RunTest(const FString& Parameters)
{
	FLootEntry Entry;
	TestFalse("Default entry is invalid", Entry.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootType_EntryDefaults,
	"ItemInventory.Types.LootEntry.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLootType_EntryDefaults::RunTest(const FString& Parameters)
{
	FLootEntry Entry;
	TestEqual("Default weight = 1.0", Entry.Weight, 1.0f);
	TestEqual("Default drop chance = 1.0", Entry.DropChance, 1.0f);
	TestEqual("Default min quantity = 1", Entry.MinQuantity, 1);
	TestEqual("Default max quantity = 1", Entry.MaxQuantity, 1);
	TestEqual("Default min level = 0", Entry.MinLevel, 0);
	TestEqual("Default max level = 0", Entry.MaxLevel, 0);
	TestTrue("No required tags", Entry.RequiredContextTags.IsEmpty());
	TestTrue("No excluded tags", Entry.ExcludedContextTags.IsEmpty());
	return true;
}

// ===========================================================================
// FLootContext
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootType_ContextDefaults,
	"ItemInventory.Types.LootContext.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLootType_ContextDefaults::RunTest(const FString& Parameters)
{
	FLootContext Context;
	TestEqual("Default level = 1", Context.Level, 1);
	TestEqual("Default party size = 1", Context.PartySize, 1);
	TestEqual("Default luck modifier = 1.0", Context.LuckModifier, 1.0f);
	TestTrue("No context tags", Context.ContextTags.IsEmpty());
	return true;
}

// ===========================================================================
// FInventorySlot
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_SlotDefaults,
	"ItemInventory.Types.InventorySlot.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_SlotDefaults::RunTest(const FString& Parameters)
{
	FInventorySlot Slot;
	TestEqual("Default slot index = -1", Slot.SlotIndex, -1);
	TestFalse("Default not occupied", Slot.bIsOccupied);
	TestFalse("Default item invalid", Slot.Item.IsValid());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
