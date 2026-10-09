#pragma once

#include "CoreMinimal.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_WorldDisplay.generated.h"

class UNiagaraSystem;

UCLASS(BlueprintType, DisplayName = "World Display")
class ITEMINVENTORYPLUGIN_API UItemFragment_WorldDisplay : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display")
	TSoftObjectPtr<UMaterialInterface> WorldMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display")
	FVector WorldScale = FVector(1.0f, 1.0f, 1.0f);

	/**
	 * Mesh-local rotation applied when the item lies in the world (dropped, burst from a chest).
	 * A sword authored blade-up (+Z) lies flat with Roll = 90; the pickup prompt and collision follow.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display")
	FRotator WorldRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display|Effects")
	TSoftObjectPtr<UNiagaraSystem> DropVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display|Effects")
	TSoftObjectPtr<USoundBase> PickupSFX;
};
