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

/**
 * Manages an inventory of item instances using pre-allocated slots.
 * Supports stacking, weight limits, tag filtering, and cross-inventory transfers.
 * Replication (RPCs, FastArraySerializer wiring) is added in Phase 5.
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

	// -----------------------------------------------------------------------
	// Dirty Tracking (for persistence)
	// -----------------------------------------------------------------------

	bool IsDirty() const { return bIsDirty; }
	void ClearDirty() { bIsDirty = false; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
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

	// -----------------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------------

	UItemDatabaseSubsystem* GetItemDatabase() const;

	bool bIsDirty = false;

	UPROPERTY()
	mutable TObjectPtr<UItemDatabaseSubsystem> CachedItemDatabase;
};
