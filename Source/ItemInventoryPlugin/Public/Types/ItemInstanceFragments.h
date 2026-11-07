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
	/**
	 * Durability left (a light's fuel, for a light source). Set on a definition's default; on a held item it changes on the
	 * server through FItemInstance::EditFragment, which marks the item for replication (G136), so Blueprints read it only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Durability")
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
	/** The entries. Blueprints read them; a change goes through SetValue on the fragment EditFragment returns (G136). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom Data")
	TMap<FString, FString> CustomData;

	/**
	 * Set one entry. On an item no slot holds yet (just created, before it is added) any fragment will do; on a held
	 * item call it on the fragment FItemInstance::EditFragment returns, on the server, or the change never replicates:
	 * replication sees a fragment's contents change only through the revision EditFragment bumps (G136).
	 * @param Key The entry's key
	 * @param Value Its value
	 */
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
