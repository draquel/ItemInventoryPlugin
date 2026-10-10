// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Components/InventoryComponent.h"
#include "Data/CraftingRecipe.h"
#include "Data/Fragments/ItemFragment_Placeable.h"
#include "Subsystems/CraftingSubsystem.h"
#include "Types/CGFItemTypes.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Crafting (feature 8): the pure rules — station match, ingredient check and
// ingredient removal — need no item database. The craft itself (output through
// the database, roll-back) runs in PIE.
// ---------------------------------------------------------------------------

namespace CraftingTestHelpers
{
	UInventoryComponent* CreateInventory(int32 Slots = 10)
	{
		UInventoryComponent* Comp = NewObject<UInventoryComponent>();
		Comp->AddToRoot();
		Comp->MaxSlots = Slots;
		Comp->MaxWeight = 0.0f;
		Comp->InventorySlots.Items.SetNum(Slots);
		for (int32 i = 0; i < Slots; ++i)
		{
			Comp->InventorySlots.Items[i].SlotIndex = i;
			Comp->InventorySlots.Items[i].bIsOccupied = false;
		}
		Comp->InventorySlots.OwningComponent = Comp;
		return Comp;
	}

	FPrimaryAssetId Id(const TCHAR* Name) { return FPrimaryAssetId(TEXT("ItemDefinition"), Name); }

	void Place(UInventoryComponent* Comp, const TCHAR* Name, int32 Stack, int32 Slot)
	{
		FItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.ItemDefinitionId = Id(Name);
		Item.StackCount = Stack;
		FInventorySlot& S = Comp->InventorySlots.Items[Slot];
		S.Item = Item;
		S.bIsOccupied = true;
	}

	UCraftingRecipe* Torch()
	{
		UCraftingRecipe* Recipe = NewObject<UCraftingRecipe>();
		Recipe->AddToRoot();
		FCraftingIngredient Wood;
		Wood.ItemId = Id(TEXT("ID_Material_Wood"));
		Wood.Count = 2;
		Recipe->Ingredients.Add(Wood);
		Recipe->OutputItemId = Id(TEXT("ID_Tool_Torch"));
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrafting_StationRule, "Crafting.StationRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCrafting_StationRule::RunTest(const FString& Parameters)
{
	const FGameplayTag Campfire = FGameplayTag::RequestGameplayTag(TEXT("Crafting.Station.Campfire"), false);
	TestTrue(TEXT("Hand recipe crafts anywhere"), UCraftingSubsystem::StationSatisfies(FGameplayTag(), FGameplayTag()));
	TestTrue(TEXT("Hand recipe crafts at a station too"), UCraftingSubsystem::StationSatisfies(FGameplayTag(), Campfire));
	TestFalse(TEXT("Station recipe needs the station"), UCraftingSubsystem::StationSatisfies(Campfire, FGameplayTag()));
	TestTrue(TEXT("Station recipe at its station"), UCraftingSubsystem::StationSatisfies(Campfire, Campfire));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrafting_Ingredients, "Crafting.Ingredients",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCrafting_Ingredients::RunTest(const FString& Parameters)
{
	using namespace CraftingTestHelpers;
	UInventoryComponent* Inv = CreateInventory();
	UCraftingRecipe* Recipe = Torch();

	TestEqual(TEXT("Empty inventory is short"), UCraftingSubsystem::CheckIngredients(Inv, Recipe), ECraftResult::MissingIngredients);
	Place(Inv, TEXT("ID_Material_Wood"), 1, 0);
	TestEqual(TEXT("One wood is still short"), UCraftingSubsystem::CheckIngredients(Inv, Recipe), ECraftResult::MissingIngredients);
	Place(Inv, TEXT("ID_Material_Wood"), 1, 3);
	TestEqual(TEXT("Two wood across stacks is enough"), UCraftingSubsystem::CheckIngredients(Inv, Recipe), ECraftResult::Success);

	// Removal spans stacks and reports what it took.
	TestEqual(TEXT("Removed two"), UCraftingSubsystem::RemoveItemsById(Inv, Id(TEXT("ID_Material_Wood")), 2), 2);
	TestEqual(TEXT("None left"), Inv->GetItemCount(Id(TEXT("ID_Material_Wood"))), 0);
	TestEqual(TEXT("Short again"), UCraftingSubsystem::CheckIngredients(Inv, Recipe), ECraftResult::MissingIngredients);

	// Unusable recipes are rejected before anything else.
	UCraftingRecipe* Empty = NewObject<UCraftingRecipe>();
	TestEqual(TEXT("Recipe without ingredients is invalid"), UCraftingSubsystem::CheckIngredients(Inv, Empty), ECraftResult::InvalidRecipe);
	TestEqual(TEXT("Null recipe is invalid"), UCraftingSubsystem::CheckIngredients(Inv, nullptr), ECraftResult::InvalidRecipe);

	Recipe->RemoveFromRoot();
	Inv->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrafting_PlaceableSlope, "Crafting.PlaceableSlope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCrafting_PlaceableSlope::RunTest(const FString& Parameters)
{
	UItemFragment_Placeable* Frag = NewObject<UItemFragment_Placeable>();
	Frag->MaxSlopeDegrees = 35.0f;
	TestTrue(TEXT("Flat ground accepted"), Frag->AcceptsGround(FVector::UpVector));
	TestTrue(TEXT("Gentle slope accepted"), Frag->AcceptsGround(FVector(0.3f, 0.f, 0.954f).GetSafeNormal()));
	TestFalse(TEXT("Steep slope rejected"), Frag->AcceptsGround(FVector(0.8f, 0.f, 0.6f).GetSafeNormal()));
	TestFalse(TEXT("Ceiling rejected"), Frag->AcceptsGround(-FVector::UpVector));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
