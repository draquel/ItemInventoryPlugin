#include "ItemSystemEditor.h"
#include "ItemDatabaseBrowser.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FItemSystemEditorModule"

static const FName ItemDatabaseBrowserTabName("ItemDatabaseBrowser");

void FItemSystemEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ItemDatabaseBrowserTabName,
		FOnSpawnTab::CreateRaw(this, &FItemSystemEditorModule::SpawnItemDatabaseBrowserTab))
		.SetDisplayName(LOCTEXT("ItemDatabaseBrowserTabTitle", "Item Database Browser"))
		.SetTooltipText(LOCTEXT("ItemDatabaseBrowserTooltip", "Browse and inspect all item definitions"))
		.SetMenuType(ETabSpawnerMenuType::Enabled);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("ItemSystem");
		Section.AddMenuEntry(
			"OpenItemDatabaseBrowser",
			LOCTEXT("OpenItemDatabaseBrowser", "Item Database Browser"),
			LOCTEXT("OpenItemDatabaseBrowserTooltip", "Open the Item Database Browser window"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(ItemDatabaseBrowserTabName);
			}))
		);
	}));
}

void FItemSystemEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ItemDatabaseBrowserTabName);
}

TSharedRef<SDockTab> FItemSystemEditorModule::SpawnItemDatabaseBrowserTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SItemDatabaseBrowser)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FItemSystemEditorModule, ItemSystemEditor)
