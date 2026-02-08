#pragma once

#include "CoreMinimal.h"
#include "NiagaraSystem.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_WorldDisplay.generated.h"

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display|Effects")
	TSoftObjectPtr<UNiagaraSystem> DropVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Display|Effects")
	TSoftObjectPtr<USoundBase> PickupSFX;
};
