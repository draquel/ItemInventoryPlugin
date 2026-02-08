#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Consumable.generated.h"

UCLASS(BlueprintType, DisplayName = "Consumable")
class ITEMINVENTORYPLUGIN_API UItemFragment_Consumable : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** Gameplay effect applied when consumed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	TSubclassOf<UGameplayEffect> ConsumeEffect;

	/** Gameplay ability triggered on use (alternative to effect) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	TSubclassOf<UGameplayAbility> ConsumeAbility;

	/** Destroy item after use? */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	bool bConsumeOnUse = true;

	/** Cooldown between uses (seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	float CooldownDuration = 0.0f;
};
