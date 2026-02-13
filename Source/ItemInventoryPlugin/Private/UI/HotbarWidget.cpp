// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/HotbarWidget.h"
#include "UI/InventorySlotWidget.h"
#include "Components/InventoryComponent.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

DEFINE_LOG_CATEGORY_STATIC(LogHotbarWidget, Log, All);

void UHotbarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UHotbarWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		UE_LOG(LogHotbarWidget, Error, TEXT("BuildWidgetTree: No WidgetTree"));
		return;
	}

	// Root: UBorder with hotbar background
	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HotbarBorder"));
	RootBorder->SetBrush(HotbarBackgroundBrush);
	RootBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.75f));
	RootBorder->SetPadding(FMargin(4.f));
	WidgetTree->RootWidget = RootBorder;

	// Horizontal box for slots
	SlotContainer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotContainer"));
	RootBorder->AddChild(SlotContainer);

	UE_LOG(LogHotbarWidget, Log, TEXT("BuildWidgetTree: Root=%s, SlotContainer=%s"),
		*RootBorder->GetName(), *SlotContainer->GetName());
}

void UHotbarWidget::InitHotbar(UInventoryComponent* InInventory, int32 NumSlots)
{
	BoundInventory = InInventory;
	CachedNumSlots = FMath::Max(1, NumSlots);

	UE_LOG(LogHotbarWidget, Log, TEXT("InitHotbar: Inventory=%s, NumSlots=%d, SlotContainer=%s"),
		InInventory ? *InInventory->GetName() : TEXT("null"),
		CachedNumSlots,
		SlotContainer ? TEXT("valid") : TEXT("NULL"));

	if (!SlotContainer)
	{
		UE_LOG(LogHotbarWidget, Error, TEXT("InitHotbar: SlotContainer is null — BuildWidgetTree may not have run"));
		return;
	}

	// Clear existing slot widgets
	SlotContainer->ClearChildren();
	SlotWidgets.Empty();

	TSubclassOf<UInventorySlotWidget> ClassToUse = SlotWidgetClass;
	if (!ClassToUse)
	{
		ClassToUse = UInventorySlotWidget::StaticClass();
	}

	for (int32 i = 0; i < CachedNumSlots; ++i)
	{
		// Create slot widget — uses CreateWidget for proper runtime integration
		UInventorySlotWidget* SlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), ClassToUse);
		if (!SlotWidget)
		{
			UE_LOG(LogHotbarWidget, Error, TEXT("InitHotbar: Failed to create slot widget %d"), i);
			continue;
		}

		SlotWidget->InitSlot(BoundInventory, i);
		SlotWidget->SetSelected(i == ActiveSlotIndex);
		SlotWidget->OnSlotClicked.AddDynamic(this, &UHotbarWidget::HandleChildSlotClicked);
		SlotWidget->OnSlotRightClicked.AddDynamic(this, &UHotbarWidget::HandleChildSlotRightClicked);
		SlotWidgets.Add(SlotWidget);

		// Add directly to horizontal box
		UHorizontalBoxSlot* HBSlot = SlotContainer->AddChildToHorizontalBox(SlotWidget);
		if (HBSlot)
		{
			HBSlot->SetPadding(FMargin(SlotSpacing * 0.5f, 0.f));
		}
	}

	UE_LOG(LogHotbarWidget, Log, TEXT("InitHotbar: Created %d slot widgets"), SlotWidgets.Num());
}

void UHotbarWidget::SetActiveSlot(int32 NewActiveSlot)
{
	const int32 OldSlot = ActiveSlotIndex;
	ActiveSlotIndex = FMath::Clamp(NewActiveSlot, 0, CachedNumSlots - 1);

	if (OldSlot >= 0 && OldSlot < SlotWidgets.Num() && SlotWidgets[OldSlot])
	{
		SlotWidgets[OldSlot]->SetSelected(false);
	}

	if (ActiveSlotIndex >= 0 && ActiveSlotIndex < SlotWidgets.Num() && SlotWidgets[ActiveSlotIndex])
	{
		SlotWidgets[ActiveSlotIndex]->SetSelected(true);
	}
}

void UHotbarWidget::RefreshAllSlots()
{
	for (UInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->RefreshSlot();
		}
	}
}

void UHotbarWidget::SetSlotHeld(int32 InSlotIndex, bool bHeld)
{
	if (InSlotIndex >= 0 && InSlotIndex < SlotWidgets.Num() && SlotWidgets[InSlotIndex])
	{
		SlotWidgets[InSlotIndex]->SetHeld(bHeld);
	}
}

void UHotbarWidget::HandleChildSlotClicked(int32 ChildSlotIndex, UInventoryComponent* Inventory)
{
	OnSlotClicked.Broadcast(ChildSlotIndex, Inventory);
}

void UHotbarWidget::HandleChildSlotRightClicked(int32 ChildSlotIndex, UInventoryComponent* Inventory)
{
	OnSlotRightClicked.Broadcast(ChildSlotIndex, Inventory);
}
