#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "LootEntry.generated.h"

class ULootTable;

/**
 * A single entry in a loot table. Can reference a direct item or a nested loot table.
 * Supports weighted random selection, independent drop chance, quantity ranges,
 * and context-based conditions (level, tags).
 */
USTRUCT(BlueprintType)
struct ITEMINVENTORYPLUGIN_API FLootEntry
{
	GENERATED_BODY()

	// -----------------------------------------------------------------------
	// What to drop (one or the other)
	// -----------------------------------------------------------------------

	/** Direct item definition reference */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FPrimaryAssetId ItemDefinitionId;

	/** Nested loot table to roll on instead of a direct item */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TSoftObjectPtr<ULootTable> NestedTable;

	// -----------------------------------------------------------------------
	// Probability
	// -----------------------------------------------------------------------

	/** Relative weight in the weighted random pool */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Independent drop chance after being selected (1.0 = always drops if selected) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 1.0f;

	// -----------------------------------------------------------------------
	// Quantity
	// -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MinQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MaxQuantity = 1;

	// -----------------------------------------------------------------------
	// Conditions
	// -----------------------------------------------------------------------

	/** Context must have ALL of these tags for this entry to be eligible */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Conditions")
	FGameplayTagContainer RequiredContextTags;

	/** Context must have NONE of these tags for this entry to be eligible */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Conditions")
	FGameplayTagContainer ExcludedContextTags;

	/** Minimum context level (0 = no minimum) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Conditions", meta = (ClampMin = "0"))
	int32 MinLevel = 0;

	/** Maximum context level (0 = no maximum) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Conditions", meta = (ClampMin = "0"))
	int32 MaxLevel = 0;

	// -----------------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------------

	/** Returns true if this entry references a nested table rather than a direct item */
	bool IsNestedTable() const { return !NestedTable.IsNull(); }

	/** Returns true if this entry has a valid item or nested table reference */
	bool IsValid() const { return ItemDefinitionId.IsValid() || !NestedTable.IsNull(); }
};
