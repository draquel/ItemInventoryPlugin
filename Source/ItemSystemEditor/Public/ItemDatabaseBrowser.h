#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FItemBrowserEntry
{
	FString DisplayName;
	FString AssetId;
	int32 MaxStackSize = 1;
	float Weight = 0.0f;
	int32 BaseValue = 0;
	int32 FragmentCount = 0;
	FString Tags;
	FSoftObjectPath AssetPath;
};

class SItemDatabaseBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SItemDatabaseBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void RefreshItemList();
	void ApplyFilter();

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FItemBrowserEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnItemDoubleClicked(TSharedPtr<FItemBrowserEntry> Item);

	TArray<TSharedPtr<FItemBrowserEntry>> AllItems;
	TArray<TSharedPtr<FItemBrowserEntry>> FilteredItems;
	TSharedPtr<SListView<TSharedPtr<FItemBrowserEntry>>> ItemListView;
	FString FilterText;
};
