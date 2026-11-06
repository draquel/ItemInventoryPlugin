#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_RangedWeapon.generated.h"

/**
 * A weapon that shoots: what it shoots, how fast, and how its draw builds power (a bow; later a crossbow or a sling).
 * Its damage, damage type and crits stay on the Weapon fragment, scaled by the draw's power. The game supplies the
 * projectile class and the attack that reads this.
 */
UCLASS(BlueprintType, DisplayName = "Ranged Weapon")
class ITEMINVENTORYPLUGIN_API UItemFragment_RangedWeapon : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** What it shoots: the tag its ammo items carry (Item.Ammo.Arrow). Empty = it needs no ammo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged", meta = (Categories = "Item.Ammo"))
	FGameplayTag AmmoTag;

	/** The projectile it launches (an actor class the game provides). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged")
	TSubclassOf<AActor> ProjectileClass;

	/** Launch speed at full power (uu/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged", meta = (ClampMin = "0"))
	float LaunchSpeed = 6000.f;

	/** The shortest draw that shoots (seconds); a shorter one is no shot and uses no ammo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged", meta = (ClampMin = "0"))
	float MinDrawSeconds = 0.25f;

	/** The draw that reaches full power (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged", meta = (ClampMin = "0"))
	float FullDrawSeconds = 1.f;

	/** The power at the shortest draw, as a fraction of full (speed and damage both scale by it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged", meta = (ClampMin = "0", ClampMax = "1"))
	float MinDrawPower = 0.3f;

	/**
	 * The power of a draw: 0 below the shortest draw, MinDrawPower at it, rising linearly to 1 at FullDrawSeconds and
	 * staying there.
	 * @param DrawSeconds How long the weapon was drawn
	 * @return The power, 0 to 1
	 */
	UFUNCTION(BlueprintPure, Category = "Ranged")
	float GetDrawPower(float DrawSeconds) const
	{
		if (DrawSeconds < MinDrawSeconds)
		{
			return 0.f;
		}
		const float Span = FullDrawSeconds - MinDrawSeconds;
		const float Alpha = Span > UE_KINDA_SMALL_NUMBER ? FMath::Clamp((DrawSeconds - MinDrawSeconds) / Span, 0.f, 1.f) : 1.f;
		return FMath::Lerp(FMath::Clamp(MinDrawPower, 0.f, 1.f), 1.f, Alpha);
	}
};
