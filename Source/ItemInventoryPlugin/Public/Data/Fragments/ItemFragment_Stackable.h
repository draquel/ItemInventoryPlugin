#pragma once

#include "CoreMinimal.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Stackable.generated.h"

UCLASS(BlueprintType, DisplayName = "Stackable")
class ITEMINVENTORYPLUGIN_API UItemFragment_Stackable : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** Overrides UItemDefinition::MaxStackSize if present */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking")
	int32 MaxStackSize = 99;

	/** Allow split/merge operations */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking")
	bool bCanPartialStack = true;
};
