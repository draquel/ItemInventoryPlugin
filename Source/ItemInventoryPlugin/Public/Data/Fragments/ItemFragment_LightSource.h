#pragma once

#include "CoreMinimal.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_LightSource.generated.h"

/**
 * Marks an item as a carried light source (feature 7: light as a mechanic): a torch, a lantern.
 *
 * While the item is equipped the equipment plugin hangs a point light with these values off the
 * held visual. Fuel is the item's Durability fragment (MaxDurability = burn seconds, bDestroyAtZero
 * = burns out and is consumed); the equipment manager spends it in FuelStepSeconds steps so the
 * replicated durability changes a few times a minute, not every frame. An item with this fragment
 * and no Durability fragment burns forever.
 */
UCLASS(BlueprintType, DisplayName = "Light Source")
class ITEMINVENTORYPLUGIN_API UItemFragment_LightSource : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** Point-light intensity in lumens while lit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "0.0"))
	float Intensity = 1200.0f;

	/** Point-light attenuation radius (world units). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "50.0"))
	float AttenuationRadius = 900.0f;

	/** Light colour (warm flame by default). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	FLinearColor Color = FLinearColor(1.0f, 0.72f, 0.45f);

	/** Shadow-casting light. Off by default: a moving shadow caster is the expensive kind. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	bool bCastShadows = false;

	/** Light position relative to the held mesh (the flame, not the handle). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	FVector LightOffset = FVector(0.0f, 0.0f, 45.0f);

	/** Fuel (durability points) burned per second while lit. 0 = never burns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light|Fuel", meta = (ClampMin = "0.0"))
	float FuelBurnPerSecond = 1.0f;

	/** Seconds between fuel deductions (coarser = fewer replicated durability changes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light|Fuel", meta = (ClampMin = "0.5"))
	float FuelStepSeconds = 5.0f;

	/** Carried light level for ICGFLightBearerInterface: lumens / 1000 (a torch ~ 1.2). */
	float LightLevel() const { return Intensity / 1000.0f; }
};
