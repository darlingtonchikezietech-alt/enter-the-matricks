using UnrealBuildTool;

public class ETM : ModuleRules
{
	public ETM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"HeadMountedDisplay", // UMotionControllerComponent
			"XRBase",             // UHeadMountedDisplayFunctionLibrary (XRBase plugin in UE 5.8)
			"EnhancedInput"
		});
	}
}
