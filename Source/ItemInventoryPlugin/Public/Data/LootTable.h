#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/LootEntry.h"
#include "LootTable.generated.h"

/**
 * Data asset defining a loot table with weighted random entries,
 * guaranteed drops, and configurable roll count.
 * Create instances in the editor Content Browser via right-click → Miscellaneous → Data Asset → LootTable.
 */
UCLASS(BlueprintType)
class ITEMINVENTORYPLUGIN_API ULootTable : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Display name for editor identification */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
	FText DisplayName;

	/** Weighted random entries — rolled RollCount times */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
	TArray<FLootEntry> Entries;

	/** How many times to roll on this table */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table", meta = (ClampMin = "1"))
	int32 RollCount = 1;

	/** If true, the same entry can be selected multiple times across rolls */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
	bool bAllowDuplicates = true;

	/** Guaranteed drops — always included regardless of rolls, still subject to conditions */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot Table")
	TArray<FLootEntry> GuaranteedEntries;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
