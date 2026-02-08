#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
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

	UE_LOG(LogTemp, Log, TEXT("ItemDatabaseSubsystem: Scanned %d item definitions"), DefinitionCache.Num());
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

void UItemDatabaseSubsystem::LoadDefinitionAsync(FPrimaryAssetId AssetId, FStreamableDelegate OnLoaded)
{
	UAssetManager& AssetManager = UAssetManager::Get();

	AssetManager.LoadPrimaryAsset(AssetId, TArray<FName>(),
		FStreamableDelegate::CreateLambda([this, AssetId, OnLoaded]()
		{
			UAssetManager& AM = UAssetManager::Get();
			UObject* LoadedAsset = AM.GetPrimaryAssetObject(AssetId);
			UItemDefinition* Definition = Cast<UItemDefinition>(LoadedAsset);
			if (Definition)
			{
				DefinitionCache.Add(AssetId, Definition);
			}
			OnLoaded.ExecuteIfBound();
		})
	);
}

FItemInstance UItemDatabaseSubsystem::CreateItemInstance(FPrimaryAssetId AssetId, int32 StackCount)
{
	FItemInstance Instance;

	UItemDefinition* Definition = GetDefinition(AssetId);
	if (!Definition)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemDatabaseSubsystem::CreateItemInstance: Definition not found for %s"), *AssetId.ToString());
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
