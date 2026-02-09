#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Types/CGFCommonEnums.h"
#include "Types/CGFItemTypes.h"
#include "Types/InventoryOperationTypes.h"
#include "InventoryComponent.generated.h"

class UItemDefinition;
class UItemDatabaseSubsystem;
class UItemStorageSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryOperationFailed, EInventoryOperationResult, Result);

/**
 * Manages an inventory of item instances using pre-allocated slots.
 * Supports stacking, weight limits, tag filtering, and cross-inventory transfers.
 * Multiplayer: Try* operations route through ServerRPCs when called on clients.
 * FFastArraySerializer replicates slot changes and fires delegates on clients.
 */
UCLASS(BlueprintType, ClassGroup = "Inventory", meta = (BlueprintSpawnableComponent))
class ITEMINVENTORYPLUGIN_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	// -----------------------------------------------------------------------
	// Configuration
	// -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	int32 MaxSlots = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	float MaxWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	FGameplayTagContainer AllowedItemTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	FGameplayTagContainer DeniedItemTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
	FGameplayTag InventoryTypeTag;

	// -----------------------------------------------------------------------
	// Storage / Persistence
	// -----------------------------------------------------------------------

	/** Unique identifier for this inventory in the storage system (e.g. "Player_0", "Chest_42"). Empty = no auto-persistence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Storage")
	FString StorageOwnerId;

	/** If true, automatically registers with ItemStorageSubsystem on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Storage")
	bool bAutoRegisterStorage = true;

	// -----------------------------------------------------------------------
	// State
	// -----------------------------------------------------------------------

	UPROPERTY(Replicated)
	FInventorySlotArray InventorySlots;

	// -----------------------------------------------------------------------
	// Operations
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryAddItem(const FItemInstance& Item, int32 PreferredSlot = -1);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryRemoveItem(const FGuid& InstanceId, int32 Count = -1);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryMoveItem(const FGuid& InstanceId, UInventoryComponent* TargetInventory, int32 TargetSlot = -1);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TrySplitStack(const FGuid& InstanceId, int32 SplitCount);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryMergeStacks(const FGuid& SourceId, const FGuid& TargetId);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TrySwapSlots(int32 SlotA, int32 SlotB);

	// -----------------------------------------------------------------------
	// Queries
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	bool HasItem(FPrimaryAssetId ItemDefId, int32 RequiredCount = 1) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	int32 GetItemCount(FPrimaryAssetId ItemDefId) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	TArray<FItemInstance> FindItemsByTag(const FGameplayTagContainer& Tags) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	FItemInstance GetItemInSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	float GetCurrentWeight() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	int32 GetFilledSlotCount() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	bool CanAcceptItem(const FItemInstance& Item) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	int32 FindFirstEmptySlot() const;

	/** Find a slot by instance GUID. C++ only — returns nullptr if not found. */
	FInventorySlot* FindSlotByInstanceId(const FGuid& InstanceId);
	const FInventorySlot* FindSlotByInstanceId(const FGuid& InstanceId) const;

	/** Blueprint-friendly: returns the slot index containing the given instance, or -1 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
	int32 FindSlotIndexByInstanceId(const FGuid& InstanceId) const;

	// -----------------------------------------------------------------------
	// Events
	// -----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryItemAdded OnItemAdded;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryItemRemoved OnItemRemoved;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryItemMoved OnItemMoved;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryItemUpdated OnItemUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryChanged OnInventoryChanged;

	/** Fired on clients when a server RPC rejects an operation */
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryOperationFailed OnOperationFailed;

	// -----------------------------------------------------------------------
	// Storage Operations
	// -----------------------------------------------------------------------

	/** Manually trigger a save for this inventory */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Storage")
	void ManualSave();

	/** Manually load this inventory from storage */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Storage")
	void ManualLoad();

	// -----------------------------------------------------------------------
	// Dirty Tracking (for persistence)
	// -----------------------------------------------------------------------

	bool IsDirty() const { return bIsDirty; }
	void ClearDirty() { bIsDirty = false; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// -----------------------------------------------------------------------
	// Server RPCs — received and executed on the server
	// -----------------------------------------------------------------------

	UFUNCTION(Server, Reliable)
	void ServerRPC_RequestAddItem(const FItemInstance& Item, int32 PreferredSlot);

	UFUNCTION(Server, Reliable)
	void ServerRPC_RequestRemoveItem(const FGuid& InstanceId, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerRPC_RequestMoveItem(const FGuid& InstanceId, UInventoryComponent* Target, int32 TargetSlot);

	UFUNCTION(Server, Reliable)
	void ServerRPC_RequestSplitStack(const FGuid& InstanceId, int32 SplitCount);

	UFUNCTION(Server, Reliable)
	void ServerRPC_RequestMergeStacks(const FGuid& SourceId, const FGuid& TargetId);

	UFUNCTION(Server, Reliable)
	void ServerRPC_RequestSwapSlots(int32 SlotA, int32 SlotB);

	// -----------------------------------------------------------------------
	// Client RPC — notify client of server-side rejection
	// -----------------------------------------------------------------------

	UFUNCTION(Client, Reliable)
	void ClientRPC_OperationFailed(EInventoryOperationResult Result);

	// -----------------------------------------------------------------------
	// Validation (const, side-effect-free, reusable for client + server)
	// -----------------------------------------------------------------------

	bool ValidateAddItem(const FItemInstance& Item, int32 PreferredSlot, FInventoryOperationData& OutData) const;
	bool ValidateRemoveItem(const FGuid& InstanceId, int32 Count, FInventoryOperationData& OutData) const;

	/** Check if item passes tag filters */
	bool PassesTagFilter(const FItemInstance& Item) const;

	/** Get the effective max stack size for an item */
	int32 GetMaxStackSize(const FItemInstance& Item) const;

	/** Get item weight from definition */
	float GetItemWeight(const FItemInstance& Item) const;

	// -----------------------------------------------------------------------
	// Internal Mutation (execute after validation passes)
	// -----------------------------------------------------------------------

	void Internal_AddItem(const FItemInstance& Item, int32 TargetSlot);
	void Internal_AddItemStacked(const FItemInstance& Item, int32 PreferredSlot = -1);
	void Internal_RemoveItem(const FGuid& InstanceId, int32 Count);
	void Internal_ClearSlot(int32 SlotIndex);

	void MarkDirty();
	void BroadcastChanged();

	/** Set up FFastArraySerializer replication callbacks */
	void SetupReplicationCallbacks();

	// -----------------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------------

	UItemDatabaseSubsystem* GetItemDatabase() const;

	bool bIsDirty = false;

	UPROPERTY()
	mutable TObjectPtr<UItemDatabaseSubsystem> CachedItemDatabase;
};
