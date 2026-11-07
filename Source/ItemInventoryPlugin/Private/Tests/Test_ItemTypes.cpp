#include "Misc/AutomationTest.h"
#include "Types/CGFItemTypes.h"
#include "Types/CGFLootTypes.h"
#include "Types/ItemInstanceFragments.h"
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
// FItemInstance::Identical: what replication compares (G135)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_Identical,
	"ItemInventory.Types.ItemInstance.Identical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_Identical::RunTest(const FString& Parameters)
{
	// The reflection system's comparison (UScriptStruct::CompareScriptStruct), the one replication runs through Identical.
	const UScriptStruct* Struct = FItemInstance::StaticStruct();
	const auto IsIdentical = [Struct](const FItemInstance& A, const FItemInstance& B)
	{
		return Struct->CompareScriptStruct(&A, &B, PPF_None);
	};

	FItemInstance Arrows;
	Arrows.InstanceId = FGuid::NewGuid();
	Arrows.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Arrow"));
	Arrows.StackCount = 20;

	TestTrue("A copy is identical", IsIdentical(Arrows, FItemInstance(Arrows)));

	// One arrow shot from the stack: the same instance, but not the same value.
	FItemInstance Shot = Arrows;
	Shot.StackCount = 19;
	TestTrue("A count changed in place keeps the instance (operator== is identity)", Arrows == Shot);
	TestFalse("A count changed in place is not identical", IsIdentical(Arrows, Shot));

	FItemInstance OtherDefinition = Arrows;
	OtherDefinition.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Bolt"));
	TestFalse("Another definition is not identical", IsIdentical(Arrows, OtherDefinition));

	FItemInstance OtherId = Arrows;
	OtherId.InstanceId = FGuid::NewGuid();
	TestFalse("Another instance id is not identical", IsIdentical(Arrows, OtherId));

	// Fragments compare as objects: the same ones are identical, one added or replaced is not.
	FItemInstance Worn = Arrows;
	Worn.InstanceFragments.Add(NewObject<UInstanceFragment_DurabilityState>());
	TestTrue("The same fragment objects are identical", IsIdentical(Worn, FItemInstance(Worn)));
	TestFalse("A fragment added is not identical", IsIdentical(Arrows, Worn));
	FItemInstance Replaced = Worn;
	Replaced.InstanceFragments[0] = NewObject<UInstanceFragment_DurabilityState>();
	TestFalse("A fragment replaced is not identical", IsIdentical(Worn, Replaced));

	// A fragment's contents changed in place: a copy made before shares the fragment object, so it reads the new value
	// too, and only the revision EditFragment bumps tells the two apart (G136).
	const FItemInstance BeforeWear = Worn;
	Worn.EditFragment<UInstanceFragment_DurabilityState>()->CurrentDurability -= 5.f;
	TestEqual("A copy shares its fragment objects", BeforeWear.FindFragment<UInstanceFragment_DurabilityState>()->CurrentDurability, 95.f);
	TestFalse("A fragment edited in place is not identical", IsIdentical(BeforeWear, Worn));
	TestTrue("A copy after the edit is identical", IsIdentical(Worn, FItemInstance(Worn)));
	const FItemInstance NoFragment = Arrows;
	TestNull("EditFragment of a class the item lacks is null", Arrows.EditFragment<UInstanceFragment_CustomDataMap>());
	TestTrue("and marks nothing", IsIdentical(NoFragment, Arrows));

	TestFalse("Null is no match", Arrows.Identical(nullptr, PPF_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_SlotItemCountChange,
	"ItemInventory.Types.InventorySlot.ItemCountChangeIsAChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_SlotItemCountChange::RunTest(const FString& Parameters)
{
	// The fast array's delta serialization compares a dirty slot with the state it last sent property by property,
	// FInventorySlot::Item through FStructProperty::Identical: a slot whose item kept its id but not its count must differ
	// there, or the count never reaches the client (G135).
	const FProperty* ItemProperty = FInventorySlot::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FInventorySlot, Item));
	if (!TestNotNull("FInventorySlot::Item is a property", ItemProperty))
	{
		return false;
	}

	FInventorySlot Sent;
	Sent.SlotIndex = 3;
	Sent.bIsOccupied = true;
	Sent.Item.InstanceId = FGuid::NewGuid();
	Sent.Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Arrow"));
	Sent.Item.StackCount = 20;

	FInventorySlot Now = Sent;
	TestTrue("An unchanged slot's item is identical", ItemProperty->Identical_InContainer(&Sent, &Now));
	TestTrue("An unchanged slot is identical", FInventorySlot::StaticStruct()->CompareScriptStruct(&Sent, &Now, PPF_None));

	Now.Item.StackCount = 24;
	TestFalse("The item's count changed in place: the item property differs", ItemProperty->Identical_InContainer(&Sent, &Now));
	TestFalse("The item's count changed in place: the slot differs", FInventorySlot::StaticStruct()->CompareScriptStruct(&Sent, &Now, PPF_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemType_SlotFragmentEdit,
	"ItemInventory.Types.InventorySlot.FragmentEditIsAChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FItemType_SlotFragmentEdit::RunTest(const FString& Parameters)
{
	// The state the fast array last sent is a shallow copy of the slot: it shares the item's fragment objects, so a
	// fragment's contents changed in place are the same on both sides of the comparison. Only the revision EditFragment
	// bumps, copied by value, makes the slot differ there (G136).
	const FProperty* ItemProperty = FInventorySlot::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FInventorySlot, Item));
	if (!TestNotNull("FInventorySlot::Item is a property", ItemProperty))
	{
		return false;
	}

	FInventorySlot Now;
	Now.SlotIndex = 2;
	Now.bIsOccupied = true;
	Now.Item.InstanceId = FGuid::NewGuid();
	Now.Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("Sword"));
	Now.Item.StackCount = 1;
	UInstanceFragment_DurabilityState* Durability = NewObject<UInstanceFragment_DurabilityState>();
	Now.Item.InstanceFragments.Add(Durability);
	const FInventorySlot Sent = Now;

	// Changed behind the item's back (a pointer kept from before): the same objects, the same revision, no difference.
	Durability->CurrentDurability = 80.f;
	TestTrue("A fragment changed without EditFragment is invisible to the comparison", ItemProperty->Identical_InContainer(&Sent, &Now));

	Now.Item.EditFragment<UInstanceFragment_DurabilityState>()->CurrentDurability = 60.f;
	TestFalse("A fragment edited through the item: the item property differs", ItemProperty->Identical_InContainer(&Sent, &Now));
	TestFalse("A fragment edited through the item: the slot differs", FInventorySlot::StaticStruct()->CompareScriptStruct(&Sent, &Now, PPF_None));
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
