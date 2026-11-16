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
			"HeadMountedDisplay", // UMotionControllerComponent, HMD function library
			"EnhancedInput"
		});

		// T-0005: depending on the locked engine version, "XRBase" may also be required.
	}
}
