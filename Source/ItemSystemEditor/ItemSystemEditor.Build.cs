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
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorStyle",
			"ItemInventoryPlugin",
			"CommonGameFramework",
			"InputCore",
			"PropertyEditor",
			"AssetRegistry",
			"GameplayTags",
			"ToolMenus",
		});
	}
}
