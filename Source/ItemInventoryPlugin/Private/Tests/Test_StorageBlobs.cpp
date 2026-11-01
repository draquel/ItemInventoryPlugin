// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Storage/LocalItemStorage.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Blob storage (feature 9): the local backend round-trips an opaque document by key.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStorageBlob_LocalRoundTrip, "ItemInventory.Storage.Blob.LocalRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FStorageBlob_LocalRoundTrip::RunTest(const FString& Parameters)
{
	ULocalItemStorage* Storage = NewObject<ULocalItemStorage>();
	Storage->AddToRoot();
	const FString Key = TEXT("Test_Blob_RoundTrip");
	const FString Json = TEXT("{\"version\":1,\"hello\":\"world\"}");

	ICGFItemStorageInterface::Execute_DeleteBlob(Storage, Key);
	TestFalse(TEXT("No blob before save"), ICGFItemStorageInterface::Execute_HasBlob(Storage, Key));
	TestTrue(TEXT("Save"), ICGFItemStorageInterface::Execute_SaveBlob(Storage, Key, Json));
	TestTrue(TEXT("Present after save"), ICGFItemStorageInterface::Execute_HasBlob(Storage, Key));

	FString Loaded;
	TestTrue(TEXT("Load"), ICGFItemStorageInterface::Execute_LoadBlob(Storage, Key, Loaded));
	TestEqual(TEXT("Round trip"), Loaded, Json);

	TestTrue(TEXT("Delete"), ICGFItemStorageInterface::Execute_DeleteBlob(Storage, Key));
	TestFalse(TEXT("Gone"), ICGFItemStorageInterface::Execute_HasBlob(Storage, Key));
	TestFalse(TEXT("Load after delete fails"), ICGFItemStorageInterface::Execute_LoadBlob(Storage, Key, Loaded));

	// Keys are sanitised into file names.
	TestTrue(TEXT("Odd characters become safe"), ULocalItemStorage::GetBlobPath(TEXT("a/b:c")).EndsWith(TEXT("a_b_c.json")));

	Storage->RemoveFromRoot();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
