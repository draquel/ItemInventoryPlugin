#include "Misc/AutomationTest.h"
#include "Data/ItemDefinition.h"
#include "Data/LootTable.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Subsystems/LootGenerationSubsystem.h"
#include "Testing/CGFTestGameInstance.h"
#include "Types/CGFItemTypes.h"
#include "Types/CGFLootTypes.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Loot distribution (code review CR2, D-CR23 / FW-34): a three-entry table rolled 10,000 times gives each entry a share
// within 2 percentage points of its weight's share. Programmatic definitions in a standalone game instance's item database,
// a seeded stream, one roll per table so every roll picks one entry.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootDistributionTest,
	"ItemInventory.Loot.Distribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLootDistributionTest::RunTest(const FString& Parameters)
{
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	ULootGenerationSubsystem* Loot = Game.GetWorldSubsystem<ULootGenerationSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database) || !TestNotNull(TEXT("The loot subsystem"), Loot))
	{
		return true;
	}

	// Three definitions with weights 1, 2 and 7: shares of 10%, 20% and 70%.
	const float Weights[3] = { 1.0f, 2.0f, 7.0f };
	const float TotalWeight = 10.0f;
	ULootTable* Table = NewObject<ULootTable>(GetTransientPackage());
	Table->AddToRoot();
	Table->RollCount = 1;
	Table->bAllowDuplicates = true;
	TArray<FPrimaryAssetId> Ids;
	for (int32 i = 0; i < 3; ++i)
	{
		UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), *FString::Printf(TEXT("LootDistributionTestItem%d"), i));
		Definition->AddToRoot();
		Definition->MaxStackSize = 99;
		const FPrimaryAssetId Id = Definition->GetPrimaryAssetId();
		Database->RegisterDefinition(Id, Definition);
		Ids.Add(Id);
		FLootEntry Entry;
		Entry.ItemDefinitionId = Id;
		Entry.Weight = Weights[i];
		Entry.DropChance = 1.0f;
		Entry.MinQuantity = 1;
		Entry.MaxQuantity = 1;
		Table->Entries.Add(Entry);
	}

	constexpr int32 Rolls = 10000;
	FLootContext Context;
	Context.Level = 1;
	Context.RandomStream.Initialize(1234); // a seeded stream: the same distribution every run
	int32 Counts[3] = { 0, 0, 0 };
	int32 Empty = 0, Several = 0;
	for (int32 Roll = 0; Roll < Rolls; ++Roll)
	{
		const TArray<FItemInstance> Items = Loot->GenerateLoot(Table, Context);
		if (Items.Num() != 1)
		{
			Empty += Items.Num() == 0 ? 1 : 0;
			Several += Items.Num() > 1 ? 1 : 0;
			continue;
		}
		const int32 Index = Ids.IndexOfByKey(Items[0].ItemDefinitionId);
		if (Index != INDEX_NONE)
		{
			++Counts[Index];
		}
	}

	TestEqual(TEXT("Every roll gives one item"), Empty + Several, 0);
	for (int32 i = 0; i < 3; ++i)
	{
		const float Share = 100.0f * Counts[i] / Rolls;
		const float Expected = 100.0f * Weights[i] / TotalWeight;
		AddInfo(FString::Printf(TEXT("entry %d (weight %.0f): %d of %d, %.2f%% against %.0f%%"), i, Weights[i], Counts[i], Rolls, Share, Expected));
		TestTrue(*FString::Printf(TEXT("Entry %d's share is within 2 points of its weight's"), i), FMath::Abs(Share - Expected) <= 2.0f);
	}

	for (const FPrimaryAssetId& Id : Ids)
	{
		if (UItemDefinition* Definition = Database->GetDefinition(Id))
		{
			Definition->RemoveFromRoot();
		}
	}
	Table->RemoveFromRoot();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
