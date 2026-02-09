#include "Misc/AutomationTest.h"
#include "Storage/ItemSerializationUtils.h"
#include "Types/CGFItemTypes.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace SerializationTestHelpers
{
	FItemInstance CreateTestItem(const FString& DefName = TEXT("TestItem"), int32 Stack = 1)
	{
		FItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), FName(*DefName));
		Item.StackCount = Stack;
		return Item;
	}
}

// ===========================================================================
// Single Item Roundtrip
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_ItemRoundtrip,
	"ItemInventory.Serialization.ItemInstance.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_ItemRoundtrip::RunTest(const FString& Parameters)
{
	FItemInstance Original = SerializationTestHelpers::CreateTestItem(TEXT("Sword_Iron"), 5);

	TSharedPtr<FJsonObject> Json = FItemSerializationUtils::SerializeItemInstance(Original);
	TestTrue("Serialized to valid JSON", Json.IsValid());

	FItemInstance Restored = FItemSerializationUtils::DeserializeItemInstance(Json, GetTransientPackage());
	TestTrue("Restored item is valid", Restored.IsValid());
	TestEqual("InstanceId preserved", Restored.InstanceId, Original.InstanceId);
	TestEqual("DefinitionId preserved", Restored.ItemDefinitionId, Original.ItemDefinitionId);
	TestEqual("StackCount preserved", Restored.StackCount, Original.StackCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_ItemInvalidJson,
	"ItemInventory.Serialization.ItemInstance.InvalidJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_ItemInvalidJson::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> NullJson;
	FItemInstance Result = FItemSerializationUtils::DeserializeItemInstance(NullJson, GetTransientPackage());
	TestFalse("Null JSON returns invalid item", Result.IsValid());

	TSharedPtr<FJsonObject> EmptyJson = MakeShared<FJsonObject>();
	FItemInstance Result2 = FItemSerializationUtils::DeserializeItemInstance(EmptyJson, GetTransientPackage());
	TestFalse("Empty JSON returns invalid item", Result2.IsValid());

	return true;
}

// ===========================================================================
// Inventory Roundtrip
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_InventoryRoundtrip,
	"ItemInventory.Serialization.Inventory.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_InventoryRoundtrip::RunTest(const FString& Parameters)
{
	const FString OwnerId = TEXT("Player_001");
	TArray<FItemInstance> OriginalItems;
	OriginalItems.Add(SerializationTestHelpers::CreateTestItem(TEXT("Sword"), 1));
	OriginalItems.Add(SerializationTestHelpers::CreateTestItem(TEXT("Potion"), 10));
	OriginalItems.Add(SerializationTestHelpers::CreateTestItem(TEXT("Shield"), 1));

	FString JsonString = FItemSerializationUtils::SerializeInventory(OwnerId, OriginalItems);
	TestFalse("JSON string not empty", JsonString.IsEmpty());

	FString RestoredOwner;
	TArray<FItemInstance> RestoredItems;
	bool bSuccess = FItemSerializationUtils::DeserializeInventory(JsonString, RestoredOwner, RestoredItems, GetTransientPackage());

	TestTrue("Deserialization succeeds", bSuccess);
	TestEqual("OwnerId preserved", RestoredOwner, OwnerId);
	TestEqual("Item count preserved", RestoredItems.Num(), 3);

	// Verify each item
	for (int32 i = 0; i < FMath::Min(OriginalItems.Num(), RestoredItems.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Item %d InstanceId"), i),
			RestoredItems[i].InstanceId, OriginalItems[i].InstanceId);
		TestEqual(FString::Printf(TEXT("Item %d DefinitionId"), i),
			RestoredItems[i].ItemDefinitionId, OriginalItems[i].ItemDefinitionId);
		TestEqual(FString::Printf(TEXT("Item %d StackCount"), i),
			RestoredItems[i].StackCount, OriginalItems[i].StackCount);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_EmptyInventory,
	"ItemInventory.Serialization.Inventory.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_EmptyInventory::RunTest(const FString& Parameters)
{
	TArray<FItemInstance> EmptyItems;
	FString JsonString = FItemSerializationUtils::SerializeInventory(TEXT("Empty"), EmptyItems);

	FString RestoredOwner;
	TArray<FItemInstance> RestoredItems;
	bool bSuccess = FItemSerializationUtils::DeserializeInventory(JsonString, RestoredOwner, RestoredItems, GetTransientPackage());

	TestTrue("Deserialization succeeds", bSuccess);
	TestEqual("OwnerId preserved", RestoredOwner, FString(TEXT("Empty")));
	TestEqual("No items", RestoredItems.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_InvalidJsonString,
	"ItemInventory.Serialization.Inventory.InvalidJsonString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_InvalidJsonString::RunTest(const FString& Parameters)
{
	FString RestoredOwner;
	TArray<FItemInstance> RestoredItems;

	bool bResult = FItemSerializationUtils::DeserializeInventory(TEXT("not json at all"), RestoredOwner, RestoredItems, GetTransientPackage());
	TestFalse("Invalid JSON fails", bResult);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_VersionMismatch,
	"ItemInventory.Serialization.Inventory.VersionMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_VersionMismatch::RunTest(const FString& Parameters)
{
	// Manually craft JSON with wrong version
	FString BadVersionJson = TEXT("{\"version\":999,\"ownerId\":\"test\",\"slots\":[]}");

	FString RestoredOwner;
	TArray<FItemInstance> RestoredItems;
	bool bResult = FItemSerializationUtils::DeserializeInventory(BadVersionJson, RestoredOwner, RestoredItems, GetTransientPackage());
	TestFalse("Version mismatch fails", bResult);

	return true;
}

// ===========================================================================
// Inventory with invalid items (should be skipped)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSerialize_SkipsInvalidItems,
	"ItemInventory.Serialization.Inventory.SkipsInvalidItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSerialize_SkipsInvalidItems::RunTest(const FString& Parameters)
{
	TArray<FItemInstance> Items;
	Items.Add(SerializationTestHelpers::CreateTestItem(TEXT("ValidItem"), 1));

	// Add an invalid item (no GUID)
	FItemInstance InvalidItem;
	InvalidItem.StackCount = 1;
	Items.Add(InvalidItem);

	Items.Add(SerializationTestHelpers::CreateTestItem(TEXT("AnotherValid"), 3));

	FString JsonString = FItemSerializationUtils::SerializeInventory(TEXT("Owner"), Items);

	FString RestoredOwner;
	TArray<FItemInstance> RestoredItems;
	bool bSuccess = FItemSerializationUtils::DeserializeInventory(JsonString, RestoredOwner, RestoredItems, GetTransientPackage());

	TestTrue("Deserialization succeeds", bSuccess);
	TestEqual("Only valid items restored", RestoredItems.Num(), 2);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
