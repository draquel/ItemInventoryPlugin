# ItemInventoryPlugin — Plugin Instructions

## Purpose

Core item data layer, inventory management, loot table generation, and persistence. This is the largest plugin and the backbone of the system. Depends on CommonGameFramework for shared types and interfaces.

## Documentation

This plugin's `Documentation/` folder contains:
- `ITEM_INVENTORY_SYSTEM.md` — Detailed design for item definitions, the fragment system, inventory component internals, loot table generation, storage/persistence, and **UI widgets** (slot, hotbar, panel, cursor, click-to-move interaction).

Also reference:
- `Plugins/CommonGameFramework/Documentation/ARCHITECTURE.md` — Master architecture, data flow, replication model, phased build plan.
- `Plugins/CommonGameFramework/Documentation/COMMON_TYPES.md` — FItemInstance, shared interfaces, and tags this plugin depends on.

## Module Structure

This plugin contains two modules:

```
ItemInventoryPlugin/
├── Source/
│   ├── ItemSystem/                        ← Runtime module: items, inventories, loot, storage
│   │   ├── Public/
│   │   │   ├── Data/
│   │   │   │   ├── ItemDefinition.h               ← UItemDefinition (PrimaryDataAsset)
│   │   │   │   ├── ItemDefinitionFragment.h       ← UItemDefinitionFragment (base class)
│   │   │   │   ├── Fragments/
│   │   │   │   │   ├── ItemFragment_Weapon.h      ← Weapon stats fragment
│   │   │   │   │   ├── ItemFragment_Consumable.h  ← Consumable behavior fragment
│   │   │   │   │   ├── ItemFragment_Durability.h  ← Durability fragment
│   │   │   │   │   ├── ItemFragment_Stackable.h   ← Stack rules fragment
│   │   │   │   │   ├── ItemFragment_Equipment.h   ← Marks item as equippable (slot, visuals)
│   │   │   │   │   └── ItemFragment_WorldDisplay.h← Mesh, material, effects for world representation
│   │   │   │   ├── LootTable.h                    ← ULootTable data asset
│   │   │   │   └── LootEntry.h                    ← FLootEntry struct (entries within loot tables)
│   │   │   ├── Components/
│   │   │   │   └── InventoryComponent.h           ← UInventoryComponent (replicated)
│   │   │   ├── Subsystems/
│   │   │   │   ├── ItemDatabaseSubsystem.h        ← UGameInstanceSubsystem: item registry
│   │   │   │   └── LootGenerationSubsystem.h      ← UWorldSubsystem: loot rolling
│   │   │   ├── Storage/
│   │   │   │   ├── LocalItemStorage.h             ← IItemStorage: local file/SaveGame impl
│   │   │   │   └── RemoteItemStorage.h            ← IItemStorage: HTTP/database impl
│   │   │   ├── Types/
│   │   │   │   ├── InventoryOperationResult.h     ← EInventoryOperationResult, FInventoryOperationData
│   │   │   │   ├── FastArrayInventory.h           ← FFastArraySerializer wrapper for inventory slots
│   │   │   │   └── ItemInstanceFragment.h         ← Re-export from Common + additional instance fragments
│   │   │   └── ItemSystem.h                       ← Module API
│   │   └── Private/
│   │       ├── (mirrors Public structure)
│   │       └── ItemSystem.cpp
│   └── ItemSystemEditor/                  ← Editor module: custom editors, debug tools
│       ├── Public/
│       │   ├── LootTablePreview.h                 ← Editor utility for previewing loot distributions
│       │   └── ItemDatabaseBrowser.h              ← Editor utility for browsing item definitions
│       └── Private/
│           └── ItemSystemEditor.cpp
└── ItemInventoryPlugin.uplugin
```

## Key Classes — Detailed Design

### UItemDefinition (Primary Data Asset)

```cpp
UCLASS(BlueprintType)
class ITEMSYSTEM_API UItemDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // Identity
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    FText DisplayName;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    FText Description;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    TSoftObjectPtr<UTexture2D> Icon;

    // Classification
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
    FGameplayTagContainer ItemTags;  // Category, rarity, etc.

    // Stacking
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Stacking")
    int32 MaxStackSize = 1;  // 1 = non-stackable

    // Weight (for weight-based inventory limits)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Properties")
    float Weight = 0.0f;

    // Base value (for vendor/economy systems)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Properties")
    int32 BaseValue = 0;

    // Definition fragments — static data shared by all instances
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Item|Fragments")
    TArray<TObjectPtr<UItemDefinitionFragment>> Fragments;

    // Default instance fragments — copied to new instances of this item
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Item|Instance Defaults")
    TArray<TObjectPtr<UItemInstanceFragment>> DefaultInstanceFragments;

    // --- Helper functions ---
    // Find a specific fragment type on this definition
    template<typename T>
    T* FindFragment() const;

    // Does this item have a fragment of the given type?
    template<typename T>
    bool HasFragment() const;

    // PrimaryDataAsset overrides
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
```

**Rules for item definitions:**
- Every item type in the game is ONE UItemDefinition asset
- Use fragments for type-specific data — do NOT create UItemDefinition subclasses
- ItemTags should always include at least one `Item.Category.*` tag and one `Item.Rarity.*` tag
- MaxStackSize of 1 means the item cannot stack. Items with instance fragments that make each instance unique (like durability) should generally have MaxStackSize = 1
- Icon uses TSoftObjectPtr for async loading — never hard-reference textures on definitions

### UInventoryComponent (The Core Component)

```cpp
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class ITEMSYSTEM_API UInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // --- Configuration ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    int32 MaxSlots = 20;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    float MaxWeight = 0.0f;  // 0 = unlimited

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    FGameplayTagContainer AllowedItemTags;  // Empty = allow all

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    FGameplayTagContainer DeniedItemTags;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Config")
    FGameplayTag InventoryTypeTag;  // Inventory.Type.Player, etc.

    // --- State (Replicated via FastArraySerializer) ---
    UPROPERTY(Replicated)
    FInventorySlotArray InventorySlots;  // FFastArraySerializer-based

    // --- Operations (all return result enum) ---
    UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
    EInventoryOperationResult TryAddItem(const FItemInstance& Item, int32 PreferredSlot = -1);

    UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
    EInventoryOperationResult TryRemoveItem(const FGuid& InstanceId, int32 Count = -1); // -1 = all

    UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
    EInventoryOperationResult TryMoveItem(const FGuid& InstanceId, UInventoryComponent* TargetInventory, int32 TargetSlot = -1);

    UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
    EInventoryOperationResult TrySplitStack(const FGuid& InstanceId, int32 SplitCount);

    UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
    EInventoryOperationResult TryMergeStacks(const FGuid& SourceId, const FGuid& TargetId);

    UFUNCTION(BlueprintCallable, Category = "Inventory|Operations")
    EInventoryOperationResult TrySwapSlots(int32 SlotA, int32 SlotB);

    // --- Queries ---
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
    bool HasItem(FPrimaryAssetId ItemDefId, int32 RequiredCount = 1) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
    int32 GetItemCount(FPrimaryAssetId ItemDefId) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Inventory|Query")
    TArray<FItemInstance> FindItemsByTag(FGameplayTagContainer Tags) const;

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

    // --- Events ---
    UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
    FOnInventoryItemAdded OnItemAdded;

    UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
    FOnInventoryItemRemoved OnItemRemoved;

    UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
    FOnInventoryItemMoved OnItemMoved;

    UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
    FOnInventoryItemUpdated OnItemUpdated;

    UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
    FOnInventoryChanged OnInventoryChanged;  // Catch-all

private:
    // --- Server RPCs ---
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

    // --- Validation (used by both client prediction and server authority) ---
    bool ValidateAddItem(const FItemInstance& Item, int32 Slot) const;
    bool ValidateMoveItem(const FGuid& InstanceId, UInventoryComponent* Target, int32 TargetSlot) const;
    // ... etc

    // --- Internal ---
    void Internal_AddItem(const FItemInstance& Item, int32 Slot);
    void Internal_RemoveItem(const FGuid& InstanceId, int32 Count);
    // ... etc — these do the actual array mutation, called after validation passes

    // Dirty tracking for persistence
    bool bIsDirty = false;
    void MarkDirty();
};
```

**Replication architecture:**

The public Try* functions check `HasAuthority()`:
- If server (or standalone): validate and execute directly
- If client: call the corresponding ServerRPC, optionally predict locally

The server RPC validates, executes via Internal_* functions, and the FFastArraySerializer replicates changes to clients. OnRep callbacks on the client fire the Blueprint delegates.

**Rules for UInventoryComponent:**
- Never directly modify InventorySlots from outside this class
- All slot mutations go through Internal_* functions which handle dirty marking, delegate broadcasting, and FastArray marking
- The component implements IInventoryOwner (from Common) on behalf of its owning actor — the actor's implementation just returns this component
- Slot indices are stable — removing an item from slot 3 leaves slot 3 empty, it does NOT shift other items down

### Loot System

**ULootTable** is a data asset containing `TArray<FLootEntry>`. Each FLootEntry:
```cpp
USTRUCT(BlueprintType)
struct FLootEntry
{
    GENERATED_BODY()

    // What to drop — one of these should be set
    UPROPERTY(EditAnywhere, Category = "Loot")
    FPrimaryAssetId ItemDefinitionId;  // Direct item reference

    UPROPERTY(EditAnywhere, Category = "Loot")
    TSoftObjectPtr<ULootTable> NestedTable;  // Or a sub-table to roll on

    // Probability
    UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0"))
    float Weight = 1.0f;  // Relative weight in the pool

    UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DropChance = 1.0f;  // Independent probability (1.0 = always if selected)

    // Quantity
    UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "1"))
    int32 MinQuantity = 1;

    UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "1"))
    int32 MaxQuantity = 1;

    // Conditions
    UPROPERTY(EditAnywhere, Category = "Loot")
    FGameplayTagContainer RequiredContextTags;  // Context must have ALL of these

    UPROPERTY(EditAnywhere, Category = "Loot")
    FGameplayTagContainer ExcludedContextTags;  // Context must have NONE of these

    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 MinLevel = 0;  // 0 = no minimum

    UPROPERTY(EditAnywhere, Category = "Loot")
    int32 MaxLevel = 0;  // 0 = no maximum
};
```

**ULootTable** data asset:
```cpp
UCLASS(BlueprintType)
class ITEMSYSTEM_API ULootTable : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "Loot Table")
    TArray<FLootEntry> Entries;

    // How many times to roll on this table
    UPROPERTY(EditAnywhere, Category = "Loot Table", meta = (ClampMin = "1"))
    int32 RollCount = 1;

    // If true, same item can be selected multiple times across rolls
    UPROPERTY(EditAnywhere, Category = "Loot Table")
    bool bAllowDuplicates = true;

    // Guaranteed drops — always included regardless of rolls
    UPROPERTY(EditAnywhere, Category = "Loot Table")
    TArray<FLootEntry> GuaranteedEntries;
};
```

**LootGenerationSubsystem** (`UWorldSubsystem`) has one primary function:
```cpp
UFUNCTION(BlueprintCallable, Category = "Loot")
TArray<FItemInstance> GenerateLoot(ULootTable* Table, const FLootContext& Context);
```

It processes guaranteed entries first, then rolls RollCount times on the weighted pool, filtering by context conditions. Nested tables are resolved recursively with a depth cap of 5.

### Storage

**IItemStorage** implementations:

`ULocalItemStorage` — Serializes to JSON files in the SaveGames directory. One file per inventory owner (keyed by owner ID string). Uses `FJsonObjectConverter` for struct serialization. Synchronous for local saves, but the async interface wraps it in a game thread callback for API consistency.

`URemoteItemStorage` — Sends JSON payloads via `FHttpModule` to a configurable endpoint. Supports POST (save), GET (load), DELETE (delete). Auth token configurable via project settings. Retry logic with exponential backoff. This is a starting point — real projects will likely subclass or replace this with their specific backend integration.

Storage implementation is selected via project settings (a `UDeveloperSettings` subclass registered under "Plugins > Item System"). Default is local.

## Build.cs Dependencies

```csharp
// ItemSystem (Runtime)
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine", "NetCore",
    "GameplayTags", "GameplayAbilities", "GameplayTasks",
    "CommonGameFramework",
    "HTTP", "Json", "JsonUtilities",  // For storage
});

// ItemSystemEditor
PrivateDependencyModuleNames.AddRange(new string[]
{
    "ItemSystem", "UnrealEd", "Slate", "SlateCore",
    "EditorStyle", "PropertyEditor",
});
```

## Implementation Phases (Internal to this Plugin)

**Phase 2: Item Definitions**
1. UItemDefinitionFragment base class
2. UItemDefinition data asset
3. Built-in fragments: Weapon, Consumable, Durability, Stackable, Equipment, WorldDisplay
4. ItemDatabaseSubsystem
5. Create 5-10 test item definitions to validate the system

**Phase 3: Item Instances**
1. FItemInstance struct with full serialization
2. UItemInstanceFragment base class (in Common, but instance fragments defined here)
3. Built-in instance fragments: DurabilityState, CustomDataMap
4. Net serialization tests

**Phase 4: Inventory Component (Single Player)**
1. UInventoryComponent with all operations (no replication yet)
2. Validation functions
3. Query API
4. Events/delegates
5. Unit tests for every operation including edge cases

**Phase 5: Inventory Replication**
1. FFastArraySerializer implementation for inventory slots
2. Server RPCs
3. Client prediction (optional, can defer)
4. OnRep callbacks wired to delegates
5. Functional tests with simulated network

**Phase 7: Loot Tables**
1. FLootEntry struct
2. ULootTable data asset
3. LootGenerationSubsystem
4. Nested table resolution
5. Editor preview tool (in ItemSystemEditor module)
6. Distribution validation tests

**Phase 10: Storage**
1. IItemStorage interface (already in Common)
2. ULocalItemStorage implementation
3. URemoteItemStorage implementation
4. Project settings for storage selection
5. Auto-save with dirty tracking on UInventoryComponent
6. Integration tests

## UI Widgets

This plugin provides inventory UI widgets. They live in `Source/ItemInventoryPlugin/Public/UI/` and `Private/UI/`. See `Documentation/ITEM_INVENTORY_SYSTEM.md` section "UI Widgets" for full documentation.

### File Layout

```
Source/ItemInventoryPlugin/
├── Public/UI/
│   ├── InventorySlotWidget.h       ← Single slot: icon, stack count, click delegates
│   ├── HotbarWidget.h              ← Horizontal slot row, always-visible
│   ├── InventoryPanelWidget.h      ← Grid of slots, toggle-visible
│   └── ItemCursorWidget.h          ← Floating mouse-following icon
└── Private/UI/
    ├── InventorySlotWidget.cpp
    ├── HotbarWidget.cpp
    ├── InventoryPanelWidget.cpp
    └── ItemCursorWidget.cpp
```

### Widget Construction Pattern

All widgets use **programmatic WidgetTree construction** — no UMG Designer assets. The pattern is:

1. Override `NativeOnInitialized()` → call private `BuildWidgetTree()`
2. `BuildWidgetTree()` uses `WidgetTree->ConstructWidget<>()` to create Slate primitives (USizeBox, UImage, UOverlay, etc.)
3. Set `WidgetTree->RootWidget` to the root element
4. Child UUserWidget instances (e.g., slot widgets inside a hotbar) use `CreateWidget<>()` instead of `WidgetTree->ConstructWidget<>()` because they need their own widget tree lifecycle
5. Data binding (inventory component, slot index) is a separate `Init*()` call

**Do NOT** build widget trees in `NativeConstruct()` — `AddToViewport()` triggers `RebuildWidget()` before `NativeConstruct` fires.

### Delegate Relay Pattern

Leaf widgets (UInventorySlotWidget) fire delegates on click. Container widgets (UHotbarWidget, UInventoryPanelWidget) bind to each child's delegates and re-broadcast on their own delegates. The player controller binds to the container delegates. This keeps the controller decoupled from individual slot widgets.

```
Slot::OnSlotClicked → Container::HandleChildSlotClicked → Container::OnSlotClicked → Controller
```

### Build.cs Dependencies

UI widgets require: `UMG`, `Slate`, `SlateCore`, `InputCore` (for `EKeys` in click handling).

## Critical Implementation Notes

- **FFastArraySerializer**: Study `FFastArraySerializerItem` in UE source. Each FInventorySlot must inherit from it. Override `PreReplicatedRemove`, `PostReplicatedAdd`, `PostReplicatedChange` to fire inventory delegates on clients.
- **Stack merging edge case**: When adding an item that matches an existing stack, if the existing stack can't absorb the full quantity, the remainder must go to a new slot. TryAddItem must handle partial stacking across multiple slots.
- **Cross-inventory moves must be atomic**: TryMoveItem validates the target can accept BEFORE removing from source. If validation passes, remove from source and add to target in one logical operation. If either fails, rollback.
- **Item GUIDs are server-generated only**: Clients never create FItemInstance — they always receive them via replication or RPCs. This prevents GUID collisions.
