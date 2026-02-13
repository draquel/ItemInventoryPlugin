// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/ItemCursorWidget.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"

void UItemCursorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		return;
	}

	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CursorSizeBox"));
	RootSizeBox->SetWidthOverride(48.f);
	RootSizeBox->SetHeightOverride(48.f);
	WidgetTree->RootWidget = RootSizeBox;

	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CursorIcon"));
	IconImage->SetColorAndOpacity(FLinearColor::White);
	RootSizeBox->AddChild(IconImage);

	SetVisibility(ESlateVisibility::Collapsed);
}

void UItemCursorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (GetVisibility() == ESlateVisibility::Collapsed)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	float MouseX, MouseY;
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		// Offset so the icon appears below-right of the cursor
		SetPositionInViewport(FVector2D(MouseX + 8.f, MouseY + 8.f));
	}
}

void UItemCursorWidget::ShowWithIcon(TSoftObjectPtr<UTexture2D> IconRef)
{
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}

	if (IconRef.IsNull())
	{
		// No icon — show a placeholder color
		if (IconImage)
		{
			IconImage->SetBrushFromTexture(nullptr);
			IconImage->SetColorAndOpacity(FLinearColor(0.4f, 0.6f, 0.3f, 1.f));
		}
		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	if (IconRef.IsValid())
	{
		if (IconImage)
		{
			IconImage->SetColorAndOpacity(FLinearColor::White);
			IconImage->SetBrushFromTexture(IconRef.Get());
		}
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		// Async load
		IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			IconRef.ToSoftObjectPath(),
			FStreamableDelegate::CreateWeakLambda(this, [this, IconRef]()
			{
				if (IconImage && IconRef.IsValid())
				{
					IconImage->SetColorAndOpacity(FLinearColor::White);
					IconImage->SetBrushFromTexture(IconRef.Get());
				}
			})
		);
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UItemCursorWidget::HideCursor()
{
	SetVisibility(ESlateVisibility::Collapsed);

	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}
}
