// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StreamableManager.h"
#include "ItemCursorWidget.generated.h"

class UImage;
class USizeBox;
class UTexture2D;

/**
 * Floating cursor widget that follows the mouse while an item is held.
 *
 * Shows the held item's icon at a small offset from the cursor position.
 * Uses HitTestInvisible so mouse clicks pass through to slots beneath.
 */
UCLASS(BlueprintType, Blueprintable)
class ITEMINVENTORYPLUGIN_API UItemCursorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show the cursor with the given item icon. */
	UFUNCTION(BlueprintCallable, Category = "ItemCursor")
	void ShowWithIcon(TSoftObjectPtr<UTexture2D> IconRef);

	/** Hide the cursor. */
	UFUNCTION(BlueprintCallable, Category = "ItemCursor")
	void HideCursor();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY()
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY()
	TObjectPtr<UImage> IconImage;

	TSharedPtr<FStreamableHandle> IconLoadHandle;
};
