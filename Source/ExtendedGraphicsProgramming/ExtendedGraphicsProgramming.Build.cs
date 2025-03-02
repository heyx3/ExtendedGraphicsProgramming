using UnrealBuildTool;

public class ExtendedGraphicsProgramming : ModuleRules
{
	public ExtendedGraphicsProgramming(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Projects",
			"Engine",
			"Slate",
			"SlateCore",	
			"Renderer", "RenderCore", "RHICore", "RHI"
		});
	}
}
