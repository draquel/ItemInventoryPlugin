#include "Storage/ItemSerializationUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

static const int32 SERIALIZATION_VERSION = 1;

// ---------------------------------------------------------------------------
// Full Inventory
// ---------------------------------------------------------------------------

FString FItemSerializationUtils::SerializeInventory(const FString& OwnerId, const TArray<FItemInstance>& Items)
{
	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetNumberField(TEXT("version"), SERIALIZATION_VERSION);
	RootObj->SetStringField(TEXT("ownerId"), OwnerId);

	TArray<TSharedPtr<FJsonValue>> SlotsArray;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FItemInstance& Item = Items[i];
		if (!Item.IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetNumberField(TEXT("slotIndex"), i);
		SlotObj->SetBoolField(TEXT("bIsOccupied"), true);
		SlotObj->SetObjectField(TEXT("item"), SerializeItemInstance(Item));
		SlotsArray.Add(MakeShared<FJsonValueObject>(SlotObj));
	}

	RootObj->SetArrayField(TEXT("slots"), SlotsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);
	return OutputString;
}

bool FItemSerializationUtils::DeserializeInventory(const FString& JsonString, FString& OutOwnerId,
	TArray<FItemInstance>& OutItems, UObject* Outer)
{
	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemSerializationUtils: Failed to parse JSON"));
		return false;
	}

	int32 Version = 0;
	RootObj->TryGetNumberField(TEXT("version"), Version);
	if (Version != SERIALIZATION_VERSION)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemSerializationUtils: Unsupported version %d (expected %d)"),
			Version, SERIALIZATION_VERSION);
		return false;
	}

	RootObj->TryGetStringField(TEXT("ownerId"), OutOwnerId);

	OutItems.Empty();
	const TArray<TSharedPtr<FJsonValue>>* SlotsArray = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("slots"), SlotsArray) || !SlotsArray)
	{
		return true; // Valid but empty inventory
	}

	for (const TSharedPtr<FJsonValue>& SlotValue : *SlotsArray)
	{
		const TSharedPtr<FJsonObject>* SlotObjPtr = nullptr;
		if (!SlotValue->TryGetObject(SlotObjPtr) || !SlotObjPtr || !(*SlotObjPtr).IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>& SlotObj = *SlotObjPtr;

		bool bIsOccupied = false;
		SlotObj->TryGetBoolField(TEXT("bIsOccupied"), bIsOccupied);
		if (!bIsOccupied)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* ItemObjPtr = nullptr;
		if (!SlotObj->TryGetObjectField(TEXT("item"), ItemObjPtr) || !ItemObjPtr || !(*ItemObjPtr).IsValid())
		{
			continue;
		}

		FItemInstance Item = DeserializeItemInstance(*ItemObjPtr, Outer);
		if (Item.IsValid())
		{
			OutItems.Add(MoveTemp(Item));
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Single Item Instance
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FItemSerializationUtils::SerializeItemInstance(const FItemInstance& Item)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("instanceId"), Item.InstanceId.ToString());
	Obj->SetStringField(TEXT("itemDefinitionId"), Item.ItemDefinitionId.ToString());
	Obj->SetNumberField(TEXT("stackCount"), Item.StackCount);

	TArray<TSharedPtr<FJsonValue>> FragArray;
	for (const UItemInstanceFragment* Fragment : Item.InstanceFragments)
	{
		TSharedPtr<FJsonObject> FragObj = SerializeFragment(Fragment);
		if (FragObj.IsValid())
		{
			FragArray.Add(MakeShared<FJsonValueObject>(FragObj));
		}
	}
	Obj->SetArrayField(TEXT("instanceFragments"), FragArray);

	return Obj;
}

FItemInstance FItemSerializationUtils::DeserializeItemInstance(const TSharedPtr<FJsonObject>& JsonObj, UObject* Outer)
{
	FItemInstance Item;
	if (!JsonObj.IsValid())
	{
		return Item;
	}

	FString InstanceIdStr;
	if (JsonObj->TryGetStringField(TEXT("instanceId"), InstanceIdStr))
	{
		FGuid::Parse(InstanceIdStr, Item.InstanceId);
	}

	FString DefIdStr;
	if (JsonObj->TryGetStringField(TEXT("itemDefinitionId"), DefIdStr))
	{
		Item.ItemDefinitionId = FPrimaryAssetId(DefIdStr);
	}

	int32 StackCount = 1;
	if (JsonObj->TryGetNumberField(TEXT("stackCount"), StackCount))
	{
		Item.StackCount = StackCount;
	}

	const TArray<TSharedPtr<FJsonValue>>* FragArray = nullptr;
	if (JsonObj->TryGetArrayField(TEXT("instanceFragments"), FragArray) && FragArray)
	{
		for (const TSharedPtr<FJsonValue>& FragValue : *FragArray)
		{
			const TSharedPtr<FJsonObject>* FragObjPtr = nullptr;
			if (FragValue->TryGetObject(FragObjPtr) && FragObjPtr && (*FragObjPtr).IsValid())
			{
				UItemInstanceFragment* Fragment = DeserializeFragment(*FragObjPtr, Outer);
				if (Fragment)
				{
					Item.InstanceFragments.Add(Fragment);
				}
			}
		}
	}

	return Item;
}

// ---------------------------------------------------------------------------
// Single Fragment
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FItemSerializationUtils::SerializeFragment(const UItemInstanceFragment* Fragment)
{
	if (!Fragment)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("fragmentClass"), Fragment->GetClass()->GetPathName());

	TSharedPtr<FJsonObject> Data = Fragment->SerializeFragment();
	if (Data.IsValid())
	{
		Obj->SetObjectField(TEXT("data"), Data);
	}

	return Obj;
}

UItemInstanceFragment* FItemSerializationUtils::DeserializeFragment(const TSharedPtr<FJsonObject>& JsonObj, UObject* Outer)
{
	if (!JsonObj.IsValid())
	{
		return nullptr;
	}

	FString ClassPath;
	if (!JsonObj->TryGetStringField(TEXT("fragmentClass"), ClassPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemSerializationUtils: Fragment missing fragmentClass"));
		return nullptr;
	}

	UClass* FragmentClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!FragmentClass)
	{
		// Try loading the class if not already in memory
		FragmentClass = LoadObject<UClass>(nullptr, *ClassPath);
	}
	if (!FragmentClass || !FragmentClass->IsChildOf(UItemInstanceFragment::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemSerializationUtils: Could not find fragment class '%s'"), *ClassPath);
		return nullptr;
	}

	UItemInstanceFragment* Fragment = NewObject<UItemInstanceFragment>(
		Outer ? Outer : GetTransientPackage(), FragmentClass);
	if (!Fragment)
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* DataObjPtr = nullptr;
	if (JsonObj->TryGetObjectField(TEXT("data"), DataObjPtr) && DataObjPtr && (*DataObjPtr).IsValid())
	{
		Fragment->DeserializeFragment(*DataObjPtr);
	}

	return Fragment;
}
