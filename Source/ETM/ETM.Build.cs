using UnrealBuildTool;

public class ETM : ModuleRules
{
	public ETM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// No unity build: test files keep their helpers in file-local anonymous namespaces
		// (Frame72, FTestWorld, ...), which collide when UBT merges .cpp files together. It also
		// keeps every file honest about its own includes. The module is small, so the cost is low.
		bUseUnity = false;

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
