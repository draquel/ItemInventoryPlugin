#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CraftingRecipe.generated.h"

/** One ingredient of a recipe: an item definition and how many of it are consumed. */
USTRUCT(BlueprintType)
struct ITEMINVENTORYPLUGIN_API FCraftingIngredient
{
	GENERATED_BODY()

	// EditAnywhere: struct instances inside a recipe asset (and editor Python) must be able to set these.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (AllowedTypes = "ItemDefinition"))
	FPrimaryAssetId ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting", meta = (ClampMin = "1"))
	int32 Count = 1;
};

/**
 * A crafting recipe (feature 8): ingredients in, one output stack out, optionally only at a station.
 *
 * Primary asset type "CraftingRecipe" (scanned by UCraftingSubsystem the way item definitions are;
 * the project's AssetManager must list the type). Recipes craft instantly; timed crafting and
 * skill gates are a consumer's business.
 */
UCLASS(BlueprintType)
class ITEMINVENTORYPLUGIN_API UCraftingRecipe : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shown in crafting lists (falls back to the output item's name when empty). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	FText DisplayName;

	/** What is consumed. Duplicated item ids are summed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	TArray<FCraftingIngredient> Ingredients;

	/** What is produced. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting", meta = (AllowedTypes = "ItemDefinition"))
	FPrimaryAssetId OutputItemId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting", meta = (ClampMin = "1"))
	int32 OutputCount = 1;

	/** Crafting.Station.* needed to craft this; empty = anywhere, by hand. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting", meta = (Categories = "Crafting.Station"))
	FGameplayTag RequiredStationTag;

	/** Ordering within crafting lists (lower first). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crafting")
	int32 SortOrder = 0;

	/** True when the recipe needs a station. */
	bool RequiresStation() const { return RequiredStationTag.IsValid(); }

	/** True when the recipe has an output and at least one ingredient. */
	bool IsUsable() const { return OutputItemId.IsValid() && OutputCount > 0 && Ingredients.Num() > 0; }

	// --- PrimaryDataAsset ---
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(FPrimaryAssetType(TEXT("CraftingRecipe")), GetFName());
	}
};
