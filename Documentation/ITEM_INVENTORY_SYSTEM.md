# Item & Inventory System — Detailed Design

## Overview

This document covers the internal design of the ItemInventoryPlugin: item definitions, the fragment system, inventory component mechanics, loot table generation, and the storage/persistence layer. For how this plugin integrates with InteractionPlugin and EquipmentPlugin, see `Plugins/CommonGameFramework/Documentation/ARCHITECTURE.md`.

---

## Item Definition System

### UItemDefinition

Each unique item type in the game is represented by exactly one `UItemDefinition` primary data asset. Definitions are immutable at runtime — they describe what an item *is*, not the state of a specific instance.

**Asset organization convention:**
```
Content/Items/
├── Weapons/
│   ├── ID_Sword_Iron.uasset
│   ├── ID_Bow_Hunting.uasset
│   └── ID_Staff_Fire.uasset
├── Armor/
│   ├── ID_Helm_Iron.uasset
│   └── ID_Chest_Leather.uasset
├── Consumables/
│   ├── ID_Potion_Health.uasset
│   └── ID_Food_Bread.uasset
└── Materials/
    ├── ID_Mat_Wood.uasset
    └── ID_Mat_IronOre.uasset
```

**Naming convention:** `ID_[Category]_[Name]` for asset names. The `FPrimaryAssetId` auto-derives from the class type and asset name: `"ItemDefinition:ID_Sword_Iron"`.

### Fragment Composition

Instead of subclassing `UItemDefinition`, item behavior is composed via fragments:

| Item Type | Fragments |
|-----------|-----------|
| Iron Sword | `Fragment_Weapon` + `Fragment_Equipment` + `Fragment_Durability` + `Fragment_WorldDisplay` |
| Health Potion | `Fragment_Consumable` + `Fragment_Stackable` + `Fragment_WorldDisplay` |
| Iron Ore | `Fragment_Stackable` + `Fragment_WorldDisplay` |
| Quest Scroll | `Fragment_WorldDisplay` (no other gameplay fragments) |

**Fragment lookup is templated for C++ and tag-based for Blueprint:**

```cpp
// C++
auto* WeaponFrag = ItemDef->FindFragment<UItemFragment_Weapon>();

// Blueprint uses FindFragmentByClass node
```

### Built-In Definition Fragments

**UItemFragment_Weapon**
```
BaseDamage: float
AttackSpeed: float
DamageType: FGameplayTag
CritChance: float
CritMultiplier: float
```

**UItemFragment_Consumable**
```
ConsumeEffect: TSubclassOf<UGameplayEffect>   // Applied on use
ConsumeAbility: TSubclassOf<UGameplayAbility>  // Triggered on use (alternative to effect)
bConsumeOnUse: bool                            // Destroy after use?
CooldownDuration: float
```

**UItemFragment_Durability**
```
MaxDurability: float
bDestroyAtZero: bool       // Remove item when durability reaches 0?
DegradeRate: float          // Per-use degradation amount
```
Pairs with `UInstanceFragment_DurabilityState` on runtime instances.

**UItemFragment_Stackable**
```
MaxStackSize: int32         // Overrides UItemDefinition.MaxStackSize if present
bCanPartialStack: bool      // Allow split/merge operations
```

**UItemFragment_Equipment**
```
EquipmentSlotTag: FGameplayTag
EquipMesh: TSoftObjectPtr<UStaticMesh>
EquipSkeletalMesh: TSoftObjectPtr<USkeletalMesh>
AnimLayerClass: TSubclassOf<UAnimInstance>
GrantedAbilities: TArray<TSubclassOf<UGameplayAbility>>
PassiveEffects: TArray<TSubclassOf<UGameplayEffect>>
OnEquipEffects: TArray<TSubclassOf<UGameplayEffect>>
```

**UItemFragment_WorldDisplay**
```
WorldMesh: TSoftObjectPtr<UStaticMesh>
WorldMaterial: TSoftObjectPtr<UMaterialInterface>     // Override material (optional)
WorldScale: FVector (default 1,1,1)
DropVFX: TSoftObjectPtr<UNiagaraSystem>
PickupSFX: TSoftObjectPtr<USoundBase>
```

### UItemDatabaseSubsystem

`UGameInstanceSubsystem` — lives for the entire game session. Responsibilities:
- Scans the asset registry for all `UItemDefinition` assets on initialization
- Provides synchronous lookup by `FPrimaryAssetId` (returns cached pointer)
- Handles async loading for definitions not yet in memory
- `CreateItemInstance(FPrimaryAssetId, int32 Count)` — the ONLY way to create runtime item instances; generates GUID, copies default instance fragments

---

## Inventory Component

### Slot Pre-Allocation

During `BeginPlay`, the inventory creates `MaxSlots` empty `FInventorySlot` entries. This array never grows or shrinks at runtime. Operations fill empty slots and clear occupied slots.

```
BeginPlay:
  for (int32 i = 0; i < MaxSlots; ++i)
      Slot[i] = { Index=i, bIsOccupied=false, Item=Invalid }
```

### Operation Semantics

**TryAddItem** — Algorithm:
1. Validate item (IsValid, tag filters, weight check)
2. If item is stackable and matching stacks exist: try to fill existing stacks first
3. For remaining quantity: find empty slots (prefer `PreferredSlot`, then first empty)
4. If all quantity placed: Success
5. If partial (some placed, no room for rest): rollback, return InsufficientSpace
6. The "no partial adds" rule ensures inventory never gets into an inconsistent state

**TryRemoveItem** — Algorithm:
1. Find slot(s) containing the specified `InstanceId`
2. If `Count == -1`: remove entire stack
3. If `Count < StackCount`: decrement stack, item stays in slot
4. If `Count == StackCount`: clear slot entirely
5. Fire OnItemRemoved

**TryMoveItem** (cross-inventory) — Algorithm:
1. Validate source has the item
2. Validate target can accept (space, weight, tags)
3. Copy item data
4. Remove from source
5. Add to target
6. If target add fails (shouldn't happen after validation, but safety): re-add to source, log error
7. Fire events on both inventories

**TrySplitStack** — Algorithm:
1. Validate source slot has a stackable item with `StackCount > SplitCount`
2. Find empty slot for new stack
3. Decrement source stack by `SplitCount`
4. Create new FItemInstance with new GUID, same definition, `StackCount = SplitCount`
5. Copy instance fragments from source
6. Place in empty slot

**TryMergeStacks** — Algorithm:
1. Validate both instances exist and have the same `ItemDefinitionId`
2. Check combined count doesn't exceed `MaxStackSize`
3. Add source count to target count
4. Remove source slot
5. If combined would exceed max: reject (don't partial merge — keep it simple)

**TrySwapSlots** — Algorithm:
1. Validate both slot indices are in range
2. Swap items between slots (simple data swap)
3. Handle case where one or both slots are empty

### Weight Calculation

```cpp
float UInventoryComponent::GetCurrentWeight() const
{
    float TotalWeight = 0.0f;
    for (const FInventorySlot& Slot : InventorySlots.Items)
    {
        if (Slot.bIsOccupied)
        {
            UItemDefinition* Def = ItemDatabaseSubsystem->GetDefinition(Slot.Item.ItemDefinitionId);
            if (Def)
            {
                TotalWeight += Def->Weight * Slot.Item.StackCount;
            }
        }
    }
    return TotalWeight;
}
```

Weight is calculated on-demand, not cached, because it changes frequently and is only needed for validation checks. If profiling shows this is hot, add a cached weight with dirty tracking.

---

## Loot Table System

### Generation Algorithm

```
GenerateLoot(Table, Context):
    Results = []

    // Phase 1: Guaranteed drops
    for Entry in Table.GuaranteedEntries:
        if PassesConditions(Entry, Context):
            Results += ResolveEntry(Entry, Context)

    // Phase 2: Weighted random rolls
    EligiblePool = Table.Entries.Filter(e => PassesConditions(e, Context))
    for i in range(Table.RollCount):
        Entry = WeightedRandom(EligiblePool)
        if Random() <= Entry.DropChance:    // Independent probability check
            Results += ResolveEntry(Entry, Context)
        if !Table.bAllowDuplicates:
            EligiblePool.Remove(Entry)

    return Results

ResolveEntry(Entry, Context):
    if Entry.NestedTable:
        return GenerateLoot(Entry.NestedTable, Context)  // Recursive, depth-capped
    else:
        Count = Random(Entry.MinQuantity, Entry.MaxQuantity)
        Instance = ItemDatabase.CreateItemInstance(Entry.ItemDefinitionId, Count)
        return [Instance]

PassesConditions(Entry, Context):
    if Entry.RequiredContextTags and !Context.ContextTags.HasAll(Entry.RequiredContextTags):
        return false
    if Entry.ExcludedContextTags and Context.ContextTags.HasAny(Entry.ExcludedContextTags):
        return false
    if Entry.MinLevel > 0 and Context.Level < Entry.MinLevel:
        return false
    if Entry.MaxLevel > 0 and Context.Level > Entry.MaxLevel:
        return false
    return true

WeightedRandom(Pool):
    TotalWeight = Sum(e.Weight for e in Pool)
    Roll = Random(0, TotalWeight)
    RunningWeight = 0
    for Entry in Pool:
        RunningWeight += Entry.Weight
        if Roll <= RunningWeight:
            return Entry
```

### Nested Table Depth Cap

Maximum recursion depth: **5**. If a nested table references another nested table and so on, generation stops at depth 5 and logs a warning. Circular references (Table A → Table B → Table A) are detected at edit time via `PostEditChangeProperty` validation and at runtime via a visited-set check.

### Deterministic Testing

`FLootContext` contains an `FRandomStream`. If seeded (non-zero), the loot generation subsystem uses it instead of `FMath::FRand`. This allows deterministic unit tests that verify exact distributions.

---

## Storage / Persistence

### Serialization Format

All storage implementations use the same JSON structure:

```json
{
    "version": 1,
    "ownerId": "Player_0",
    "inventoryType": "Inventory.Type.Player",
    "maxSlots": 20,
    "slots": [
        {
            "slotIndex": 0,
            "bIsOccupied": true,
            "item": {
                "instanceId": "550e8400-e29b-41d4-a716-446655440000",
                "itemDefinitionId": "ItemDefinition:ID_Sword_Iron",
                "stackCount": 1,
                "instanceFragments": [
                    {
                        "fragmentClass": "/Script/ItemSystem.InstanceFragment_DurabilityState",
                        "data": {
                            "currentDurability": 73.0
                        }
                    }
                ]
            }
        }
    ]
}
```

**Version field** exists for future migration. If the format changes, storage implementations can detect the version and migrate old saves.

### ULocalItemStorage

- Save path: `[ProjectSavedDir]/SaveGames/Inventories/{OwnerId}.json`
- Write: `FFileHelper::SaveStringToFile` with JSON string
- Read: `FFileHelper::LoadFileToString` then `FJsonSerializer::Deserialize`
- Atomic writes: write to `.tmp` file first, then rename (prevents corruption on crash)
- Async wrapper: delegates to game thread task for API consistency

### URemoteItemStorage

- Endpoint configurable via `UItemSystemSettings` (UDeveloperSettings)
- Save: `POST /api/inventory/{ownerId}` with JSON body
- Load: `GET /api/inventory/{ownerId}`
- Delete: `DELETE /api/inventory/{ownerId}`
- Auth: Bearer token from settings, injected as header
- Retry: 3 attempts with exponential backoff (1s, 2s, 4s)
- Timeout: 10 seconds per request
- Error handling: log failure, fire callback with `bSuccess = false`, never crash

### Auto-Save System

The `UItemStorageSubsystem` (GameInstanceSubsystem) manages auto-saving:

```
Timer fires every 30 seconds (configurable):
    for each registered InventoryComponent:
        if component.bIsDirty:
            StorageImpl->SaveInventoryAsync(ownerId, items, callback)
            component.bIsDirty = false

Also triggers on:
    - PreStreamingLevel (level transition)
    - Player disconnect (EndPlay for player controller)
    - Explicit call: StorageSubsystem->SaveAll()
```

Inventory components register themselves with the storage subsystem during `BeginPlay` and unregister during `EndPlay`.

---

## Editor Tools

### Loot Table Preview (ItemSystemEditor Module)

An editor utility widget that allows designers to:
1. Select a loot table asset
2. Configure a mock FLootContext (level, tags, luck)
3. Set number of simulation rolls (default 1000)
4. Run simulation
5. View results:
   - Distribution bar chart showing item frequencies
   - Expected vs actual drop rates
   - Guaranteed items flagged separately
   - Nested table resolution chain visualization

### Item Database Browser

An editor utility that shows:
- All registered UItemDefinition assets in a searchable/filterable list
- Per-item detail panel showing all fragments
- Tag-based filtering (show all weapons, show all rare+ items)
- Validation warnings (missing required tags, fragments without data, etc.)
