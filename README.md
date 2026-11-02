# ItemInventoryPlugin

Items, inventories, loot and persistence for Unreal Engine 5.8, server-authoritative with per-slot replication. Item
types are `UItemDefinition` data assets composed from fragments (weapon, equipment, durability, light source, key,
placeable, ...); `UInventoryComponent` holds `FItemInstance` stacks in stable slots and replicates them with
`FFastArraySerializer`; `ULootTable` rolls weighted, nested, context-filtered loot; `UCraftingRecipe` and
`UCraftingSubsystem` craft by hand or at a station; local JSON or remote HTTP storage saves inventories and the world
save's documents. Includes code-built inventory widgets and an editor Item Database Browser. Requires CommonGameFramework
(`Plugins/CommonGameFramework/`) and the GameplayAbilities plugin.

- [ITEM_INVENTORY_SYSTEM.md](Documentation/ITEM_INVENTORY_SYSTEM.md) — the system design
- [.claude/instructions.md](.claude/instructions.md) — rules for working in this plugin
- `Plugins/CommonGameFramework/Documentation/ARCHITECTURE.md` — how the framework plugins fit together
