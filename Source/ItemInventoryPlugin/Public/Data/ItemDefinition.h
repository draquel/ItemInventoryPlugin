#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/CGFItemTypes.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemDefinition.generated.h"

/**
 * Primary data asset representing one item type.
 * Each unique item in the game is ONE UItemDefinition asset.
 * Use fragments for type-specific data — do NOT subclass this.
 */
UCLASS(BlueprintType)
class ITEMINVENTORYPLUGIN_API UItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Identity ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	// --- Classification ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FGameplayTagContainer ItemTags;

	// --- Stacking ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Stacking")
	int32 MaxStackSize = 1;

	// --- Properties ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Properties")
	float Weight = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Properties")
	int32 BaseValue = 0;

	// --- Fragments ---

	/** Definition fragments — static data shared by all instances */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Item|Fragments")
	TArray<TObjectPtr<UItemDefinitionFragment>> Fragments;

	/** Default instance fragments — copied to new instances of this item */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Item|Instance Defaults")
	TArray<TObjectPtr<UItemInstanceFragment>> DefaultInstanceFragments;

	// --- Helper Functions ---

	/** Find a specific definition fragment type */
	template<typename T>
	T* FindFragment() const
	{
		for (UItemDefinitionFragment* Fragment : Fragments)
		{
			if (T* Typed = Cast<T>(Fragment))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	/** Does this item have a fragment of the given type? */
	template<typename T>
	bool HasFragment() const
	{
		return FindFragment<T>() != nullptr;
	}

	/** Blueprint-callable fragment lookup by class */
	UFUNCTION(BlueprintCallable, Category = "Item|Fragments", meta = (DeterminesOutputType = "FragmentClass"))
	UItemDefinitionFragment* FindFragmentByClass(TSubclassOf<UItemDefinitionFragment> FragmentClass) const;

	// --- PrimaryDataAsset ---

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
