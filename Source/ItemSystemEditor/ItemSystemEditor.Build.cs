using UnrealBuildTool;

public class ItemSystemEditor : ModuleRules
{
	public ItemSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore", // linked, not included: SListView's key handling reaches EKeys
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"ItemInventoryPlugin",
			"CommonGameFramework",
			"AssetRegistry",
			"GameplayTags",
			"ToolMenus",
		});
	}
}
