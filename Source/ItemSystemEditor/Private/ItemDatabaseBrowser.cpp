#include "ItemDatabaseBrowser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "SItemDatabaseBrowser"

void SItemDatabaseBrowser::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar: Search + Refresh
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 0, 4, 0)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter by name..."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					FilterText = Text.ToString();
					ApplyFilter();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked_Lambda([this]() -> FReply
				{
					RefreshItemList();
					return FReply::Handled();
				})
			]
		]

		// List View
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ItemListView, SListView<TSharedPtr<FItemBrowserEntry>>)
			.ListItemsSource(&FilteredItems)
			.OnGenerateRow(this, &SItemDatabaseBrowser::OnGenerateRow)
			.OnMouseButtonDoubleClick(this, &SItemDatabaseBrowser::OnItemDoubleClicked)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column("DisplayName")
					.DefaultLabel(LOCTEXT("Col_Name", "Name"))
					.FillWidth(2.0f)
				+ SHeaderRow::Column("AssetId")
					.DefaultLabel(LOCTEXT("Col_AssetId", "Asset ID"))
					.FillWidth(2.0f)
				+ SHeaderRow::Column("MaxStack")
					.DefaultLabel(LOCTEXT("Col_MaxStack", "Max Stack"))
					.FillWidth(0.5f)
				+ SHeaderRow::Column("Weight")
					.DefaultLabel(LOCTEXT("Col_Weight", "Weight"))
					.FillWidth(0.5f)
				+ SHeaderRow::Column("Value")
					.DefaultLabel(LOCTEXT("Col_Value", "Value"))
					.FillWidth(0.5f)
				+ SHeaderRow::Column("Fragments")
					.DefaultLabel(LOCTEXT("Col_Fragments", "Fragments"))
					.FillWidth(0.5f)
				+ SHeaderRow::Column("Tags")
					.DefaultLabel(LOCTEXT("Col_Tags", "Tags"))
					.FillWidth(2.0f)
			)
		]
	];

	RefreshItemList();
}

void SItemDatabaseBrowser::RefreshItemList()
{
	AllItems.Empty();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UItemDefinition::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		UItemDefinition* Def = Cast<UItemDefinition>(AssetData.GetAsset());
		if (!Def) continue;

		TSharedPtr<FItemBrowserEntry> Entry = MakeShared<FItemBrowserEntry>();
		Entry->DisplayName = Def->DisplayName.ToString();
		Entry->AssetId = Def->GetPrimaryAssetId().ToString();
		Entry->MaxStackSize = Def->MaxStackSize;
		Entry->Weight = Def->Weight;
		Entry->BaseValue = Def->BaseValue;
		Entry->FragmentCount = Def->Fragments.Num();
		Entry->Tags = Def->ItemTags.ToStringSimple();
		Entry->AssetPath = AssetData.GetSoftObjectPath();

		AllItems.Add(Entry);
	}

	ApplyFilter();
}

void SItemDatabaseBrowser::ApplyFilter()
{
	FilteredItems.Empty();

	if (FilterText.IsEmpty())
	{
		FilteredItems = AllItems;
	}
	else
	{
		for (const TSharedPtr<FItemBrowserEntry>& Entry : AllItems)
		{
			if (Entry->DisplayName.Contains(FilterText, ESearchCase::IgnoreCase) ||
				Entry->AssetId.Contains(FilterText, ESearchCase::IgnoreCase))
			{
				FilteredItems.Add(Entry);
			}
		}
	}

	if (ItemListView.IsValid())
	{
		ItemListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SItemDatabaseBrowser::OnGenerateRow(
	TSharedPtr<FItemBrowserEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	class SItemBrowserRow : public SMultiColumnTableRow<TSharedPtr<FItemBrowserEntry>>
	{
	public:
		SLATE_BEGIN_ARGS(SItemBrowserRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FItemBrowserEntry> InItem)
		{
			Entry = InItem;
			SMultiColumnTableRow<TSharedPtr<FItemBrowserEntry>>::Construct(
				SMultiColumnTableRow<TSharedPtr<FItemBrowserEntry>>::FArguments(), InOwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			FString Text;
			if (ColumnName == "DisplayName") Text = Entry->DisplayName;
			else if (ColumnName == "AssetId") Text = Entry->AssetId;
			else if (ColumnName == "MaxStack") Text = FString::Printf(TEXT("%d"), Entry->MaxStackSize);
			else if (ColumnName == "Weight") Text = FString::Printf(TEXT("%.1f"), Entry->Weight);
			else if (ColumnName == "Value") Text = FString::Printf(TEXT("%d"), Entry->BaseValue);
			else if (ColumnName == "Fragments") Text = FString::Printf(TEXT("%d"), Entry->FragmentCount);
			else if (ColumnName == "Tags") Text = Entry->Tags;

			return SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Margin(FMargin(4, 2));
		}

		TSharedPtr<FItemBrowserEntry> Entry;
	};

	return SNew(SItemBrowserRow, OwnerTable, Item);
}

void SItemDatabaseBrowser::OnItemDoubleClicked(TSharedPtr<FItemBrowserEntry> Item)
{
	if (!Item.IsValid()) return;

	UObject* Asset = Item->AssetPath.TryLoad();
	if (Asset)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
	}
}

#undef LOCTEXT_NAMESPACE
