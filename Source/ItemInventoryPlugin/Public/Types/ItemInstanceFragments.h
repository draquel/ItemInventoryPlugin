#pragma once

#include "CoreMinimal.h"
#include "Types/CGFItemTypes.h"
#include "ItemInstanceFragments.generated.h"

/**
 * Tracks current durability for a specific item instance.
 * Pairs with UItemFragment_Durability on the definition side.
 */
UCLASS(BlueprintType, DisplayName = "Durability State")
class ITEMINVENTORYPLUGIN_API UInstanceFragment_DurabilityState : public UItemInstanceFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Durability")
	float CurrentDurability = 100.0f;

	virtual TSharedPtr<FJsonObject> SerializeFragment() const override;
	virtual void DeserializeFragment(const TSharedPtr<FJsonObject>& Data) override;
	virtual FText GetFragmentDisplayText_Implementation() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
};

/**
 * Generic key-value store for arbitrary per-instance data.
 * Use for game-specific state that doesn't warrant its own fragment class
 * (e.g. "KillCount", "CrafterName", "EnchantmentSeed").
 */
UCLASS(BlueprintType, DisplayName = "Custom Data")
class ITEMINVENTORYPLUGIN_API UInstanceFragment_CustomDataMap : public UItemInstanceFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Data")
	TMap<FString, FString> CustomData;

	UFUNCTION(BlueprintCallable, Category = "Custom Data")
	void SetValue(const FString& Key, const FString& Value);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Custom Data")
	FString GetValue(const FString& Key, const FString& DefaultValue = TEXT("")) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Custom Data")
	bool HasKey(const FString& Key) const;

	virtual TSharedPtr<FJsonObject> SerializeFragment() const override;
	virtual void DeserializeFragment(const TSharedPtr<FJsonObject>& Data) override;
	virtual FText GetFragmentDisplayText_Implementation() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
};
