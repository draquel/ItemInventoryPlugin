#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ItemDefinitionFragment.generated.h"

/**
 * Base class for item definition fragments.
 * Definition fragments hold static data shared across all instances of an item.
 * Subclass this for type-specific data (weapon stats, equipment config, etc).
 *
 * Use EditInlineNew + DefaultToInstanced so fragments can be edited inline
 * on UItemDefinition assets.
 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class ITEMINVENTORYPLUGIN_API UItemDefinitionFragment : public UObject
{
	GENERATED_BODY()

public:
	/** Override to provide a display name for editor UI */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Fragment")
	FText GetFragmentDisplayName() const;

	/** Override to provide description text for editor UI */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Fragment")
	FText GetFragmentDescription() const;
};
