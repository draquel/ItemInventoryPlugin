// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/InventoryPanelWidget.h"
#include "UI/InventorySlotWidget.h"
#include "Components/InventoryComponent.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

void UInventoryPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UInventoryPanelWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	// Root: UBorder
	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBorder"));
	RootBorder->SetBrush(PanelBackgroundBrush);
	RootBorder->SetBrushColor(PanelBackgroundTint);
	RootBorder->SetPadding(PanelPadding);
	WidgetTree->RootWidget = RootBorder;

	// Vertical box: title + grid
	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelVBox"));
	RootBorder->AddChild(VBox);

	// Title
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PanelTitle"));
	TitleText->SetText(PanelTitle);
	FSlateFontInfo TitleFont = TitleText->GetFont();
	TitleFont.Size = 16;
	TitleText->SetFont(TitleFont);
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	UVerticalBoxSlot* TitleSlot = VBox->AddChildToVerticalBox(TitleText);
	if (TitleSlot)
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	// Grid panel
	GridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("SlotGrid"));
	GridPanel->SetSlotPadding(FMargin(SlotSpacing * 0.5f));
	VBox->AddChildToVerticalBox(GridPanel);
}

void UInventoryPanelWidget::InitPanel(UInventoryComponent* InInventory, int32 StartSlot)
{
	BoundInventory = InInventory;
	CachedStartSlot = FMath::Max(0, StartSlot);

	if (!GridPanel)
	{
		return;
	}

	// Clear existing
	GridPanel->ClearChildren();
	SlotWidgets.Empty();

	if (!BoundInventory)
	{
		return;
	}

	const int32 MaxSlots = BoundInventory->MaxSlots;
	const int32 NumSlotsToShow = MaxSlots - CachedStartSlot;

	if (NumSlotsToShow <= 0)
	{
		return;
	}

	TSubclassOf<UInventorySlotWidget> ClassToUse = SlotWidgetClass;
	if (!ClassToUse)
	{
		ClassToUse = UInventorySlotWidget::StaticClass();
	}

	for (int32 i = 0; i < NumSlotsToShow; ++i)
	{
		const int32 InventorySlotIndex = CachedStartSlot + i;
		const int32 Row = i / GridColumns;
		const int32 Col = i % GridColumns;

		UInventorySlotWidget* SlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), ClassToUse);
		if (SlotWidget)
		{
			SlotWidget->InitSlot(BoundInventory, InventorySlotIndex);
			SlotWidget->OnSlotClicked.AddDynamic(this, &UInventoryPanelWidget::HandleChildSlotClicked);
			SlotWidget->OnSlotRightClicked.AddDynamic(this, &UInventoryPanelWidget::HandleChildSlotRightClicked);
			GridPanel->AddChildToUniformGrid(SlotWidget, Row, Col);
			SlotWidgets.Add(SlotWidget);
		}
	}
}

void UInventoryPanelWidget::RefreshAllSlots()
{
	for (UInventorySlotWidget* SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->RefreshSlot();
		}
	}
}

void UInventoryPanelWidget::SetSlotHeld(int32 InSlotIndex, bool bHeld)
{
	// Convert absolute inventory slot index to local array index
	const int32 LocalIndex = InSlotIndex - CachedStartSlot;
	if (LocalIndex >= 0 && LocalIndex < SlotWidgets.Num() && SlotWidgets[LocalIndex])
	{
		SlotWidgets[LocalIndex]->SetHeld(bHeld);
	}
}

void UInventoryPanelWidget::HandleChildSlotClicked(int32 ChildSlotIndex, UInventoryComponent* Inventory)
{
	OnSlotClicked.Broadcast(ChildSlotIndex, Inventory);
}

void UInventoryPanelWidget::HandleChildSlotRightClicked(int32 ChildSlotIndex, UInventoryComponent* Inventory)
{
	OnSlotRightClicked.Broadcast(ChildSlotIndex, Inventory);
}
