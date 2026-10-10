#include "Subsystems/CraftingSubsystem.h"
#include "Components/InventoryComponent.h"
#include "Data/CraftingRecipe.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"

void UCraftingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ScanForRecipes();
}

void UCraftingSubsystem::ScanForRecipes()
{
	UAssetManager& AssetManager = UAssetManager::Get();
	const FPrimaryAssetType RecipeType(TEXT("CraftingRecipe"));

	TArray<FPrimaryAssetId> AssetIds;
	AssetManager.GetPrimaryAssetIdList(RecipeType, AssetIds);
	for (const FPrimaryAssetId& AssetId : AssetIds)
	{
		const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		if (UCraftingRecipe* Recipe = AssetPath.IsValid() ? Cast<UCraftingRecipe>(AssetPath.TryLoad()) : nullptr)
		{
			RecipeCache.Add(AssetId, Recipe);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("CraftingSubsystem: scanned %d recipe(s)"), RecipeCache.Num());
}

TArray<UCraftingRecipe*> UCraftingSubsystem::GetAllRecipes() const
{
	TArray<UCraftingRecipe*> Out;
	Out.Reserve(RecipeCache.Num());
	for (const TPair<FPrimaryAssetId, TObjectPtr<UCraftingRecipe>>& Pair : RecipeCache)
	{
		if (Pair.Value)
		{
			Out.Add(Pair.Value);
		}
	}
	Out.Sort([](const UCraftingRecipe& A, const UCraftingRecipe& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder : A.GetFName().LexicalLess(B.GetFName());
	});
	return Out;
}

TArray<UCraftingRecipe*> UCraftingSubsystem::GetRecipesForStation(FGameplayTag StationTag) const
{
	TArray<UCraftingRecipe*> Out = GetAllRecipes();
	Out.RemoveAll([&](const UCraftingRecipe* Recipe)
	{
		return !StationSatisfies(Recipe->RequiredStationTag, StationTag);
	});
	return Out;
}

UCraftingRecipe* UCraftingSubsystem::FindRecipe(FPrimaryAssetId RecipeId) const
{
	const TObjectPtr<UCraftingRecipe>* Found = RecipeCache.Find(RecipeId);
	return Found ? Found->Get() : nullptr;
}

UCraftingRecipe* UCraftingSubsystem::FindRecipeByName(FName AssetName) const
{
	return FindRecipe(FPrimaryAssetId(FPrimaryAssetType(TEXT("CraftingRecipe")), AssetName));
}

void UCraftingSubsystem::RegisterRecipe(UCraftingRecipe* Recipe)
{
	if (Recipe)
	{
		RecipeCache.Add(Recipe->GetPrimaryAssetId(), Recipe);
	}
}

bool UCraftingSubsystem::StationSatisfies(const FGameplayTag& RequiredStation, const FGameplayTag& StationTag)
{
	if (!RequiredStation.IsValid())
	{
		return true; // by hand: any place
	}
	return StationTag.IsValid() && StationTag.MatchesTag(RequiredStation);
}

ECraftResult UCraftingSubsystem::CheckIngredients(const UInventoryComponent* Inventory, const UCraftingRecipe* Recipe)
{
	if (!Recipe || !Recipe->IsUsable())
	{
		return ECraftResult::InvalidRecipe;
	}
	if (!Inventory)
	{
		return ECraftResult::NoDatabase;
	}
	// Sum duplicated ingredient lines before comparing against the inventory.
	TMap<FPrimaryAssetId, int32> Needed;
	for (const FCraftingIngredient& Ingredient : Recipe->Ingredients)
	{
		if (Ingredient.ItemId.IsValid() && Ingredient.Count > 0)
		{
			Needed.FindOrAdd(Ingredient.ItemId) += Ingredient.Count;
		}
	}
	for (const TPair<FPrimaryAssetId, int32>& Need : Needed)
	{
		if (Inventory->GetItemCount(Need.Key) < Need.Value)
		{
			return ECraftResult::MissingIngredients;
		}
	}
	return ECraftResult::Success;
}

int32 UCraftingSubsystem::RemoveItemsById(UInventoryComponent* Inventory, FPrimaryAssetId ItemId, int32 Count)
{
	if (!Inventory || !ItemId.IsValid() || Count <= 0)
	{
		return 0;
	}
	int32 Removed = 0;
	for (int32 SlotIndex = 0; SlotIndex < Inventory->MaxSlots && Removed < Count; ++SlotIndex)
	{
		const FItemInstance Item = Inventory->GetItemInSlot(SlotIndex);
		if (!Item.IsValid() || Item.ItemDefinitionId != ItemId)
		{
			continue;
		}
		const int32 Take = FMath::Min(Item.StackCount, Count - Removed);
		if (Inventory->TryRemoveItem(Item.InstanceId, Take) == EInventoryOperationResult::Success)
		{
			Removed += Take;
		}
	}
	return Removed;
}

ECraftResult UCraftingSubsystem::CanCraft(const UInventoryComponent* Inventory, const UCraftingRecipe* Recipe, FGameplayTag StationTag) const
{
	if (!Recipe || !Recipe->IsUsable())
	{
		return ECraftResult::InvalidRecipe;
	}
	if (!StationSatisfies(Recipe->RequiredStationTag, StationTag))
	{
		return ECraftResult::WrongStation;
	}
	return CheckIngredients(Inventory, Recipe);
}

ECraftResult UCraftingSubsystem::Craft(UInventoryComponent* Inventory, const UCraftingRecipe* Recipe, FGameplayTag StationTag)
{
	const ECraftResult Check = CanCraft(Inventory, Recipe, StationTag);
	if (Check != ECraftResult::Success)
	{
		return Check;
	}
	const AActor* Owner = Inventory->GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		return ECraftResult::NotAuthority;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	if (!ItemDB || !ItemDB->HasDefinition(Recipe->OutputItemId))
	{
		return ECraftResult::NoDatabase;
	}

	// Take the ingredients first so the output never overlaps them in a nearly full inventory.
	TMap<FPrimaryAssetId, int32> Taken;
	for (const FCraftingIngredient& Ingredient : Recipe->Ingredients)
	{
		if (Ingredient.ItemId.IsValid() && Ingredient.Count > 0)
		{
			Taken.FindOrAdd(Ingredient.ItemId) += RemoveItemsById(Inventory, Ingredient.ItemId, Ingredient.Count);
		}
	}

	FItemInstance Output = ItemDB->CreateItemInstance(Recipe->OutputItemId, Recipe->OutputCount);
	if (Output.IsValid() && Inventory->TryAddItem(Output) == EInventoryOperationResult::Success)
	{
		return ECraftResult::Success;
	}

	// No room: give the ingredients back.
	for (const TPair<FPrimaryAssetId, int32>& Pair : Taken)
	{
		if (Pair.Value > 0)
		{
			Inventory->TryAddItem(ItemDB->CreateItemInstance(Pair.Key, Pair.Value));
		}
	}
	return ECraftResult::NoRoomForOutput;
}
