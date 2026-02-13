// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/InventorySlotWidget.h"
#include "InventoryPanelWidget.generated.h"

class UInventoryComponent;
class UBorder;
class UVerticalBox;
class UUniformGridPanel;
class UTextBlock;

/**
 * Floating inventory panel widget.
 *
 * Shows a grid of inventory slots starting from StartSlotIndex
 * (typically after the hotbar slots). Toggle-visible when the
 * player opens the inventory UI.
 */
UCLASS(BlueprintType, Blueprintable)
class ITEMINVENTORYPLUGIN_API UInventoryPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- Style ---

	/** Background brush for the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	FSlateBrush PanelBackgroundBrush;

	/** Background tint color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	FLinearColor PanelBackgroundTint = FLinearColor(0.05f, 0.05f, 0.1f, 0.85f);

	/** Title text displayed at the top of the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	FText PanelTitle = NSLOCTEXT("InventoryPanel", "Title", "Inventory");

	/** Number of columns in the slot grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	int32 GridColumns = 5;

	/** Spacing between slots in the grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	float SlotSpacing = 4.f;

	/** Padding around the panel content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	FMargin PanelPadding = FMargin(8.f);

	/** Override class for slot widgets (for Blueprint skinning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventoryPanel|Style")
	TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

	// --- API ---

	/** Initialize the panel. StartSlot is the first inventory slot to display (skipping hotbar). */
	UFUNCTION(BlueprintCallable, Category = "InventoryPanel")
	void InitPanel(UInventoryComponent* InInventory, int32 StartSlot = 9);

	/** Refresh all slot widgets. */
	UFUNCTION(BlueprintCallable, Category = "InventoryPanel")
	void RefreshAllSlots();

	/** Set the held (grabbed) highlight on a specific inventory slot. */
	UFUNCTION(BlueprintCallable, Category = "InventoryPanel")
	void SetSlotHeld(int32 InSlotIndex, bool bHeld);

	/** Relayed when any child slot is left-clicked. */
	UPROPERTY(BlueprintAssignable, Category = "InventoryPanel")
	FOnInventorySlotClicked OnSlotClicked;

	/** Relayed when any child slot is right-clicked. */
	UPROPERTY(BlueprintAssignable, Category = "InventoryPanel")
	FOnInventorySlotClicked OnSlotRightClicked;

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildWidgetTree();

	UFUNCTION()
	void HandleChildSlotClicked(int32 ChildSlotIndex, UInventoryComponent* Inventory);

	UFUNCTION()
	void HandleChildSlotRightClicked(int32 ChildSlotIndex, UInventoryComponent* Inventory);

	UPROPERTY()
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UUniformGridPanel> GridPanel;

	UPROPERTY()
	TArray<TObjectPtr<UInventorySlotWidget>> SlotWidgets;

	UPROPERTY()
	TObjectPtr<UInventoryComponent> BoundInventory;

	int32 CachedStartSlot = 9;
};
