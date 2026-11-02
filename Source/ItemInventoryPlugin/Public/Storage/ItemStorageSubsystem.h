#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/CGFItemStorageInterface.h"
#include "ItemStorageSubsystem.generated.h"

class UInventoryComponent;

/** A save the subsystem started has finished: the inventory's storage id and whether it landed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryStorageSaved, const FString&, OwnerId, bool, bSuccess);
/** A load the subsystem started has finished: the storage id and whether the items were read and applied. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryStorageLoaded, const FString&, OwnerId, bool, bSuccess);

/**
 * Auto-save coordinator subsystem.
 * Manages registered inventory components, periodic dirty checks,
 * and routing save/load operations to the configured storage backend.
 * Survives level transitions as a UGameInstanceSubsystem.
 *
 * Saves and loads are asynchronous (G93): every call returns at once and reports through OnInventorySaved /
 * OnInventoryLoaded. Starting a save clears the component's dirty flag; a save that fails marks it dirty again, so the
 * next auto-save tries again. A load that fails leaves the inventory as it was.
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

	/** Unregister an inventory component (starts saving it if dirty) */
	void UnregisterInventory(UInventoryComponent* Component);

	// --- Manual Operations ---

	/**
	 * Start saving a registered inventory and return at once; OnInventorySaved reports the result.
	 * @param Component A component registered with RegisterInventory
	 * @return False when it is not registered or there is no backend (nothing started)
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	bool SaveInventory(UInventoryComponent* Component);

	/**
	 * Start loading a component's items and return at once; when the read lands the inventory is replaced by it and
	 * OnInventoryLoaded reports. A failed read leaves the inventory as it was. Server only.
	 * @param Component The inventory to fill
	 * @param OwnerId   The storage id to read
	 * @return False when refused: no component, backend or id, or a client's component (G91)
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	bool LoadInventory(UInventoryComponent* Component, const FString& OwnerId);

	/**
	 * Start saving every dirty registered inventory (fire-and-forget).
	 * @return The number of saves started
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	int32 SaveAll();

	/** The configured backend (local / remote), for systems that store their own documents through it (feature 9). */
	UFUNCTION(BlueprintPure, Category = "Item Storage")
	TScriptInterface<ICGFItemStorageInterface> GetStorageBackend() const { return StorageBackend; }

	/**
	 * Replace the backend chosen from UItemSystemSettings (a custom backend, or a configured remote one).
	 * @param Backend An object implementing ICGFItemStorageInterface
	 * @return False when Backend does not implement the interface (the old backend stays)
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Storage")
	bool SetStorageBackend(UObject* Backend);

	/** A save started by this subsystem (manual, auto-save, unregister, SaveAll) finished. */
	UPROPERTY(BlueprintAssignable, Category = "Item Storage")
	FOnInventoryStorageSaved OnInventorySaved;

	/** A load started by LoadInventory finished. */
	UPROPERTY(BlueprintAssignable, Category = "Item Storage")
	FOnInventoryStorageLoaded OnInventoryLoaded;

private:
	/** Auto-save timer callback */
	void OnAutoSaveTick();

	/** Start the asynchronous save of one inventory and clear its dirty flag. */
	void StartSave(UInventoryComponent* Component, const FString& OwnerId);

	/** The backend's save callback: a failure marks the inventory dirty again; broadcasts OnInventorySaved. */
	UFUNCTION()
	void HandleSaveComplete(const FString& OwnerId, bool bSuccess);

	/** The backend's load callback: applies the items to the component waiting for them; broadcasts OnInventoryLoaded. */
	UFUNCTION()
	void HandleLoadComplete(const FString& OwnerId, bool bSuccess, const TArray<FItemInstance>& Items);

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

	/** Loads in flight, in request order: the backend reports one owner's operations in order, so the first entry with an id is the one its result is for. */
	TArray<FRegisteredInventory> PendingLoads;

	/** The active storage backend */
	UPROPERTY()
	TScriptInterface<ICGFItemStorageInterface> StorageBackend;

	/** Auto-save timer handle */
	FTimerHandle AutoSaveTimerHandle;
};
