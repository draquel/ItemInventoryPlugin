#include "Subsystems/ItemDatabaseSubsystem.h"
#include "ItemInventoryPlugin.h"
#include "Data/ItemDefinition.h"
#include "Components/InventoryComponent.h"
#include "Engine/AssetManager.h"

void UItemDatabaseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ScanForDefinitions();
}

void UItemDatabaseSubsystem::ScanForDefinitions()
{
	UAssetManager& AssetManager = UAssetManager::Get();

	const FPrimaryAssetType ItemType(TEXT("ItemDefinition"));

	TArray<FPrimaryAssetId> AssetIds;
	AssetManager.GetPrimaryAssetIdList(ItemType, AssetIds);

	for (const FPrimaryAssetId& AssetId : AssetIds)
	{
		FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		if (AssetPath.IsValid())
		{
			UItemDefinition* Definition = Cast<UItemDefinition>(AssetPath.TryLoad());
			if (Definition)
			{
				DefinitionCache.Add(AssetId, Definition);
			}
		}
	}

	UE_LOG(LogIIP, Log, TEXT("ItemDatabaseSubsystem: Scanned %d item definitions"), DefinitionCache.Num());
}

UItemDefinition* UItemDatabaseSubsystem::GetDefinition(FPrimaryAssetId AssetId) const
{
	const TObjectPtr<UItemDefinition>* Found = DefinitionCache.Find(AssetId);
	return Found ? *Found : nullptr;
}

TArray<FPrimaryAssetId> UItemDatabaseSubsystem::GetAllDefinitionIds() const
{
	TArray<FPrimaryAssetId> Result;
	DefinitionCache.GetKeys(Result);
	return Result;
}

bool UItemDatabaseSubsystem::HasDefinition(FPrimaryAssetId AssetId) const
{
	return DefinitionCache.Contains(AssetId);
}

void UItemDatabaseSubsystem::RegisterDefinition(FPrimaryAssetId AssetId, UItemDefinition* Definition)
{
	if (!AssetId.IsValid())
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemDatabaseSubsystem::RegisterDefinition: Invalid AssetId"));
		return;
	}
	if (!Definition)
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemDatabaseSubsystem::RegisterDefinition: Null Definition for %s"), *AssetId.ToString());
		return;
	}
	DefinitionCache.Add(AssetId, Definition);
	UE_LOG(LogIIP, Log, TEXT("ItemDatabaseSubsystem: Registered definition %s"), *AssetId.ToString());
}

FItemInstance UItemDatabaseSubsystem::CreateItemInstance(FPrimaryAssetId AssetId, int32 StackCount)
{
	FItemInstance Instance;

	UItemDefinition* Definition = GetDefinition(AssetId);
	if (!Definition)
	{
		UE_LOG(LogIIP, Warning, TEXT("ItemDatabaseSubsystem::CreateItemInstance: Definition not found for %s"), *AssetId.ToString());
		return Instance;
	}

	Instance.InstanceId = FGuid::NewGuid();
	Instance.ItemDefinitionId = AssetId;
	Instance.StackCount = FMath::Max(1, StackCount);

	// Copy default instance fragments from the definition
	for (UItemInstanceFragment* DefaultFragment : Definition->DefaultInstanceFragments)
	{
		if (DefaultFragment)
		{
			UItemInstanceFragment* FragCopy = DuplicateObject<UItemInstanceFragment>(DefaultFragment, GetTransientPackage());
			if (FragCopy)
			{
				FragCopy->OnAttachedToInstance(Instance);
				Instance.InstanceFragments.Add(FragCopy);
			}
		}
	}

	return Instance;
}

EInventoryOperationResult UItemDatabaseSubsystem::GiveItemByName(UInventoryComponent* Inventory, const FString& Name, int32 Count, bool bMatchSubstring, FPrimaryAssetId& OutDefinition)
{
	OutDefinition = FPrimaryAssetId();
	if (Count <= 0)
	{
		Count = 1;
	}
	if (bMatchSubstring)
	{
		// The first registered definition whose name contains the text.
		for (const FPrimaryAssetId& Id : GetAllDefinitionIds())
		{
			if (Id.PrimaryAssetName.ToString().Contains(Name))
			{
				OutDefinition = Id;
				break;
			}
		}
		if (!OutDefinition.IsValid())
		{
			return EInventoryOperationResult::ItemNotFound;
		}
	}
	else
	{
		OutDefinition = FPrimaryAssetId(FPrimaryAssetType("ItemDefinition"), FName(*Name));
	}

	const FItemInstance Instance = CreateItemInstance(OutDefinition, Count);
	if (!Instance.IsValid())
	{
		return EInventoryOperationResult::InvalidItem;
	}
	if (!Inventory)
	{
		return EInventoryOperationResult::Failed;
	}
	return Inventory->TryAddItem(Instance);
}
