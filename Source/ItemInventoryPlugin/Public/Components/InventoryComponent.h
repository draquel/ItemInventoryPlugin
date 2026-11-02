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
 * Multiplayer: operations on items the inventory already holds (remove, move, split, merge, swap) route through
 * ServerRPCs when called on clients; operations that take a whole item (add, replace) run on the authority only, so a
 * client can never hand the server an item (G91). FFastArraySerializer replicates slot changes and fires delegates on
 * clients.
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

	/**
	 * Add an item, stacking onto existing stacks first, then filling empty slots (PreferredSlot first). Authority only:
	 * items enter an inventory from server code (loot, crafting, pickups, gathering, saves); a client has no request
	 * that carries an item (G91). The listen-server host has authority and adds directly.
	 * @param Item          The item to add, made by UItemDatabaseSubsystem::CreateItemInstance on the server
	 * @param PreferredSlot The empty slot tried first for what does not stack (-1 = none)
	 * @return Success; NotAuthorized on a client; the validation failure otherwise (nothing changes)
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryAddItem(const FItemInstance& Item, int32 PreferredSlot = -1);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryRemoveItem(const FGuid& InstanceId, int32 Count = -1);

	/**
	 * Move a whole stack to another inventory (or another slot of this one). Both sides are validated before either
	 * changes. A client sends the request to the server, which also requires the target to be reachable by this
	 * inventory's owner (IsReachableBy: the same player's, or opened to it with SetOpenTo; G96) and answers NotAuthorized
	 * through OnOperationFailed otherwise. The authority's own call is game code and moves between any inventories.
	 * @param InstanceId      The stack to move
	 * @param TargetInventory Where it goes
	 * @param TargetSlot      -1: stack onto the target's stacks, then its first empty slots. A slot: exactly there, into
	 *                        the empty slot or merged whole into the same item's stack in it (G94)
	 * @return Success (Pending on a client); InvalidSlot, SlotOccupied (another item there), StackFull (the stack there
	 *         cannot take the whole count) or the add / remove validation failure otherwise (nothing changes)
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryMoveItem(const FGuid& InstanceId, UInventoryComponent* TargetInventory, int32 TargetSlot = -1);

	/**
	 * Put NewItem where the stack InstanceId is, in one step: the stack leaves and NewItem takes its slot. Validated
	 * whole first (NewItem passes the filters, fits one stack, and the weight after the exchange is allowed), so nothing
	 * changes on a refusal. Authority only, like TryAddItem: equipping from the inventory uses it to put the item it
	 * replaces into the slot the new one came from (G90).
	 * @param InstanceId The stack to take out
	 * @param NewItem    The item to put in its slot
	 * @return Success; NotAuthorized on a client; ItemNotFound, InvalidItem, FilterRejected, StackFull or WeightExceeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryReplaceItem(const FGuid& InstanceId, const FItemInstance& NewItem);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TrySplitStack(const FGuid& InstanceId, int32 SplitCount);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TryMergeStacks(const FGuid& SourceId, const FGuid& TargetId);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
	EInventoryOperationResult TrySwapSlots(int32 SlotA, int32 SlotB);

	// -----------------------------------------------------------------------
	// Access: which requesters may name this inventory as a move's target (G96)
	// -----------------------------------------------------------------------

	/**
	 * Open this inventory to a requester, or close it again: while open, the requester's move requests may name it as their
	 * target (a chest or a station the player has opened). Server only and not replicated: the game layer opens on the
	 * interaction that shows the container and closes when it is closed or out of reach.
	 * @param Requester The player's actor (its pawn, its player state or its controller: the same player either way)
	 * @param bOpen     True to open, false to close
	 * @return False on a client or for no requester (nothing changes)
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Access")
	bool SetOpenTo(AActor* Requester, bool bOpen);

	/**
	 * The reach rule for a move's target, which the server checks on every client move request (G96): the requester's own
	 * inventory (this inventory's owner and the requester have the same owner at the top of their owner chains: the same
	 * player, whether the inventory sits on its pawn, its player state or its controller), or one opened to that player
	 * with SetOpenTo. An inventory with no owner is reachable by no one.
	 * @param Requester The actor whose inventory the stack leaves (the move request's source owner)
	 * @return True if the requester may move items into this inventory
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Access")
	bool IsReachableBy(const AActor* Requester) const;

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

	/**
	 * The item fits exactly at Slot: the slot is empty, or holds the same item with room for the whole count.
	 * @param bCountWeight False when the item is already counted in this inventory's weight (a move between its own slots)
	 */
	bool ValidateAddItemAtSlot(const FItemInstance& Item, int32 Slot, bool bCountWeight, FInventoryOperationData& OutData) const;

	/** TryMoveItem's predicate, run by the caller and again by the RPC: the source can release, the target can take. */
	bool ValidateMoveItem(const FGuid& InstanceId, const UInventoryComponent* Target, int32 TargetSlot, FInventoryOperationData& OutData) const;

	/** TryReplaceItem's predicate: the stack exists and NewItem can take its slot. */
	bool ValidateReplaceItem(const FGuid& InstanceId, const FItemInstance& NewItem, FInventoryOperationData& OutData) const;

	/** True where the inventory's state may change: the owner has authority, or there is no owner (a bare component). */
	bool HasMutationAuthority() const;

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
	/** Place the item in Slot, or merge its count into the stack there (after ValidateAddItemAtSlot). */
	void Internal_AddItemAtSlot(const FItemInstance& Item, int32 Slot);
	/** Remove the stack here and add it to Target (at TargetSlot when one is given), after ValidateMoveItem. */
	void Internal_MoveItem(const FGuid& InstanceId, UInventoryComponent* Target, int32 TargetSlot);
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

	/** The players this inventory is open to (SetOpenTo), by any of their actors; server only. */
	TArray<TWeakObjectPtr<const AActor>> OpenToRequesters;

	UPROPERTY()
	mutable TObjectPtr<UItemDatabaseSubsystem> CachedItemDatabase;
};
