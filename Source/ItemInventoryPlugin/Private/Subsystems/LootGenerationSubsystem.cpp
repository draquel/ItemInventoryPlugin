#include "Subsystems/LootGenerationSubsystem.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/LootTable.h"
#include "Data/LootEntry.h"

// ===========================================================================
// Public API
// ===========================================================================

TArray<FItemInstance> ULootGenerationSubsystem::GenerateLoot(ULootTable* Table, FLootContext& Context)
{
	TArray<FItemInstance> Results;

	if (!Table)
	{
		return Results;
	}

	GenerateLootInternal(Table, Context, Results, 0);
	return Results;
}

FLootResult ULootGenerationSubsystem::GenerateLootResult(ULootTable* Table, FLootContext& Context)
{
	FLootResult Result;
	Result.Items = GenerateLoot(Table, Context);
	Result.bSuccess = Result.Items.Num() > 0;
	return Result;
}

// ===========================================================================
// Internal Generation
// ===========================================================================

void ULootGenerationSubsystem::GenerateLootInternal(ULootTable* Table, FLootContext& Context,
	TArray<FItemInstance>& OutItems, int32 Depth)
{
	if (!Table || Depth >= MaxRecursionDepth)
	{
		if (Depth >= MaxRecursionDepth)
		{
			UE_LOG(LogTemp, Warning, TEXT("LootGenerationSubsystem: Max recursion depth (%d) reached."),
				MaxRecursionDepth);
		}
		return;
	}

	// Phase 1: Guaranteed drops
	for (const FLootEntry& Entry : Table->GuaranteedEntries)
	{
		if (PassesConditions(Entry, Context))
		{
			ResolveEntry(Entry, Context, OutItems, Depth);
		}
	}

	// Phase 2: Build eligible pool from weighted entries
	TArray<int32> EligibleIndices;
	for (int32 i = 0; i < Table->Entries.Num(); ++i)
	{
		if (PassesConditions(Table->Entries[i], Context))
		{
			EligibleIndices.Add(i);
		}
	}

	if (EligibleIndices.Num() == 0)
	{
		return;
	}

	// Phase 3: Roll
	for (int32 Roll = 0; Roll < Table->RollCount; ++Roll)
	{
		if (EligibleIndices.Num() == 0)
		{
			break;
		}

		int32 SelectedIndex = WeightedRandom(Table->Entries, EligibleIndices, Context);
		if (SelectedIndex == INDEX_NONE)
		{
			continue;
		}

		const FLootEntry& Entry = Table->Entries[SelectedIndex];

		// Independent drop chance check
		if (GetRandom(Context) <= Entry.DropChance)
		{
			ResolveEntry(Entry, Context, OutItems, Depth);
		}

		// Remove from pool if duplicates not allowed
		if (!Table->bAllowDuplicates)
		{
			EligibleIndices.Remove(SelectedIndex);
		}
	}
}

void ULootGenerationSubsystem::ResolveEntry(const FLootEntry& Entry, FLootContext& Context,
	TArray<FItemInstance>& OutItems, int32 Depth)
{
	// Nested table takes priority
	if (Entry.IsNestedTable())
	{
		ULootTable* Nested = Entry.NestedTable.LoadSynchronous();
		if (Nested)
		{
			GenerateLootInternal(Nested, Context, OutItems, Depth + 1);
		}
		return;
	}

	// Direct item
	if (!Entry.ItemDefinitionId.IsValid())
	{
		return;
	}

	UItemDatabaseSubsystem* DB = GetItemDatabase();
	if (!DB)
	{
		return;
	}

	int32 Quantity = GetRandomRange(Context, Entry.MinQuantity, Entry.MaxQuantity);
	FItemInstance Instance = DB->CreateItemInstance(Entry.ItemDefinitionId, Quantity);
	if (Instance.IsValid())
	{
		OutItems.Add(Instance);
	}
}

// ===========================================================================
// Conditions
// ===========================================================================

bool ULootGenerationSubsystem::PassesConditions(const FLootEntry& Entry, const FLootContext& Context) const
{
	// Required tags: context must have ALL
	if (Entry.RequiredContextTags.Num() > 0 && !Context.ContextTags.HasAll(Entry.RequiredContextTags))
	{
		return false;
	}

	// Excluded tags: context must have NONE
	if (Entry.ExcludedContextTags.Num() > 0 && Context.ContextTags.HasAny(Entry.ExcludedContextTags))
	{
		return false;
	}

	// Level range
	if (Entry.MinLevel > 0 && Context.Level < Entry.MinLevel)
	{
		return false;
	}

	if (Entry.MaxLevel > 0 && Context.Level > Entry.MaxLevel)
	{
		return false;
	}

	return true;
}

// ===========================================================================
// Random Selection
// ===========================================================================

int32 ULootGenerationSubsystem::WeightedRandom(const TArray<FLootEntry>& Pool,
	const TArray<int32>& EligibleIndices, FLootContext& Context) const
{
	if (EligibleIndices.Num() == 0)
	{
		return INDEX_NONE;
	}

	// Sum weights of eligible entries
	float TotalWeight = 0.0f;
	for (int32 Index : EligibleIndices)
	{
		TotalWeight += Pool[Index].Weight;
	}

	if (TotalWeight <= 0.0f)
	{
		return INDEX_NONE;
	}

	float Roll = GetRandom(Context) * TotalWeight;
	float RunningWeight = 0.0f;

	for (int32 Index : EligibleIndices)
	{
		RunningWeight += Pool[Index].Weight;
		if (Roll <= RunningWeight)
		{
			return Index;
		}
	}

	// Fallback (floating point edge case)
	return EligibleIndices.Last();
}

float ULootGenerationSubsystem::GetRandom(FLootContext& Context) const
{
	if (Context.RandomStream.GetInitialSeed() != 0)
	{
		return Context.RandomStream.FRand();
	}
	return FMath::FRand();
}

int32 ULootGenerationSubsystem::GetRandomRange(FLootContext& Context, int32 Min, int32 Max) const
{
	if (Min == Max)
	{
		return Min;
	}

	if (Context.RandomStream.GetInitialSeed() != 0)
	{
		return Context.RandomStream.RandRange(Min, Max);
	}
	return FMath::RandRange(Min, Max);
}

// ===========================================================================
// Helpers
// ===========================================================================

UItemDatabaseSubsystem* ULootGenerationSubsystem::GetItemDatabase() const
{
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
}
