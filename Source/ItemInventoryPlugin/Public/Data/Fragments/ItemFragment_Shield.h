#pragma once

#include "CoreMinimal.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Shield.generated.h"

/**
 * A shield: what its guard takes from a hit. The game's block reads it from the item held in the off hand; the numbers are
 * the shield's, the rules (when a guard is up, the stamina it has) the game's.
 */
UCLASS(BlueprintType, DisplayName = "Shield")
class ITEMINVENTORYPLUGIN_API UItemFragment_Shield : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** The share of a blocked hit's damage the guard takes (0 = none, 1 = all of it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0", ClampMax = "1"))
	float BlockDamageReduction = 0.8f;

	/** How far to either side of the holder's facing a hit's source may be and still be blocked (degrees). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0", ClampMax = "180"))
	float BlockHalfAngle = 60.f;

	/** Stamina a blocked hit costs per point of damage the guard takes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0"))
	float StaminaPerBlockedDamage = 1.f;
};
