// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/InventorySlotWidget.h"
#include "Components/InventoryComponent.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Types/CGFItemTypes.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"

void UInventorySlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UInventorySlotWidget::NativeDestruct()
{
	// Unbind inventory delegates
	if (BoundInventory)
	{
		BoundInventory->OnItemAdded.RemoveDynamic(this, &UInventorySlotWidget::HandleItemAdded);
		BoundInventory->OnItemRemoved.RemoveDynamic(this, &UInventorySlotWidget::HandleItemRemoved);
		BoundInventory->OnItemUpdated.RemoveDynamic(this, &UInventorySlotWidget::HandleItemUpdated);
		BoundInventory->OnInventoryChanged.RemoveDynamic(this, &UInventorySlotWidget::HandleInventoryChanged);
	}

	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}

	Super::NativeDestruct();
}

void UInventorySlotWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	// Root: USizeBox
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SlotSizeBox"));
	RootSizeBox->SetWidthOverride(SlotSize);
	RootSizeBox->SetHeightOverride(SlotSize);
	WidgetTree->RootWidget = RootSizeBox;

	// Overlay container
	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SlotOverlay"));
	RootSizeBox->AddChild(Overlay);

	// Background image — use brush if set, otherwise a solid dark color
	BackgroundImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BackgroundImage"));
	if (SlotBackgroundBrush.HasUObject() || SlotBackgroundBrush.GetResourceName() != NAME_None)
	{
		BackgroundImage->SetBrush(SlotBackgroundBrush);
	}
	else
	{
		// Default: dark semi-transparent background
		BackgroundImage->SetColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.12f, 0.9f));
	}
	UOverlaySlot* BgSlot = Overlay->AddChildToOverlay(BackgroundImage);
	if (BgSlot)
	{
		BgSlot->SetHorizontalAlignment(HAlign_Fill);
		BgSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// Item icon — starts collapsed, shown when slot has an item
	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
	IconImage->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* IconSlot = Overlay->AddChildToOverlay(IconImage);
	if (IconSlot)
	{
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}

	// Stack count text (bottom-right)
	StackCountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StackCountText"));
	StackCountText->SetColorAndOpacity(FSlateColor(StackCountColor));
	if (StackCountFont.HasValidFont())
	{
		StackCountText->SetFont(StackCountFont);
	}
	StackCountText->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* TextSlot = Overlay->AddChildToOverlay(StackCountText);
	if (TextSlot)
	{
		TextSlot->SetHorizontalAlignment(HAlign_Right);
		TextSlot->SetVerticalAlignment(VAlign_Bottom);
	}
}

void UInventorySlotWidget::InitSlot(UInventoryComponent* InInventory, int32 InSlotIndex)
{
	// Unbind old
	if (BoundInventory)
	{
		BoundInventory->OnItemAdded.RemoveDynamic(this, &UInventorySlotWidget::HandleItemAdded);
		BoundInventory->OnItemRemoved.RemoveDynamic(this, &UInventorySlotWidget::HandleItemRemoved);
		BoundInventory->OnItemUpdated.RemoveDynamic(this, &UInventorySlotWidget::HandleItemUpdated);
		BoundInventory->OnInventoryChanged.RemoveDynamic(this, &UInventorySlotWidget::HandleInventoryChanged);
	}

	BoundInventory = InInventory;
	SlotIndex = InSlotIndex;

	// Bind new
	if (BoundInventory)
	{
		BoundInventory->OnItemAdded.AddDynamic(this, &UInventorySlotWidget::HandleItemAdded);
		BoundInventory->OnItemRemoved.AddDynamic(this, &UInventorySlotWidget::HandleItemRemoved);
		BoundInventory->OnItemUpdated.AddDynamic(this, &UInventorySlotWidget::HandleItemUpdated);
		BoundInventory->OnInventoryChanged.AddDynamic(this, &UInventorySlotWidget::HandleInventoryChanged);
	}

	RefreshSlot();
}

void UInventorySlotWidget::RefreshSlot()
{
	if (!BoundInventory || SlotIndex < 0)
	{
		if (IconImage)
		{
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (StackCountText)
		{
			StackCountText->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	const FItemInstance Item = BoundInventory->GetItemInSlot(SlotIndex);

	if (!Item.IsValid())
	{
		// Empty slot — hide icon
		if (IconImage)
		{
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (StackCountText)
		{
			StackCountText->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	// Slot has an item — make icon visible
	if (IconImage)
	{
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// Stack count
	if (StackCountText)
	{
		if (Item.StackCount > 1)
		{
			StackCountText->SetText(FText::AsNumber(Item.StackCount));
			StackCountText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			StackCountText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Resolve item definition for icon
	UItemDatabaseSubsystem* ItemDB = nullptr;
	if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		ItemDB = GI->GetSubsystem<UItemDatabaseSubsystem>();
	}

	if (!ItemDB)
	{
		// No database — show a placeholder color so the slot looks occupied
		if (IconImage)
		{
			IconImage->SetBrushFromTexture(nullptr);
			IconImage->SetColorAndOpacity(FLinearColor(0.4f, 0.6f, 0.3f, 1.f));
		}
		return;
	}

	const UItemDefinition* Def = ItemDB->GetDefinition(Item.ItemDefinitionId);
	if (!Def)
	{
		// Definition not found — show placeholder
		if (IconImage)
		{
			IconImage->SetBrushFromTexture(nullptr);
			IconImage->SetColorAndOpacity(FLinearColor(0.4f, 0.6f, 0.3f, 1.f));
		}
		return;
	}

	// Load icon from definition
	if (!Def->Icon.IsNull())
	{
		// Reset tint to white so the texture renders correctly
		if (IconImage)
		{
			IconImage->SetColorAndOpacity(FLinearColor::White);
		}

		if (Def->Icon.IsValid())
		{
			// Already loaded
			if (IconImage)
			{
				IconImage->SetBrushFromTexture(Def->Icon.Get());
			}
		}
		else
		{
			// Async load
			if (IconLoadHandle.IsValid())
			{
				IconLoadHandle->CancelHandle();
			}

			TSoftObjectPtr<UTexture2D> IconRef = Def->Icon;
			IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				IconRef.ToSoftObjectPath(),
				FStreamableDelegate::CreateWeakLambda(this, [this, IconRef]()
				{
					if (IconImage && IconRef.IsValid())
					{
						IconImage->SetBrushFromTexture(IconRef.Get());
					}
				})
			);
		}
	}
	else
	{
		// Item has no icon — show placeholder color
		if (IconImage)
		{
			IconImage->SetBrushFromTexture(nullptr);
			IconImage->SetColorAndOpacity(FLinearColor(0.4f, 0.6f, 0.3f, 1.f));
		}
	}
}

void UInventorySlotWidget::SetSelected(bool bInSelected)
{
	bIsSelected = bInSelected;

	// Held state takes visual priority over selected state
	if (bIsHeld)
	{
		return;
	}

	if (BackgroundImage)
	{
		const bool bHasCustomBrush = SlotBackgroundBrush.HasUObject() || SlotBackgroundBrush.GetResourceName() != NAME_None;
		const bool bHasSelectedBrush = SlotSelectedBrush.HasUObject() || SlotSelectedBrush.GetResourceName() != NAME_None;

		if (bIsSelected)
		{
			if (bHasSelectedBrush)
			{
				BackgroundImage->SetBrush(SlotSelectedBrush);
			}
			else
			{
				BackgroundImage->SetColorAndOpacity(FLinearColor(0.3f, 0.5f, 0.8f, 0.9f));
			}
		}
		else
		{
			if (bHasCustomBrush)
			{
				BackgroundImage->SetBrush(SlotBackgroundBrush);
			}
			else
			{
				BackgroundImage->SetColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.12f, 0.9f));
			}
		}
	}
}

void UInventorySlotWidget::SetHeld(bool bInHeld)
{
	bIsHeld = bInHeld;
	if (bIsHeld)
	{
		if (BackgroundImage)
		{
			BackgroundImage->SetColorAndOpacity(FLinearColor(0.8f, 0.6f, 0.2f, 0.9f));
		}
	}
	else
	{
		// Revert to selected/default state
		SetSelected(bIsSelected);
	}
}

FReply UInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotClicked.Broadcast(SlotIndex, BoundInventory);
		return FReply::Handled();
	}
	else if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSlotRightClicked.Broadcast(SlotIndex, BoundInventory);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// --- Delegate Handlers ---

void UInventorySlotWidget::HandleItemAdded(const FItemInstance& Item, int32 InSlotIndex)
{
	if (InSlotIndex == SlotIndex)
	{
		RefreshSlot();
	}
}

void UInventorySlotWidget::HandleItemRemoved(const FItemInstance& Item, int32 InSlotIndex)
{
	if (InSlotIndex == SlotIndex)
	{
		RefreshSlot();
	}
}

void UInventorySlotWidget::HandleItemUpdated(const FItemInstance& Item, int32 InSlotIndex)
{
	if (InSlotIndex == SlotIndex)
	{
		RefreshSlot();
	}
}

void UInventorySlotWidget::HandleInventoryChanged()
{
	RefreshSlot();
}
