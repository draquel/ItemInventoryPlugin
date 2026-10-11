// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/CGFItemTypes.h"
#include "ItemTooltipWidget.generated.h"

class UItemDefinition;
class UTextBlock;

/**
 * Item tooltip (ItemInventoryPlugin UI): the item's name, its stats (weapon damage and speed, armour and consumable
 * attribute changes, durability or a light's fuel) and its description. Code-built. Inventory and equipment slots
 * create one, set it as their tooltip and refresh it whenever their item (or its durability) changes.
 */
UCLASS(BlueprintType, Blueprintable)
class ITEMINVENTORYPLUGIN_API UItemTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Show an item.
	 * @param Definition The item's definition (null shows nothing but an empty frame)
	 * @param Item       The instance, for per-item state such as current durability
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void SetItem(const UItemDefinition* Definition, const FItemInstance& Item);

	/**
	 * Pure: the stat lines for an item, one per line: weapon damage, speed and critical hits; attribute changes from
	 * equipment and consumables ("+20 Max Health"); durability, or fuel time for a light source.
	 * @param Definition The item's definition
	 * @param Item       The instance (current durability lives here)
	 * @return The lines, or empty text when the item has no stats
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|UI")
	static FText FormatItemStats(const UItemDefinition* Definition, const FItemInstance& Item);

	/**
	 * Pure: an attribute name as a label ("MaxHealth" -> "Max Health", "AttackPower" -> "Attack Power").
	 * @param AttributeName The attribute's property name
	 * @return The spaced label
	 */
	static FString PrettyAttributeName(const FString& AttributeName);

protected:
	virtual void NativeOnInitialized() override;

	/** Build the code-built tree (idempotent: widgets created without a player context build it lazily). */
	void BuildWidgetTree();

	UPROPERTY()
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatsText;

	UPROPERTY()
	TObjectPtr<UTextBlock> DescriptionText;
};
