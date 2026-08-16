using UnrealBuildTool;

public class EditorLinkMCPDemoRuntime : ModuleRules
{
	public EditorLinkMCPDemoRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});
	}
}

