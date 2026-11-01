#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/AssetManager.h"
#include "Types/CGFItemTypes.h"
#include "ItemDatabaseSubsystem.generated.h"

class UItemDefinition;
class UInventoryComponent;

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

	/** Register a programmatic definition (for testing/runtime-created items) */
	UFUNCTION(BlueprintCallable, Category = "Item Database")
	void RegisterDefinition(FPrimaryAssetId AssetId, UItemDefinition* Definition);

	// --- Instance Creation ---

	/**
	 * Create a new FItemInstance from a definition. SERVER ONLY.
	 * Generates a new GUID, copies DefaultInstanceFragments from the definition.
	 * Returns invalid instance if definition not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Database")
	FItemInstance CreateItemInstance(FPrimaryAssetId AssetId, int32 StackCount = 1);

	/**
	 * Give an inventory a stack of a definition named on the console: the one implementation behind Inventory.Give and the
	 * character's GiveItem exec (code review CR2, FW-37). The name is the definition's primary asset name, exactly, or with
	 * bMatchSubstring the first registered definition whose name contains it. SERVER ONLY (CreateItemInstance).
	 * @param Inventory The inventory to add to (null: Failed, once the definition is resolved and the instance made)
	 * @param Name The definition's name, or a part of it
	 * @param Count The stack count (at least 1)
	 * @param bMatchSubstring Take the first registered definition whose name contains Name instead of the exact name
	 * @param OutDefinition The definition resolved (invalid when no name matched)
	 * @return ItemNotFound when no definition matched the substring, InvalidItem when no instance could be made from the
	 *         definition, Failed with no inventory, else the inventory's result
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Database|Debug")
	EInventoryOperationResult GiveItemByName(UInventoryComponent* Inventory, const FString& Name, int32 Count, bool bMatchSubstring, FPrimaryAssetId& OutDefinition);

private:
	/** Scan asset registry and cache definitions */
	void ScanForDefinitions();

	/** Cached definitions keyed by PrimaryAssetId */
	UPROPERTY()
	TMap<FPrimaryAssetId, TObjectPtr<UItemDefinition>> DefinitionCache;
};
