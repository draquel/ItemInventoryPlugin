// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "UI/ItemTooltipWidget.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Durability.h"
#include "Data/Fragments/ItemFragment_LightSource.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
#include "Types/ItemInstanceFragments.h"
#include "Types/CGFItemTypes.h"

#if WITH_AUTOMATION_TESTS

// QA refinement R8: tooltips show an item's stats and wear (weapon durability was invisible; QA F5-07 was blocked).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemTooltip_AttributeNames, "Inventory.Tooltip.AttributeNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FItemTooltip_AttributeNames::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("MaxHealth"), UItemTooltipWidget::PrettyAttributeName(TEXT("MaxHealth")), FString(TEXT("Max Health")));
	TestEqual(TEXT("AttackPower"), UItemTooltipWidget::PrettyAttributeName(TEXT("AttackPower")), FString(TEXT("Attack Power")));
	TestEqual(TEXT("Defense"), UItemTooltipWidget::PrettyAttributeName(TEXT("Defense")), FString(TEXT("Defense")));
	TestEqual(TEXT("an acronym stays together"), UItemTooltipWidget::PrettyAttributeName(TEXT("MaxHPBonus")), FString(TEXT("Max HP Bonus")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemTooltip_FormatsStats, "Inventory.Tooltip.FormatsStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FItemTooltip_FormatsStats::RunTest(const FString& Parameters)
{
	// A worn iron sword: damage, speed, crit and current durability from the instance.
	UItemDefinition* Sword = NewObject<UItemDefinition>();
	UItemFragment_Weapon* Weapon = NewObject<UItemFragment_Weapon>(Sword);
	Weapon->BaseDamage = 18.f;
	Weapon->AttackSpeed = 1.f;
	Weapon->CritChance = 0.05f;
	Weapon->CritMultiplier = 2.f;
	UItemFragment_Durability* Durability = NewObject<UItemFragment_Durability>(Sword);
	Durability->MaxDurability = 50.f;
	Sword->Fragments.Add(Weapon);
	Sword->Fragments.Add(Durability);

	FItemInstance Worn;
	UInstanceFragment_DurabilityState* State = NewObject<UInstanceFragment_DurabilityState>();
	State->CurrentDurability = 37.f;
	Worn.InstanceFragments.Add(State);

	const FString SwordText = UItemTooltipWidget::FormatItemStats(Sword, Worn).ToString();
	TestTrue(TEXT("shows damage"), SwordText.Contains(TEXT("Damage 18")));
	TestTrue(TEXT("shows attack speed"), SwordText.Contains(TEXT("Attack speed 1.0")));
	TestTrue(TEXT("shows crit"), SwordText.Contains(TEXT("Critical 5% (x2)")));
	TestTrue(TEXT("shows the instance's durability"), SwordText.Contains(TEXT("Durability 37 / 50")));

	// A torch: durability is fuel seconds.
	UItemDefinition* Torch = NewObject<UItemDefinition>();
	UItemFragment_Durability* Fuel = NewObject<UItemFragment_Durability>(Torch);
	Fuel->MaxDurability = 600.f;
	Torch->Fragments.Add(Fuel);
	Torch->Fragments.Add(NewObject<UItemFragment_LightSource>(Torch));
	FItemInstance Burning;
	UInstanceFragment_DurabilityState* FuelState = NewObject<UInstanceFragment_DurabilityState>();
	FuelState->CurrentDurability = 570.f;
	Burning.InstanceFragments.Add(FuelState);
	TestTrue(TEXT("a light shows fuel time"), UItemTooltipWidget::FormatItemStats(Torch, Burning).ToString().Contains(TEXT("Fuel 9:30 / 10:00")));

	TestTrue(TEXT("no definition, no stats"), UItemTooltipWidget::FormatItemStats(nullptr, Worn).IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
