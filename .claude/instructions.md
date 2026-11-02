# ItemInventoryPlugin — Plugin Instructions

> State as of 2026-11-02 (code review CR5).

## Purpose

The item data layer: item definitions and fragments, the item database, the replicated inventory component, loot tables,
crafting, persistence (local and remote storage, blobs for the world save), the Item Database Browser and the inventory
UI widgets. Depends on CommonGameFramework only. InteractionPlugin and EquipmentPlugin depend on it (downward, required;
D-CR22); it never references either of them.

## Reading Order

1. `Documentation/ITEM_INVENTORY_SYSTEM.md` — the system: definitions, fragments, the database, inventory operations and
   validation, loot, crafting, storage, the editor tool, console commands, tests, UI widgets and click-to-move.
2. `Plugins/CommonGameFramework/Documentation/ARCHITECTURE.md` — how the framework plugins fit together.
3. `Plugins/CommonGameFramework/Documentation/COMMON_TYPES.md` — `FItemInstance`, `FInventorySlot`,
   `ICGFItemStorageInterface`, the tags this plugin uses.

## Module Structure

```
ItemInventoryPlugin/
├── Source/
│   ├── ItemInventoryPlugin/                  ← runtime module (API macro ITEMINVENTORYPLUGIN_API, log LogIIP)
│   │   ├── Public/
│   │   │   ├── Data/                         ← UItemDefinition, UItemDefinitionFragment, Fragments/ItemFragment_*,
│   │   │   │                                    ULootTable, FLootEntry, UCraftingRecipe
│   │   │   ├── Components/InventoryComponent.h
│   │   │   ├── Subsystems/                   ← UItemDatabaseSubsystem, UCraftingSubsystem (game instance),
│   │   │   │                                    ULootGenerationSubsystem (world)
│   │   │   ├── Storage/                      ← UItemStorageSubsystem, ULocalItemStorage, URemoteItemStorage,
│   │   │   │                                    UItemSystemSettings, FItemSerializationUtils
│   │   │   ├── Types/                        ← InventoryOperationTypes.h (FInventoryOperationData),
│   │   │   │                                    ItemInstanceFragments.h (UInstanceFragment_DurabilityState, _CustomDataMap)
│   │   │   ├── UI/                           ← InventorySlot, Hotbar, InventoryPanel, ItemCursor, ItemTooltip widgets
│   │   │   └── ItemInventoryPlugin.h
│   │   └── Private/ (mirrors Public; Tests/ holds the automation tests; ItemInventoryPlugin.cpp the console commands)
│   └── ItemSystemEditor/                     ← editor module: SItemDatabaseBrowser (Tools > Item Database Browser)
└── ItemInventoryPlugin.uplugin               ← requires CommonGameFramework, GameplayAbilities
```

## Rules

1. **One definition per item type; behaviour comes from fragments.** Never subclass `UItemDefinition`. Tag every item
   with an `Item.Category.*` and an `Item.Rarity.*`. Items whose instances differ (durability) keep `MaxStackSize` 1.
   Icons and meshes are soft references, loaded asynchronously in gameplay code.
2. **Instances come from the database on the server.** `UItemDatabaseSubsystem::CreateItemInstance` is the only
   constructor of runtime items; clients receive instances by replication and never make GUIDs. **No client request
   carries an item (G91):** `TryAddItem` and `TryReplaceItem` take a whole item and run on the authority only
   (`NotAuthorized` on a client; there is no add RPC); a client's requests name items the server holds, by instance id.
   Loading from storage is refused for a client's component too.
3. **Inventory state changes only through `UInventoryComponent`.** Nothing outside the class writes `InventorySlots`;
   every mutation goes through an `Internal_*` function, which marks the slot dirty for replication, marks the component
   dirty for storage and broadcasts the delegates. Slot indices are stable: removing never shifts other slots.
4. **Validation (D-CR16):** every Server RPC validates inline and never trusts the client's check. Remove and move share
   the const predicates `ValidateRemoveItem` / `ValidateMoveItem` (with `ValidateAddItem` / `ValidateAddItemAtSlot` for the
   target), which the `Try*` function runs on the caller and the RPC runs again; split, merge and swap repeat their inline
   checks in the RPC. A refused RPC answers with
   `ClientRPC_OperationFailed`. New operations follow the same shape: a const predicate where the client pre-checks, the
   checks again in the RPC, results as `EInventoryOperationResult`. A request that names another inventory checks that
   the requester can reach it (`IsReachableBy`: the same player's, or opened to it with `SetOpenTo`; G96).
5. **No partial operations.** An add that does not fit entirely is refused; a cross-inventory move validates both sides
   before touching either, and a move to a named slot lands exactly there or is refused (G94); a merge that would
   overflow is refused; a replace is validated whole.
6. **Storage goes through `ICGFItemStorageInterface`.** Pick the backend in `UItemSystemSettings`; never write files or
   HTTP from game code. The world save uses the same backend's blobs. Nothing waits on the game thread (G93): callers
   use the asynchronous calls (`SaveInventoryAsync`, `LoadInventoryAsync`), and a backend keeps the ordering rule (for one
   inventory the last save requested lands; `ITEM_INVENTORY_SYSTEM.md`, URemoteItemStorage).
7. **Recipes and definitions are primary assets.** A new `UCraftingRecipe` or `UItemDefinition` folder must be in the
   project's AssetManager scan list. `Craft` runs on the authority; callers check the station (distance, tag) inside their
   server RPC.
8. **UI widgets are code-built** in `NativeOnInitialized()` (never `NativeConstruct()`), bind data with `Init*()`, and
   relay child slot delegates through the container; the player controller binds containers, not slots.

## Build.cs Dependencies

```csharp
// ItemInventoryPlugin (Runtime)
PublicDependencyModuleNames:  Core, CoreUObject, Engine, NetCore, GameplayTags, GameplayAbilities, CommonGameFramework
PrivateDependencyModuleNames: HTTP, Json (storage), Niagara, DeveloperSettings, UMG, Slate, SlateCore, InputCore (UI)
                              Sockets (not in Shipping: the remote storage tests' in-process HTTP server)

// ItemSystemEditor (Editor)
PublicDependencyModuleNames:  Core, CoreUObject, Engine, Slate, SlateCore, InputCore
PrivateDependencyModuleNames: UnrealEd, ItemInventoryPlugin, CommonGameFramework, AssetRegistry, GameplayTags, ToolMenus
```

## Logging, Commands and Tests

- Log category `LogIIP` (`ItemInventoryPlugin.h`); never `LogTemp`.
- Console commands: `Inventory.Give`, `Inventory.Clear`, `Inventory.List`, `Inventory.Recipes`, `Loot.Roll`
  (`Documentation/EDITOR_CONSOLE_GUIDE.md`).
- Tests live in `Source/ItemInventoryPlugin/Private/Tests/` under `ItemInventory.*`; every inventory operation has
  success and failure cases there. Tests that need the item database or loot subsystem use
  `CGFTesting::FTestGameInstance` (CommonGameFramework); `ItemInventory.Authority.*` plays a client and the server in
  one world (a client is an actor with role AutonomousProxy, `SpawnRemoteOwner`; its requests are delivered to the
  server's component with `CGFTesting::FRPCCall`). That covers the RPC bodies, not replication itself: there is still no
  replication functional test (D-CR23 open).
