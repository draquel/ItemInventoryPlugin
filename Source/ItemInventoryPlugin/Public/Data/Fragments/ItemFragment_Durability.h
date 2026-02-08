#pragma once

#include "CoreMinimal.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Durability.generated.h"

/**
 * Definition-side durability config.
 * Pairs with UInstanceFragment_DurabilityState on runtime instances.
 */
UCLASS(BlueprintType, DisplayName = "Durability")
class ITEMINVENTORYPLUGIN_API UItemFragment_Durability : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability")
	float MaxDurability = 100.0f;

	/** Remove item when durability reaches 0? */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability")
	bool bDestroyAtZero = false;

	/** Per-use degradation amount */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability")
	float DegradeRate = 1.0f;
};
