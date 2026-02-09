#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/CGFItemStorageInterface.h"
#include "ItemStorageSubsystem.generated.h"

class UInventoryComponent;

/**
 * Auto-save coordinator subsystem.
 * Manages registered inventory components, periodic dirty checks,
 * and routing save/load operations to the configured storage backend.
 * Survives level transitions as a UGameInstanceSubsystem.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API UItemStorageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- USubsystem ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Registration ---

	/** Register an inventory component for auto-save tracking */
	void RegisterInventory(UInventoryComponent* Component, const FString& OwnerId);

	/** Unregister an inventory component (saves if dirty) */
	void UnregisterInventory(UInventoryComponent* Component);

	// --- Manual Operations ---

	/** Save a specific inventory component immediately */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	void SaveInventory(UInventoryComponent* Component);

	/** Load items from storage into a component */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	void LoadInventory(UInventoryComponent* Component, const FString& OwnerId);

	/** Force-save all dirty registered inventories */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	void SaveAll();

private:
	/** Auto-save timer callback */
	void OnAutoSaveTick();

	/** Collect all valid items from a component's slots */
	static TArray<FItemInstance> CollectItemsFromComponent(UInventoryComponent* Component);

	/** Replace a component's inventory with loaded items */
	static void ApplyItemsToComponent(UInventoryComponent* Component, const TArray<FItemInstance>& Items);

	/** Registered components and their owner IDs */
	struct FRegisteredInventory
	{
		TWeakObjectPtr<UInventoryComponent> Component;
		FString OwnerId;
	};
	TArray<FRegisteredInventory> RegisteredInventories;

	/** The active storage backend */
	UPROPERTY()
	TScriptInterface<ICGFItemStorageInterface> StorageBackend;

	/** Auto-save timer handle */
	FTimerHandle AutoSaveTimerHandle;
};
