#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Types/CGFCombatTypes.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Equipment.generated.h"

UCLASS(BlueprintType, DisplayName = "Equipment")
class ITEMINVENTORYPLUGIN_API UItemFragment_Equipment : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FGameplayTag EquipmentSlotTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	TSoftObjectPtr<UStaticMesh> EquipMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	TSoftObjectPtr<USkeletalMesh> EquipSkeletalMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	TSubclassOf<UAnimInstance> AnimLayerClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> GrantedAbilities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Effects")
	TArray<TSubclassOf<UGameplayEffect>> PassiveEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Effects")
	TArray<TSubclassOf<UGameplayEffect>> OnEquipEffects;

	/**
	 * Data-driven lasting stats while equipped ("+3 Defense", "+10 MaxHealth"), no effect asset
	 * needed. Applied through the project's stat-modifier effect class (see
	 * UEquipmentGASSettings::StatModifierEffectClass) and removed on unequip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Stats")
	TArray<FCGFAttributeModifier> StatModifiers;
};
