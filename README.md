# ItemInventoryPlugin

Core item definition, inventory management, loot table generation, and persistence system for Unreal Engine 5.8. Server-authoritative with full multiplayer replication.

## What This Plugin Does

ItemInventoryPlugin is the backbone of the Game Framework suite. It provides everything needed to define items, store them in inventories, generate randomized loot, and persist inventory state across sessions.

### Item Definitions

Items are defined as `UPrimaryDataAsset` instances with a **fragment composition** model. Instead of deep class hierarchies, item behavior is assembled from reusable fragments:

```
Iron Sword = ItemDefinition + Fragment_Weapon + Fragment_Equipment 
             + Fragment_Durability + Fragment_WorldDisplay

Health Potion = ItemDefinition + Fragment_Consumable + Fragment_Stackable 
                + Fragment_WorldDisplay
```

Built-in definition fragments: Weapon, Consumable, Durability, Stackable, Equipment, WorldDisplay. Create custom fragments by subclassing `UItemDefinitionFragment`.

### Inventory Component

A single replicated component (`UInventoryComponent`) that works on players, NPCs, chests, loot drops, and vendors — behavioral differences come from configuration, not subclasses.

Features:
- Slot-based storage with stable indices
- Weight and tag-based filtering
- Atomic operations: add, remove, move, split, merge, swap
- All operations return result enums for clear success/failure handling
- `FFastArraySerializer` for efficient per-element network replication
- Server-authoritative with client RPC requests
- Event delegates for UI and game system binding

### Loot Tables

Data-driven loot generation with:
- Weighted random selection
- Independent drop chance per entry
- Min/max quantity ranges
- Nested/referenced tables for compositional loot design
- Context conditions (level, tags, exclusions)
- Guaranteed drops alongside random rolls
- Deterministic seeding for testing

### Persistence

Swappable storage backends behind a common interface:
- **Local storage** — JSON files via SaveGame system
- **Remote storage** — HTTP client for database/backend integration
- Auto-save with dirty tracking
- Backend selected via project settings

## Requirements

- Unreal Engine 5.8
- [CommonGameFramework](../CommonGameFramework/) plugin
- GameplayAbilities plugin (enabled in .uproject)

## Installation

Clone into your project's `Plugins/` directory:

```bash
git clone <repo-url> Plugins/ItemInventoryPlugin
```

Ensure CommonGameFramework is also present in `Plugins/`.

Add to your module's `Build.cs`:

```csharp
PublicDependencyModuleNames.Add("ItemSystem");
```

## Module Dependencies

```
ItemSystem (Runtime)
├── CommonGameFramework
├── Core, CoreUObject, Engine, NetCore
├── GameplayTags, GameplayAbilities, GameplayTasks
└── HTTP, Json, JsonUtilities  (for storage)

ItemSystemEditor (Editor)
├── ItemSystem
├── UnrealEd, Slate, SlateCore
└── PropertyEditor
```

## Plugin Structure

```
ItemInventoryPlugin/
├── Source/
│   ├── ItemSystem/                    Runtime module
│   │   ├── Public/
│   │   │   ├── Data/                  Item definitions, fragments, loot tables
│   │   │   ├── Components/            UInventoryComponent
│   │   │   ├── Subsystems/            Item database, loot generation
│   │   │   ├── Storage/               Local and remote persistence
│   │   │   └── Types/                 Operation results, FastArray wrapper
│   │   └── Private/
│   └── ItemSystemEditor/              Editor module
│       └── Public/                    Loot table preview, item browser
├── Content/                           Example item definitions
├── Documentation/
│   └── ITEM_INVENTORY_SYSTEM.md       Detailed system design
└── .claude/
    └── instructions.md                Claude Code implementation instructions
```

## Quick Start

### Define an Item

Create a new `UItemDefinition` data asset in the Content Browser. Add fragments to define its behavior:

```
Content/Items/ID_Sword_Iron
├── DisplayName: "Iron Sword"
├── ItemTags: Item.Category.Weapon, Item.Rarity.Common
├── MaxStackSize: 1
├── Weight: 3.5
├── Fragments:
│   ├── Fragment_Weapon { BaseDamage=25, AttackSpeed=1.2 }
│   ├── Fragment_Equipment { Slot=Equipment.Slot.MainHand, Mesh=SM_Sword_Iron }
│   ├── Fragment_Durability { MaxDurability=100 }
│   └── Fragment_WorldDisplay { WorldMesh=SM_Sword_Iron_World }
└── DefaultInstanceFragments:
    └── InstanceFragment_DurabilityState { CurrentDurability=100 }
```

### Add an Inventory to an Actor

Attach `UInventoryComponent` to any actor in the editor or in C++:

```cpp
UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
TObjectPtr<UInventoryComponent> InventoryComponent;

// In constructor
InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
InventoryComponent->MaxSlots = 20;
InventoryComponent->MaxWeight = 100.0f;
```

### Inventory Operations

```cpp
// Add an item (server-authoritative)
UItemDatabaseSubsystem* ItemDB = GetGameInstance()->GetSubsystem<UItemDatabaseSubsystem>();
FItemInstance Sword = ItemDB->CreateItemInstance(SwordAssetId, 1);
EInventoryOperationResult Result = InventoryComponent->TryAddItem(Sword);

// Query
bool HasWood = InventoryComponent->HasItem(WoodAssetId, 5);
int32 WoodCount = InventoryComponent->GetItemCount(WoodAssetId);
TArray<FItemInstance> Weapons = InventoryComponent->FindItemsByTag(WeaponTags);

// Move between inventories (atomic)
InventoryComponent->TryMoveItem(ItemGuid, ChestInventory, -1);

// Bind to events (for UI)
InventoryComponent->OnItemAdded.AddDynamic(this, &AMyActor::HandleItemAdded);
```

### Generate Loot

```cpp
ULootGenerationSubsystem* LootSys = GetWorld()->GetSubsystem<ULootGenerationSubsystem>();

FLootContext Context;
Context.Level = PlayerLevel;
Context.ContextTags.AddTag(BiomeTag);

TArray<FItemInstance> Loot = LootSys->GenerateLoot(BossLootTable, Context);
for (const FItemInstance& Item : Loot)
{
    ChestInventory->TryAddItem(Item);
}
```

## Blueprint Support

All public API functions are exposed to Blueprint with categories under `Inventory|Operations`, `Inventory|Query`, `Inventory|Events`, and `Loot`. Item definitions and loot tables are editor-friendly data assets with full property customization.

## Related Plugins

| Plugin | Integration |
|--------|------------|
| [CommonGameFramework](../CommonGameFramework/) | Foundation types and interfaces (required) |
| [InteractionPlugin](../InteractionPlugin/) | World item pickup/drop, loot container interaction |
| [EquipmentPlugin](../EquipmentPlugin/) | Equipping items from inventory, stat application |

## Documentation

- [ITEM_INVENTORY_SYSTEM.md](Documentation/ITEM_INVENTORY_SYSTEM.md) — Fragment architecture, inventory operation algorithms, loot generation, storage format
- [ARCHITECTURE.md](../CommonGameFramework/Documentation/ARCHITECTURE.md) — System-wide architecture and data flow

## License

[Your license here]
