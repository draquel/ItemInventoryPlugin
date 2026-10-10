#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CraftingSubsystem.generated.h"

class UCraftingRecipe;
class UInventoryComponent;

/** Outcome of a craft attempt or check. */
UENUM(BlueprintType)
enum class ECraftResult : uint8
{
	Success,
	/** Null or unusable recipe (no output / no ingredients). */
	InvalidRecipe,
	/** The recipe needs a station the crafter is not at. */
	WrongStation,
	/** One or more ingredients are short. */
	MissingIngredients,
	/** The output did not fit in the inventory (ingredients were put back). */
	NoRoomForOutput,
	/** Craft called off the authority. */
	NotAuthority,
	/** No item database / inventory to work with. */
	NoDatabase,
};

/**
 * Recipe registry + the craft operation (feature 8). Game-instance scoped like the item database:
 * scans primary assets of type "CraftingRecipe" at start, answers which recipes a station offers
 * and whether an inventory can craft one, and performs the craft on the authority.
 *
 * The craft itself is: remove the ingredients, create the output through the item database, add
 * it; if the output does not fit, the ingredients go back. Consumers (the character) wrap it in a
 * server RPC and validate the station (distance, tag) before calling.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API UCraftingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Every registered recipe, sorted by SortOrder then name. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Crafting")
	TArray<UCraftingRecipe*> GetAllRecipes() const;

	/**
	 * Recipes craftable at a station: hand recipes (no station) plus those whose station the given
	 * tag satisfies. An empty StationTag lists hand recipes only.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Crafting")
	TArray<UCraftingRecipe*> GetRecipesForStation(FGameplayTag StationTag) const;

	/** Find a recipe by its primary asset id (type CraftingRecipe). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Crafting")
	UCraftingRecipe* FindRecipe(FPrimaryAssetId RecipeId) const;

	/** Find a recipe by asset name (CR_Torch). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Crafting")
	UCraftingRecipe* FindRecipeByName(FName AssetName) const;

	/** Register a recipe created at runtime (tests, procedural content). */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void RegisterRecipe(UCraftingRecipe* Recipe);

	/** Everything short of mutating: recipe valid, station satisfied, ingredients present. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Crafting")
	ECraftResult CanCraft(const UInventoryComponent* Inventory, const UCraftingRecipe* Recipe, FGameplayTag StationTag) const;

	/**
	 * Authority: craft the recipe from / into the inventory. Removes the ingredients, creates the
	 * output through the item database and adds it; puts the ingredients back when the output
	 * does not fit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	ECraftResult Craft(UInventoryComponent* Inventory, const UCraftingRecipe* Recipe, FGameplayTag StationTag);

	// --- Pure rules (tested) ---

	/** Does a station tag satisfy a recipe's requirement (exact or child; empty requirement = always). */
	static bool StationSatisfies(const FGameplayTag& RequiredStation, const FGameplayTag& StationTag);

	/** Success or MissingIngredients for this inventory (InvalidRecipe for a bad recipe). Needs no database. */
	static ECraftResult CheckIngredients(const UInventoryComponent* Inventory, const UCraftingRecipe* Recipe);

	/**
	 * Remove up to Count items of a definition across the inventory's stacks.
	 * @return How many were removed.
	 */
	static int32 RemoveItemsById(UInventoryComponent* Inventory, FPrimaryAssetId ItemId, int32 Count);

private:
	void ScanForRecipes();

	UPROPERTY()
	TMap<FPrimaryAssetId, TObjectPtr<UCraftingRecipe>> RecipeCache;
};
