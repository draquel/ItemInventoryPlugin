#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Weapon.generated.h"

UCLASS(BlueprintType, DisplayName = "Weapon")
class ITEMINVENTORYPLUGIN_API UItemFragment_Weapon : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	float BaseDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	float AttackSpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTag DamageType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	float CritChance = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	float CritMultiplier = 2.0f;
};
