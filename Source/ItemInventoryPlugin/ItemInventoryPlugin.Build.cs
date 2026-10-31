using UnrealBuildTool;

public class ItemInventoryPlugin : ModuleRules
{
	public ItemInventoryPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"NetCore",
			"GameplayTags",
			"GameplayAbilities",
			"CommonGameFramework",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"HTTP",
			"Json",
			"Niagara",
			"DeveloperSettings",
			"UMG",
			"Slate",
			"SlateCore",
			"InputCore",
		});
	}
}
