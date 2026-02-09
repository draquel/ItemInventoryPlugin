#include "ItemInventoryPlugin.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "Components/InventoryComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Subsystems/LootGenerationSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/LootTable.h"
#include "Types/CGFItemTypes.h"
#include "Types/CGFLootTypes.h"

#define LOCTEXT_NAMESPACE "FItemInventoryPluginModule"

namespace
{
	UInventoryComponent* FindPlayerInventory(UWorld* World)
	{
		if (!World) return nullptr;
		APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
		if (!Pawn) return nullptr;
		return Pawn->FindComponentByClass<UInventoryComponent>();
	}
}

void FItemInventoryPluginModule::StartupModule()
{
	// Inventory.Give <DefName> [Count=1]
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommandWithWorldAndArgs>(
		TEXT("Inventory.Give"),
		TEXT("Give an item to the player's inventory. Usage: Inventory.Give <DefName> [Count=1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Inventory.Give: Usage: Inventory.Give <DefName> [Count=1]"));
					return;
				}

				UInventoryComponent* Inventory = FindPlayerInventory(World);
				if (!Inventory)
				{
					UE_LOG(LogTemp, Warning, TEXT("Inventory.Give: No player inventory found."));
					return;
				}

				UGameInstance* GI = World->GetGameInstance();
				if (!GI) return;
				UItemDatabaseSubsystem* DB = GI->GetSubsystem<UItemDatabaseSubsystem>();
				if (!DB)
				{
					UE_LOG(LogTemp, Warning, TEXT("Inventory.Give: ItemDatabaseSubsystem not available."));
					return;
				}

				const FString& DefName = Args[0];
				int32 Count = 1;
				if (Args.Num() >= 2)
				{
					Count = FCString::Atoi(*Args[1]);
					if (Count < 1) Count = 1;
				}

				FPrimaryAssetId AssetId(FPrimaryAssetType("ItemDefinition"), FName(*DefName));
				FItemInstance Item = DB->CreateItemInstance(AssetId, Count);
				if (!Item.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("Inventory.Give: Failed to create item '%s'. Definition not found."), *DefName);
					return;
				}

				EInventoryOperationResult Result = Inventory->TryAddItem(Item);
				UE_LOG(LogTemp, Log, TEXT("Inventory.Give: '%s' x%d -> %s"),
					*DefName, Count,
					Result == EInventoryOperationResult::Success ? TEXT("Success") : TEXT("Failed"));
			})
	));

	// Inventory.Clear
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommandWithWorldAndArgs>(
		TEXT("Inventory.Clear"),
		TEXT("Clear all items from the player's inventory."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UInventoryComponent* Inventory = FindPlayerInventory(World);
				if (!Inventory)
				{
					UE_LOG(LogTemp, Warning, TEXT("Inventory.Clear: No player inventory found."));
					return;
				}

				TArray<FGuid> ItemIds;
				for (const FInventorySlot& Slot : Inventory->InventorySlots.Items)
				{
					if (Slot.bIsOccupied)
					{
						ItemIds.Add(Slot.Item.InstanceId);
					}
				}

				int32 Cleared = 0;
				for (const FGuid& Id : ItemIds)
				{
					if (Inventory->TryRemoveItem(Id) == EInventoryOperationResult::Success)
					{
						Cleared++;
					}
				}

				UE_LOG(LogTemp, Log, TEXT("Inventory.Clear: Removed %d item(s)."), Cleared);
			})
	));

	// Inventory.List
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommandWithWorldAndArgs>(
		TEXT("Inventory.List"),
		TEXT("List all items in the player's inventory."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UInventoryComponent* Inventory = FindPlayerInventory(World);
				if (!Inventory)
				{
					UE_LOG(LogTemp, Warning, TEXT("Inventory.List: No player inventory found."));
					return;
				}

				UGameInstance* GI = World->GetGameInstance();
				UItemDatabaseSubsystem* DB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;

				UE_LOG(LogTemp, Log, TEXT("=== Inventory (%d/%d slots) ==="),
					Inventory->GetFilledSlotCount(), Inventory->MaxSlots);

				for (const FInventorySlot& Slot : Inventory->InventorySlots.Items)
				{
					if (!Slot.bIsOccupied) continue;

					FString ItemName = Slot.Item.ItemDefinitionId.ToString();
					if (DB)
					{
						UItemDefinition* Def = DB->GetDefinition(Slot.Item.ItemDefinitionId);
						if (Def)
						{
							ItemName = Def->DisplayName.ToString();
						}
					}

					int32 FragmentCount = Slot.Item.InstanceFragments.Num();
					UE_LOG(LogTemp, Log, TEXT("  [%d] %s  x%d  (%d fragment(s))"),
						Slot.SlotIndex, *ItemName, Slot.Item.StackCount, FragmentCount);
				}
			})
	));

	// Loot.Roll <TablePath> [Count=1]
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommandWithWorldAndArgs>(
		TEXT("Loot.Roll"),
		TEXT("Roll a loot table. Usage: Loot.Roll <TableAssetPath> [Count=1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Loot.Roll: Usage: Loot.Roll <TableAssetPath> [Count=1]"));
					return;
				}

				const FString& TablePath = Args[0];
				int32 Count = 1;
				if (Args.Num() >= 2)
				{
					Count = FCString::Atoi(*Args[1]);
					if (Count < 1) Count = 1;
				}

				ULootTable* Table = Cast<ULootTable>(StaticLoadObject(ULootTable::StaticClass(), nullptr, *TablePath));
				if (!Table)
				{
					UE_LOG(LogTemp, Warning, TEXT("Loot.Roll: Failed to load loot table at '%s'."), *TablePath);
					return;
				}

				ULootGenerationSubsystem* LootSys = World->GetSubsystem<ULootGenerationSubsystem>();
				if (!LootSys)
				{
					UE_LOG(LogTemp, Warning, TEXT("Loot.Roll: LootGenerationSubsystem not available."));
					return;
				}

				UE_LOG(LogTemp, Log, TEXT("=== Loot.Roll: %s x%d ==="), *TablePath, Count);

				for (int32 i = 0; i < Count; ++i)
				{
					FLootContext Context;
					TArray<FItemInstance> Items = LootSys->GenerateLoot(Table, Context);
					UE_LOG(LogTemp, Log, TEXT("  Roll %d: %d item(s)"), i + 1, Items.Num());
					for (const FItemInstance& Item : Items)
					{
						UE_LOG(LogTemp, Log, TEXT("    - %s x%d"),
							*Item.ItemDefinitionId.ToString(), Item.StackCount);
					}
				}
			})
	));
}

void FItemInventoryPluginModule::ShutdownModule()
{
	ConsoleCommands.Empty();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FItemInventoryPluginModule, ItemInventoryPlugin)
