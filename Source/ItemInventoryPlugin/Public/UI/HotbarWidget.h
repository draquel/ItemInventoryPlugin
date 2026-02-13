// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotbarWidget.generated.h"

class UInventoryComponent;
class UInventorySlotWidget;
class UHorizontalBox;
class UBorder;

/**
 * Always-visible horizontal hotbar widget.
 *
 * Displays a row of inventory slots [0, NumSlots) at the bottom center
 * of the viewport. Each slot shows the item icon and stack count.
 * One slot is highlighted as the active selection.
 */
UCLASS(BlueprintType, Blueprintable)
class ITEMINVENTORYPLUGIN_API UHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- Style ---

	/** Background brush for the hotbar bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotbar|Style")
	FSlateBrush HotbarBackgroundBrush;

	/** Spacing between slot widgets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotbar|Style")
	float SlotSpacing = 4.f;

	/** Override class for slot widgets (for Blueprint skinning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotbar|Style")
	TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

	// --- API ---

	/** Initialize the hotbar with an inventory component and number of slots. */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void InitHotbar(UInventoryComponent* InInventory, int32 NumSlots = 9);

	/** Update the active (selected) slot highlight. */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void SetActiveSlot(int32 NewActiveSlot);

	/** Refresh all slot widgets. */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void RefreshAllSlots();

	/** Returns the currently active slot index. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Hotbar")
	int32 GetActiveSlot() const { return ActiveSlotIndex; }

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildWidgetTree();

	UPROPERTY()
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY()
	TObjectPtr<UHorizontalBox> SlotContainer;

	UPROPERTY()
	TArray<TObjectPtr<UInventorySlotWidget>> SlotWidgets;

	int32 ActiveSlotIndex = 0;
	int32 CachedNumSlots = 9;

	UPROPERTY()
	TObjectPtr<UInventoryComponent> BoundInventory;
};
