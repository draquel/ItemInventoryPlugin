// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StreamableManager.h"
#include "InventorySlotWidget.generated.h"

class UInventoryComponent;
class UImage;
class UTextBlock;
class USizeBox;
class UOverlay;
class UTexture2D;

/** Fired when an inventory slot is clicked. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventorySlotClicked, int32, SlotIndex, UInventoryComponent*, Inventory);

/**
 * Single inventory slot display widget.
 *
 * Reusable by both the hotbar and the inventory panel.
 * Shows an item icon (async-loaded from UItemDefinition::Icon)
 * and a stack count badge when StackCount > 1.
 */
UCLASS(BlueprintType, Blueprintable)
class ITEMINVENTORYPLUGIN_API UInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- Style ---

	/** Background brush for the slot (default state). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventorySlot|Style")
	FSlateBrush SlotBackgroundBrush;

	/** Background brush when this slot is selected/active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventorySlot|Style")
	FSlateBrush SlotSelectedBrush;

	/** Brush shown when the slot is empty (placeholder icon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventorySlot|Style")
	FSlateBrush EmptySlotIconBrush;

	/** Color for the stack count text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventorySlot|Style")
	FLinearColor StackCountColor = FLinearColor::White;

	/** Font for the stack count text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventorySlot|Style")
	FSlateFontInfo StackCountFont;

	/** Size of the slot in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InventorySlot|Style")
	float SlotSize = 64.f;

	// --- API ---

	/** Bind this widget to an inventory component and slot index. */
	UFUNCTION(BlueprintCallable, Category = "InventorySlot")
	void InitSlot(UInventoryComponent* InInventory, int32 InSlotIndex);

	/** Refresh the displayed item icon and stack count from the bound inventory. */
	UFUNCTION(BlueprintCallable, Category = "InventorySlot")
	void RefreshSlot();

	/** Set whether this slot shows the selected/active highlight. */
	UFUNCTION(BlueprintCallable, Category = "InventorySlot")
	void SetSelected(bool bInSelected);

	/** Set whether this slot shows the held/grabbed highlight (gold). */
	UFUNCTION(BlueprintCallable, Category = "InventorySlot")
	void SetHeld(bool bInHeld);

	/** Returns the slot index this widget is bound to. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InventorySlot")
	int32 GetSlotIndex() const { return SlotIndex; }

	/** Fired when this slot is left-clicked. */
	UPROPERTY(BlueprintAssignable, Category = "InventorySlot")
	FOnInventorySlotClicked OnSlotClicked;

	/** Fired when this slot is right-clicked. */
	UPROPERTY(BlueprintAssignable, Category = "InventorySlot")
	FOnInventorySlotClicked OnSlotRightClicked;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** Build the Slate widget tree programmatically. */
	void BuildWidgetTree();

	/** Inventory delegate handlers — filter by slot and refresh. */
	UFUNCTION()
	void HandleItemAdded(const FItemInstance& Item, int32 InSlotIndex);

	UFUNCTION()
	void HandleItemRemoved(const FItemInstance& Item, int32 InSlotIndex);

	UFUNCTION()
	void HandleItemUpdated(const FItemInstance& Item, int32 InSlotIndex);

	UFUNCTION()
	void HandleInventoryChanged();

	// --- Widget refs ---
	UPROPERTY()
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY()
	TObjectPtr<UImage> BackgroundImage;

	UPROPERTY()
	TObjectPtr<UImage> IconImage;

	UPROPERTY()
	TObjectPtr<UTextBlock> StackCountText;

	// --- State ---
	UPROPERTY()
	TObjectPtr<UInventoryComponent> BoundInventory;

	int32 SlotIndex = -1;
	bool bIsSelected = false;
	bool bIsHeld = false;

	/** Handle for async icon loading. */
	TSharedPtr<FStreamableHandle> IconLoadHandle;
};
