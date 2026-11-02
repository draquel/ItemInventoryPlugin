# Item & Inventory System

> State as of 2026-11-02 (code review CR5).

## Overview

The design of ItemInventoryPlugin: item definitions and fragments, the item database, the inventory component and its
validation, loot tables, crafting, storage, the editor tool and the inventory UI widgets. How the plugin fits with
InteractionPlugin and EquipmentPlugin is in `Plugins/CommonGameFramework/Documentation/ARCHITECTURE.md`; the shared
types (`FItemInstance`, `FInventorySlot`, the storage interface) are in that folder's `COMMON_TYPES.md`.

Modules: `ItemInventoryPlugin` (runtime, headers under `Source/ItemInventoryPlugin/Public/`) and `ItemSystemEditor`
(editor). Log category `LogIIP`.

---

## Item Definition System

### UItemDefinition

`Data/ItemDefinition.h`. Each item type is exactly one `UItemDefinition` (`UPrimaryDataAsset`, primary asset type
`ItemDefinition`). Definitions are immutable at runtime: they describe what an item is, not one instance's state.

Fields: `DisplayName`, `Description`, `Icon` (`TSoftObjectPtr<UTexture2D>`), `ItemTags`, `MaxStackSize` (1 = does not
stack), `Weight`, `BaseValue`, `Fragments` (`UItemDefinitionFragment`, instanced) and `DefaultInstanceFragments`
(`UItemInstanceFragment` templates copied into every new instance). Lookup: `FindFragment<T>()` / `HasFragment<T>()` in
C++, `FindFragmentByClass` in Blueprint.

**Assets.** The project's definitions are under `/Game/PluginTesting/Items/` by category (`Armor`, `Consumables`,
`Keys`, `Materials`, `Tools`, `Weapons`), named `ID_<Category>_<Name>` (`ID_Weapon_IronSword`, `ID_Tool_Torch`); the
primary asset id is `ItemDefinition:<AssetName>`. The project's AssetManager settings must scan the folder.

### Fragment Composition

Item behaviour is composed from fragments, never by subclassing `UItemDefinition`:

| Item | Fragments |
|------|-----------|
| Iron sword | `UItemFragment_Weapon`, `UItemFragment_Equipment`, `UItemFragment_Durability`, `UItemFragment_WorldDisplay` |
| Health potion | `UItemFragment_Consumable`, `UItemFragment_Stackable`, `UItemFragment_WorldDisplay` |
| Torch | `UItemFragment_Equipment`, `UItemFragment_LightSource`, `UItemFragment_Durability` (the fuel) |
| Campfire kit | `UItemFragment_Placeable` |
| Boss key | `UItemFragment_Key` |

### Built-In Definition Fragments

`Data/Fragments/`.

```
UItemFragment_Weapon        BaseDamage, AttackSpeed, DamageType (FGameplayTag), CritChance, CritMultiplier
UItemFragment_Consumable    AttributeChanges (TArray<FCGFAttributeModifier>), ConsumeEffect, ConsumeAbility,
                            bConsumeOnUse, CooldownDuration
UItemFragment_Durability    MaxDurability, bDestroyAtZero, DegradeRate (per use)
                            pairs with UInstanceFragment_DurabilityState on the instance
UItemFragment_Stackable     MaxStackSize (overrides the definition's), bCanPartialStack (not read by code)
UItemFragment_Equipment     EquipmentSlotTag, EquipMesh, EquipSkeletalMesh, AnimLayerClass, AttachOffset (per-item fit),
                            GrantedAbilities, PassiveEffects, OnEquipEffects, StatModifiers (TArray<FCGFAttributeModifier>)
UItemFragment_WorldDisplay  WorldMesh, WorldMaterial, WorldScale, WorldRotation (the lying pose),
                            DropVFX and PickupSFX (not read by code)
UItemFragment_Key           KeyTag (Item.Key.*), bConsumeOnUnlock; Opens(LockTag)
UItemFragment_LightSource   Intensity, AttenuationRadius, Color, bCastShadows, LightOffset,
                            FuelBurnPerSecond, FuelStepSeconds; the fuel is the Durability fragment
UItemFragment_Placeable     ActorClass, PlaceDistance, MaxSlopeDegrees (AcceptsGround), PlacementOffsetZ, bConsumeOnPlace
```

**Keys.** A lock names the tag it accepts; a consumer walks the interactor's inventory (`ICGFInventoryInterface` →
`UInventoryComponent` slots → definition → fragment) for an item whose `Opens(LockTag)` is true and removes one when the
fragment says so. The dungeon boss door (VoxelWorldPOI) is the first lock.

**World pose.** `UItemFragment_WorldDisplay::WorldRotation` is the mesh-local rotation a world item takes when it lies
in the world (dropped or burst from a container); `AWorldItem` applies it with the mesh. A sword authored blade-up lies
flat with Roll = 90.

**Data-driven stats.** `UItemFragment_Equipment::StatModifiers` and `UItemFragment_Consumable::AttributeChanges` need no
gameplay-effect asset: equipment stats go through the project's stat-modifier effect class (EquipmentPlugin's
`UEquipmentGASSettings::StatModifierEffectClass`) and are removed on unequip; consumable changes are applied once
through a transient instant effect by whoever handles "use" (VoxelCharacterPlugin's `AVCCharacterBase::UseItemInSlot`).
The class-based `PassiveEffects` / `OnEquipEffects` / `ConsumeEffect` / `ConsumeAbility` remain for authored effects.
Scripts build modifiers with `CGFGameplayEffectStatics.make_attribute_modifier(attribute_set_class, "Defense", 3.0)`.

### Instance Fragments

`Types/ItemInstanceFragments.h`: `UInstanceFragment_DurabilityState` (`CurrentDurability`) and
`UInstanceFragment_CustomDataMap` (a string map; `SetValue` / `GetValue` / `HasKey`). Both serialize themselves for
storage (`SerializeFragment` / `DeserializeFragment`).

### UItemDatabaseSubsystem

`UGameInstanceSubsystem`, `Subsystems/ItemDatabaseSubsystem.h`:
- On initialisation scans the AssetManager for every `ItemDefinition` primary asset and loads each one (synchronously,
  once); `GetDefinition`, `HasDefinition`, `GetAllDefinitionIds` read that cache; `RegisterDefinition` adds one (tests).
- `CreateItemInstance(AssetId, StackCount)` is the only way to create runtime instances: a new GUID and copies of the
  definition's default instance fragments.
- `GiveItemByName(Inventory, Name, Count, bMatchSubstring, OutDefinition)` resolves a definition by asset name and adds
  it (the console commands and the debug tools).

---

## Inventory Component

`UInventoryComponent`, `Components/InventoryComponent.h`. One replicated component for players, NPCs and containers;
behaviour differs by configuration (`MaxSlots` 20, `MaxWeight` 0 = unlimited, `AllowedItemTags` (empty = all),
`DeniedItemTags`, `InventoryTypeTag` (carried, not read), `StorageOwnerId`, `bAutoRegisterStorage`).

### Slot Pre-Allocation

`BeginPlay` creates `MaxSlots` empty `FInventorySlot` entries in `InventorySlots` (`FInventorySlotArray`). The array never
grows or shrinks; operations fill and clear slots, so slot indices are stable.

### Operations

All return `EInventoryOperationResult`. On a client each sends its Server RPC and returns `Pending`.

**TryAddItem**
1. `ValidateAddItem`: valid item, tag filters, weight (`GetItemWeight` × `StackCount` against `MaxWeight`), and room for
   the whole quantity (space left in matching stacks, then the preferred slot, then other empty slots). Otherwise
   `InvalidItem`, `FilterRejected`, `WeightExceeded` or `InsufficientSpace`; nothing is added in part.
2. `Internal_AddItemStacked`: tops up matching stacks first, then fills empty slots, the preferred slot first.

**TryRemoveItem**: `Count == -1` removes the stack; a count below the stack decrements it; a count above it fails.

**TryMoveItem** (to another inventory): both sides are validated before either changes (the target's `ValidateAddItem`
on a copy, the source's `ValidateRemoveItem`); then the whole stack leaves the source and the copy is added to the
target, stacking where it can.

**TrySplitStack**: the stack must hold more than `SplitCount` and an empty slot must exist; the new stack gets a new GUID
and deep copies of the instance fragments.

**TryMergeStacks**: same definition, and the combined count must fit the maximum stack (`UItemFragment_Stackable`'s
`MaxStackSize` when present, else the definition's), else `StackFull`; no partial merge. The source slot is cleared.

**TrySwapSlots**: both indices in range; swaps the items and occupied flags (either slot may be empty).

Queries: `HasItem`, `GetItemCount`, `FindItemsByTag`, `GetItemInSlot`, `GetCurrentWeight` (computed on demand from the
definitions), `GetFilledSlotCount`, `CanAcceptItem`, `FindFirstEmptySlot`, `FindSlotIndexByInstanceId` (Blueprint) and
`FindSlotByInstanceId` (C++). Events: `OnItemAdded`, `OnItemRemoved`, `OnItemMoved`, `OnItemUpdated`,
`OnInventoryChanged`, and `OnOperationFailed` on a client whose request the server refused.

### Validation and Server RPCs

The server never trusts the client's check (code review decision D-CR16). Add and remove share const predicates,
`ValidateAddItem` and `ValidateRemoveItem` (filling `FInventoryOperationData`), which the `Try*` functions run on the
caller and the Server RPCs run again; move runs both predicates on both inventories. Split, merge and swap validate
inline, in the `Try*` function and again in their Server RPC. A refused RPC calls `ClientRPC_OperationFailed`, which
fires `OnOperationFailed`. Mutations happen only in the `Internal_*` functions, which mark the slot dirty for
replication, mark the component dirty for storage and broadcast the delegates.

On clients the slot array's replication callbacks (`PostReplicatedAdd` / `PostReplicatedChange` /
`PreReplicatedRemove`) raise the same delegates through the `TFunction` callbacks the component binds on
`FInventorySlotArray`.

---

## Loot Table System

`ULootTable` (`UDataAsset`, `Data/LootTable.h`): `DisplayName`, `Entries`, `RollCount`, `bAllowDuplicates`,
`GuaranteedEntries`. `FLootEntry` (`Data/LootEntry.h`): an `ItemDefinitionId` or a `NestedTable` (the nested table wins
when both are set), `Weight`, `DropChance`, `MinQuantity` / `MaxQuantity`, `RequiredContextTags` (all required),
`ExcludedContextTags` (none allowed), `MinLevel` / `MaxLevel` (0 = no bound).

`ULootGenerationSubsystem` (`UWorldSubsystem`): `GenerateLoot(Table, Context)` returns `TArray<FItemInstance>`,
`GenerateLootResult` an `FLootResult`. The context is passed by reference because its random stream advances.

### Generation Algorithm

```
GenerateLoot(Table, Context, Depth = 0):
    if Depth >= MaxRecursionDepth (5): warn, stop

    for Entry in Table.GuaranteedEntries passing the conditions:
        Resolve(Entry)

    Pool = Table.Entries passing the conditions
    repeat Table.RollCount times while Pool is not empty:
        Entry = weighted pick from Pool by Weight
        if Random() <= Entry.DropChance: Resolve(Entry)
        if !Table.bAllowDuplicates: remove Entry from Pool

Resolve(Entry):
    NestedTable set → GenerateLoot(NestedTable, Context, Depth + 1)   (the table loads synchronously)
    else            → CreateItemInstance(ItemDefinitionId, RandomRange(MinQuantity, MaxQuantity))

Conditions: Context.ContextTags has all RequiredContextTags and none of ExcludedContextTags;
            Context.Level within MinLevel / MaxLevel where those are non-zero
```

`FLootContext::RandomStream` is used when its initial seed is non-zero, so seeded rolls are deterministic (the dungeon
and surface loot pass a seed); seed 0 falls back to `FMath::FRand`. `PartySize` and `LuckModifier` are carried but not
read.

**Validation.** Editing a table (`PostEditChangeProperty`) clamps `MaxQuantity` up to `MinQuantity` and warns on entries
with neither or both of an item and a nested table. Cycles between tables are caught only by the depth cap at runtime.

---

## Crafting

- `UCraftingRecipe` (`Data/CraftingRecipe.h`, `UPrimaryDataAsset`, primary asset type `CraftingRecipe`: the project's
  AssetManager must scan it like `ItemDefinition`): `DisplayName`, `Ingredients` (`FCraftingIngredient`: `ItemId`,
  `Count`), `OutputItemId`, `OutputCount`, `RequiredStationTag` (`Crafting.Station.*`, empty = by hand), `SortOrder`.
- `UCraftingSubsystem` (`UGameInstanceSubsystem`): scans the recipes at initialisation; `GetAllRecipes`,
  `GetRecipesForStation(Tag)` (the hand recipes plus the station's), `FindRecipe` / `FindRecipeByName`,
  `CanCraft(Inventory, Recipe, StationTag)` → `ECraftResult` (`Success`, `InvalidRecipe`, `WrongStation`,
  `MissingIngredients`, `NoRoomForOutput`, `NotAuthority`, `NoDatabase`), and the authority-only `Craft(...)`: removes
  the ingredients across stacks (`RemoveItemsById`), creates the output through the item database and adds it, and puts
  the ingredients back when the output does not fit (`NoRoomForOutput`).
- The pure rules `StationSatisfies` and `CheckIngredients` are tested without a database (`ItemInventory.Crafting.*`).
- Callers validate the station (distance, tag) before calling `Craft`, inside a server RPC (VoxelCharacterPlugin's
  crafting and campsite UI).
- `UItemFragment_Placeable`: the character's use path traces the ground in front of the user, checks the slope
  (`AcceptsGround`) and spawns `ActorClass` on the authority (the campfire kit).

---

## Storage / Persistence

### UItemStorageSubsystem

`UGameInstanceSubsystem`, `Storage/ItemStorageSubsystem.h`. It creates the backend chosen by
`UItemSystemSettings::StorageBackend` (Project Settings > Plugins > Item System; Local by default) and keeps a list of
registered inventories:

- A component with a `StorageOwnerId` and `bAutoRegisterStorage` registers in `BeginPlay` and unregisters in `EndPlay`,
  which saves it when dirty.
- With `bAutoSaveEnabled`, a timer every `AutoSaveIntervalSeconds` (30 s) saves each dirty inventory through the
  backend's synchronous `SaveInventory` and clears its dirty flag.
- `SaveAll` runs at `Deinitialize`; `UInventoryComponent::ManualSave` / `ManualLoad` save or load one inventory.
- `GetStorageBackend()` hands the backend to the world save (VoxelWorldSave), which stores its documents as blobs.

### Serialization Format

`FItemSerializationUtils` (`Storage/ItemSerializationUtils.h`) writes every backend's JSON:

```json
{
    "version": 1,
    "ownerId": "Player_0",
    "slots": [
        {
            "slotIndex": 0,
            "bIsOccupied": true,
            "item": {
                "instanceId": "550e8400-e29b-41d4-a716-446655440000",
                "itemDefinitionId": "ItemDefinition:ID_Weapon_IronSword",
                "stackCount": 1,
                "instanceFragments": [
                    {
                        "fragmentClass": "/Script/ItemInventoryPlugin.InstanceFragment_DurabilityState",
                        "data": { "currentDurability": 73.0 }
                    }
                ]
            }
        }
    ]
}
```

Only valid items are written. A document whose `version` differs from the current one is refused on load (test
`ItemInventory.Serialization.Inventory.VersionMismatch`).

### ULocalItemStorage

- Inventories: `Saved/SaveGames/Inventories/<OwnerId>.json`.
- Blobs: `Saved/SaveGames/Worlds/<Key>.json`, the key sanitised to a file name.
- Every write goes to a `.tmp` file first and is then moved over the target, so a crash leaves the old file.
- `SaveInventoryAsync` serializes on the calling thread and writes on a background task; the callback returns on the
  game thread.

### URemoteItemStorage

- Endpoint: `UItemSystemSettings::RemoteEndpointURL`; an inventory is `POST` (save), `GET` (load) and `DELETE` at
  `<RemoteEndpointURL>/<OwnerId>`, a blob at `<RemoteEndpointURL>/blobs/<Key>` (fire-and-forget `POST` / `DELETE`;
  the synchronous blob load and `HasBlob` return false).
- `Authorization: Bearer <RemoteAuthToken>`; timeout `RemoteTimeoutSeconds` (10 s) per request; `RemoteMaxRetries`
  (3) retries with 1, 2, 4 s of backoff.
- The synchronous calls wait for the response on the calling thread. A starting point: real projects subclass or
  replace it for their backend.

---

## Editor Tools

**Item Database Browser** (`ItemSystemEditor`, `SItemDatabaseBrowser`): Tools > Item Database Browser in the level
editor. Lists every `ItemDefinition` primary asset with its display name, id, stack size, weight, value, fragment count
and tags, filtered by name; double-clicking a row opens the asset.

## Console Commands

`Inventory.Give <DefName> [Count=1]`, `Inventory.Clear`, `Inventory.List`, `Inventory.Recipes` (the recipes with the
player's ingredient counts) and `Loot.Roll <TableAssetPath> [Count=1]`, registered in `ItemInventoryPlugin.cpp`.
Arguments and defaults for every project command are in `Documentation/EDITOR_CONSOLE_GUIDE.md`.

## Tests

`Source/ItemInventoryPlugin/Private/Tests/`, under `ItemInventory.*`: `Operations.*` (add, remove, move across
inventories, split, merge, swap, failure cases), `Queries.*`, `Filtering.*`, `Types.*`, `Serialization.*`,
`Storage.Blob.LocalRoundTrip`, `ItemInventory.Loot.Distribution` (rolled counts against the entry weights), `Crafting.*`, `Tooltip.*`.

---

## UI Widgets

### Design Approach: Programmatic Widget Trees

Every widget in this plugin is built in C++ with a programmatic `WidgetTree`; no UMG Designer assets. Slot counts follow
runtime configuration, and Blueprint subclasses can still be swapped in through `TSubclassOf<>` properties on the
controller.

**Pattern:** each widget overrides `NativeOnInitialized()` and calls a private `BuildWidgetTree()`, which creates the
hierarchy with `WidgetTree->ConstructWidget<>()` and sets `WidgetTree->RootWidget`. Data binding (inventory component,
slot index) is a separate `Init*()` call after creation. Child user widgets (slots inside a hotbar) use
`CreateWidget<>()`, since they need their own widget tree lifecycle.

**Timing constraint:** build the tree in `NativeOnInitialized()`, never `NativeConstruct()`. `AddToViewport()` calls
`TakeWidget()` → `RebuildWidget()` before `NativeConstruct` fires, so a tree built there is empty.

UI widgets need `UMG`, `Slate`, `SlateCore` and `InputCore` (for `EKeys` in click handling).

### UInventorySlotWidget

**Path:** `Source/ItemInventoryPlugin/Public/UI/InventorySlotWidget.h`

One slot, used by the hotbar and the inventory panel.

```
USizeBox (SlotSize x SlotSize, default 64 px)
└── UOverlay
    ├── UImage (BackgroundImage) — Fill/Fill
    ├── UImage (IconImage) — Center/Center, async-loaded from UItemDefinition::Icon
    └── UTextBlock (StackCountText) — Right/Bottom, visible when StackCount > 1
```

`InitSlot(UInventoryComponent*, int32 SlotIndex)` binds the widget to a slot; it follows `OnItemAdded`,
`OnItemRemoved`, `OnItemUpdated` and `OnInventoryChanged`, filtered by its index. Icons load through
`UAssetManager::GetStreamableManager().RequestAsyncLoad()` with a placeholder tint meanwhile; in-flight loads are
cancelled on destruction. The slot shows a `UItemTooltipWidget` for its item.

| State | Background | Trigger |
|-------|-----------|---------|
| Default | `(0.08, 0.08, 0.12, 0.9)` | nothing selected or held |
| Selected | `(0.3, 0.5, 0.8, 0.9)` blue | `SetSelected(true)`: the active hotbar slot |
| Held | `(0.8, 0.6, 0.2, 0.9)` gold | `SetHeld(true)`: the click-to-move grabbed slot |

Held wins over selected. `NativeOnMouseButtonDown`: left click broadcasts `OnSlotClicked`, right click
`OnSlotRightClicked`, both `FOnInventorySlotClicked(int32 SlotIndex, UInventoryComponent* Inventory)`.

### UHotbarWidget

**Path:** `Source/ItemInventoryPlugin/Public/UI/HotbarWidget.h`

The always-visible row of slots `[0, NumSlots)` (default 9) at the bottom centre.

```
UBorder (dark, semi-transparent, 4 px padding)
└── UHorizontalBox
    └── [N] UInventorySlotWidget (CreateWidget in InitHotbar)
```

`InitHotbar(UInventoryComponent*, int32 NumSlots)` creates the slots and relays their click delegates on its own
`OnSlotClicked` / `OnSlotRightClicked`. `SetActiveSlot(int32)` moves the selection highlight; `SetSlotHeld(int32, bool)`
forwards to the slot. It is `HitTestInvisible` while the inventory is closed and `Visible` (clickable) while it is open.

### UInventoryPanelWidget

**Path:** `Source/ItemInventoryPlugin/Public/UI/InventoryPanelWidget.h`

The toggled grid of slots `[StartSlot, MaxSlots)`.

```
UBorder (dark background, configurable padding)
└── UVerticalBox
    ├── UTextBlock ("Inventory")
    └── UUniformGridPanel (configurable columns, default 5)
        └── [N] UInventorySlotWidget
```

`InitPanel(UInventoryComponent*, int32 StartSlot)` creates the slots and relays their delegates like the hotbar.
`SetSlotHeld(int32 InSlotIndex, bool)` converts the inventory index to the panel's (`InSlotIndex - CachedStartSlot`).

### UItemCursorWidget

**Path:** `Source/ItemInventoryPlugin/Public/UI/ItemCursorWidget.h`

The icon that follows the mouse while an item is held: a 48 x 48 `UImage` in a `USizeBox`, positioned in `NativeTick`
from `GetOwningPlayer()->GetMousePosition()` with an 8 px offset, `HitTestInvisible` so clicks reach the slots.
`ShowWithIcon(TSoftObjectPtr<UTexture2D>)` / `HideCursor()`. The controller creates it on first use at Z-order 100.

### UItemTooltipWidget

**Path:** `Source/ItemInventoryPlugin/Public/UI/ItemTooltipWidget.h`

The item's name, its stat lines and its description. `SetItem(Definition, Item)`; the pure `FormatItemStats` gives the
lines (weapon damage, speed and critical hits; attribute changes such as "+20 Max Health"; durability, or fuel time for a
light). Inventory and equipment slots create one as their tooltip and refresh it when their item or its durability
changes.

### Click-to-Move Interaction Flow

Items move between hotbar, panel and equipment by click. The state machine lives in VoxelCharacterPlugin's
`AVCPlayerController` (`VCPlayerController_Items.cpp`); the widgets and delegates live here.

```
UInventorySlotWidget::OnSlotClicked
  → UHotbarWidget / UInventoryPanelWidget::HandleChildSlotClicked (relay)
    → UHotbarWidget / UInventoryPanelWidget::OnSlotClicked (re-broadcast)
      → AVCPlayerController::OnSlotClickedFromUI (state machine)

Nothing held:  click an occupied slot → EnterHeldState (gold highlight, cursor icon); an empty slot → nothing
Item held:     the same slot → CancelHeldState; another slot → ExecuteSwapAndClearHeld (TrySwapSlots);
               right click → CancelHeldState; closing the inventory (HideInventoryPanels) → CancelHeldState
```

The controller binds the containers' delegates, never the individual slots', so it stays decoupled from slot widgets.
