#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/AssetManager.h"
#include "Types/CGFItemTypes.h"
#include "ItemDatabaseSubsystem.generated.h"

class UItemDefinition;

/**
 * Game instance subsystem that manages item definitions.
 * Scans the asset registry for all UItemDefinition assets,
 * provides lookup by FPrimaryAssetId, and is the ONLY way
 * to create runtime FItemInstance structs.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API UItemDatabaseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- USubsystem ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// --- Definition Lookup ---

	/** Get a cached definition by asset ID. Returns nullptr if not found/loaded. */
	UFUNCTION(BlueprintCallable, Category = "Item Database")
	UItemDefinition* GetDefinition(FPrimaryAssetId AssetId) const;

	/** Get all registered definition asset IDs */
	UFUNCTION(BlueprintCallable, Category = "Item Database")
	TArray<FPrimaryAssetId> GetAllDefinitionIds() const;

	/** Check if a definition exists in the registry */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item Database")
	bool HasDefinition(FPrimaryAssetId AssetId) const;

	/** Async load a definition that may not be in memory yet */
	void LoadDefinitionAsync(FPrimaryAssetId AssetId, FStreamableDelegate OnLoaded);

	// --- Instance Creation ---

	/**
	 * Create a new FItemInstance from a definition. SERVER ONLY.
	 * Generates a new GUID, copies DefaultInstanceFragments from the definition.
	 * Returns invalid instance if definition not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Database")
	FItemInstance CreateItemInstance(FPrimaryAssetId AssetId, int32 StackCount = 1);

private:
	/** Scan asset registry and cache definitions */
	void ScanForDefinitions();

	/** Cached definitions keyed by PrimaryAssetId */
	UPROPERTY()
	TMap<FPrimaryAssetId, TObjectPtr<UItemDefinition>> DefinitionCache;
};
