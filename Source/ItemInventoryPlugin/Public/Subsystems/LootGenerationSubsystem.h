#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/CGFLootTypes.h"
#include "LootGenerationSubsystem.generated.h"

class ULootTable;
struct FLootEntry;
class UItemDatabaseSubsystem;

/**
 * World subsystem that generates loot from loot tables.
 * Supports weighted random selection, nested tables (depth-capped),
 * context-based conditions, and deterministic testing via FRandomStream.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API ULootGenerationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Maximum nested table recursion depth */
	static constexpr int32 MaxRecursionDepth = 5;

	/**
	 * Generate loot from a loot table with the given context.
	 * @param Table   The loot table to roll on
	 * @param Context Generation parameters (level, tags, luck, random seed)
	 * @return Array of generated item instances
	 */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	TArray<FItemInstance> GenerateLoot(ULootTable* Table, UPARAM(ref) FLootContext& Context);

	/**
	 * Generate a full FLootResult (includes success flag).
	 */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	FLootResult GenerateLootResult(ULootTable* Table, UPARAM(ref) FLootContext& Context);

private:
	/** Internal recursive generation with depth tracking */
	void GenerateLootInternal(ULootTable* Table, FLootContext& Context,
		TArray<FItemInstance>& OutItems, int32 Depth);

	/** Resolve a single entry into item instances */
	void ResolveEntry(const FLootEntry& Entry, FLootContext& Context,
		TArray<FItemInstance>& OutItems, int32 Depth);

	/** Check if an entry's conditions are met by the context */
	bool PassesConditions(const FLootEntry& Entry, const FLootContext& Context) const;

	/** Select a random entry from an eligible pool using weights */
	int32 WeightedRandom(const TArray<FLootEntry>& Pool, const TArray<int32>& EligibleIndices,
		FLootContext& Context) const;

	/** Get a random float [0, 1) from context stream or FMath */
	float GetRandom(FLootContext& Context) const;

	/** Get a random int in [Min, Max] from context stream or FMath */
	int32 GetRandomRange(FLootContext& Context, int32 Min, int32 Max) const;

	UItemDatabaseSubsystem* GetItemDatabase() const;
};
