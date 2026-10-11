// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/ItemTooltipWidget.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Consumable.h"
#include "Data/Fragments/ItemFragment_Durability.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Data/Fragments/ItemFragment_LightSource.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
#include "Types/ItemInstanceFragments.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	/** "+20 Max Health" / "-5 Defense" for every valid modifier. */
	void AppendModifiers(const TArray<FCGFAttributeModifier>& Modifiers, TArray<FString>& OutLines)
	{
		for (const FCGFAttributeModifier& Modifier : Modifiers)
		{
			if (!Modifier.IsValid())
			{
				continue;
			}
			const FString Amount = FString::SanitizeFloat(FMath::Abs(Modifier.Magnitude), 0);
			OutLines.Add(FString::Printf(TEXT("%s%s %s"), Modifier.Magnitude < 0.f ? TEXT("-") : TEXT("+"), *Amount,
				*UItemTooltipWidget::PrettyAttributeName(Modifier.Attribute.GetName())));
		}
	}

	/** Seconds as M:SS. */
	FString FormatMinutes(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::RoundToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}
}

void UItemTooltipWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UItemTooltipWidget::BuildWidgetTree()
{
	if (!WidgetTree || NameText)
	{
		return;
	}

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TooltipFrame"));
	Frame->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.06f, 0.94f));
	Frame->SetPadding(FMargin(10.f, 8.f));
	WidgetTree->RootWidget = Frame;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TooltipColumn"));
	Frame->SetContent(Column);

	auto MakeLine = [this, Column](const TCHAR* Name, int32 Size, const FLinearColor& Color, float TopPad) -> UTextBlock*
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetAutoWrapText(true);
		Text->SetWrapTextAt(280.f);
		if (UVerticalBoxSlot* Slot = Column->AddChildToVerticalBox(Text))
		{
			Slot->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f));
		}
		return Text;
	};
	NameText = MakeLine(TEXT("TooltipName"), 15, FLinearColor(1.f, 0.85f, 0.45f), 0.f);
	StatsText = MakeLine(TEXT("TooltipStats"), 12, FLinearColor(0.92f, 0.92f, 0.95f), 4.f);
	DescriptionText = MakeLine(TEXT("TooltipDescription"), 11, FLinearColor(0.65f, 0.67f, 0.72f), 6.f);
}

void UItemTooltipWidget::SetItem(const UItemDefinition* Definition, const FItemInstance& Item)
{
	BuildWidgetTree();
	if (!NameText)
	{
		return;
	}

	NameText->SetText(Definition ? Definition->DisplayName : FText::GetEmpty());

	const FText Stats = FormatItemStats(Definition, Item);
	StatsText->SetText(Stats);
	StatsText->SetVisibility(Stats.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	const FText Description = Definition ? Definition->Description : FText::GetEmpty();
	DescriptionText->SetText(Description);
	DescriptionText->SetVisibility(Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

FText UItemTooltipWidget::FormatItemStats(const UItemDefinition* Definition, const FItemInstance& Item)
{
	if (!Definition)
	{
		return FText::GetEmpty();
	}

	TArray<FString> Lines;
	if (const UItemFragment_Weapon* Weapon = Definition->FindFragment<UItemFragment_Weapon>())
	{
		Lines.Add(FString::Printf(TEXT("Damage %s"), *FString::SanitizeFloat(Weapon->BaseDamage, 0)));
		Lines.Add(FString::Printf(TEXT("Attack speed %.1f"), Weapon->AttackSpeed));
		if (Weapon->CritChance > 0.f)
		{
			Lines.Add(FString::Printf(TEXT("Critical %d%% (x%s)"), FMath::RoundToInt(Weapon->CritChance * 100.f),
				*FString::SanitizeFloat(Weapon->CritMultiplier, 0)));
		}
	}
	if (const UItemFragment_Equipment* Equipment = Definition->FindFragment<UItemFragment_Equipment>())
	{
		AppendModifiers(Equipment->StatModifiers, Lines);
	}
	if (const UItemFragment_Consumable* Consumable = Definition->FindFragment<UItemFragment_Consumable>())
	{
		AppendModifiers(Consumable->AttributeChanges, Lines);
	}
	if (const UItemFragment_Durability* Durability = Definition->FindFragment<UItemFragment_Durability>())
	{
		const UInstanceFragment_DurabilityState* State = Item.FindFragment<UInstanceFragment_DurabilityState>();
		const float Current = State ? State->CurrentDurability : Durability->MaxDurability;
		// A light source burns its durability as fuel seconds.
		if (Definition->FindFragment<UItemFragment_LightSource>())
		{
			Lines.Add(FString::Printf(TEXT("Fuel %s / %s"), *FormatMinutes(Current), *FormatMinutes(Durability->MaxDurability)));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("Durability %d / %d"), FMath::RoundToInt(Current), FMath::RoundToInt(Durability->MaxDurability)));
		}
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FString UItemTooltipWidget::PrettyAttributeName(const FString& AttributeName)
{
	FString Out;
	Out.Reserve(AttributeName.Len() + 4);
	for (int32 i = 0; i < AttributeName.Len(); ++i)
	{
		const TCHAR Char = AttributeName[i];
		// A capital starts a new word unless it continues an acronym ("HP") or follows a space already.
		if (i > 0 && FChar::IsUpper(Char) && (FChar::IsLower(AttributeName[i - 1])
			|| (i + 1 < AttributeName.Len() && FChar::IsLower(AttributeName[i + 1]) && FChar::IsUpper(AttributeName[i - 1]))))
		{
			Out.AppendChar(TEXT(' '));
		}
		Out.AppendChar(Char);
	}
	return Out;
}
