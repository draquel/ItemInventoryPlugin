#include "Types/ItemInstanceFragments.h"
#include "Dom/JsonObject.h"

// ---------------------------------------------------------------------------
// UInstanceFragment_DurabilityState
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> UInstanceFragment_DurabilityState::SerializeFragment() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("currentDurability"), CurrentDurability);
	return Json;
}

void UInstanceFragment_DurabilityState::DeserializeFragment(const TSharedPtr<FJsonObject>& Data)
{
	if (Data.IsValid())
	{
		CurrentDurability = Data->GetNumberField(TEXT("currentDurability"));
	}
}

FText UInstanceFragment_DurabilityState::GetFragmentDisplayText_Implementation() const
{
	return FText::Format(NSLOCTEXT("ItemFragments", "DurabilityFormat", "Durability: {0}"),
		FText::AsNumber(FMath::RoundToInt(CurrentDurability)));
}

bool UInstanceFragment_DurabilityState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << CurrentDurability;
	bOutSuccess = true;
	return true;
}

// ---------------------------------------------------------------------------
// UInstanceFragment_CustomDataMap
// ---------------------------------------------------------------------------

void UInstanceFragment_CustomDataMap::SetValue(const FString& Key, const FString& Value)
{
	CustomData.Add(Key, Value);
}

FString UInstanceFragment_CustomDataMap::GetValue(const FString& Key, const FString& DefaultValue) const
{
	const FString* Found = CustomData.Find(Key);
	return Found ? *Found : DefaultValue;
}

bool UInstanceFragment_CustomDataMap::HasKey(const FString& Key) const
{
	return CustomData.Contains(Key);
}

TSharedPtr<FJsonObject> UInstanceFragment_CustomDataMap::SerializeFragment() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const auto& Pair : CustomData)
	{
		Json->SetStringField(Pair.Key, Pair.Value);
	}
	return Json;
}

void UInstanceFragment_CustomDataMap::DeserializeFragment(const TSharedPtr<FJsonObject>& Data)
{
	CustomData.Empty();
	if (Data.IsValid())
	{
		for (const auto& Pair : Data->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value))
			{
				CustomData.Add(Pair.Key, Value);
			}
		}
	}
}

FText UInstanceFragment_CustomDataMap::GetFragmentDisplayText_Implementation() const
{
	return FText::Format(NSLOCTEXT("ItemFragments", "CustomDataFormat", "Custom Data: {0} entries"),
		FText::AsNumber(CustomData.Num()));
}

bool UInstanceFragment_CustomDataMap::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	int32 Count = CustomData.Num();
	Ar << Count;

	if (Ar.IsLoading())
	{
		CustomData.Empty(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			FString Key, Value;
			Ar << Key;
			Ar << Value;
			CustomData.Add(Key, Value);
		}
	}
	else
	{
		for (auto& Pair : CustomData)
		{
			Ar << Pair.Key;
			Ar << Pair.Value;
		}
	}

	bOutSuccess = true;
	return true;
}
