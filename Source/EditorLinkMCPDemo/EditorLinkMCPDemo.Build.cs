using UnrealBuildTool;

public class EditorLinkMCPDemo : ModuleRules
{
	public EditorLinkMCPDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore",
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"ContentBrowser",
			"EditorSubsystem",
			"EditorLinkMCPDemoRuntime",
			"HTTPServer",
			"InputCore",
			"Json",
			"JsonUtilities",
			"KismetCompiler",
			"MessageLog",
			"Projects",
			"Settings",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure"
		});
	}
}

