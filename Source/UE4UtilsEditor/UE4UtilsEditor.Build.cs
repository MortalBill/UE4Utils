// Copyright 2021 Cold Symmetry. MIT License

using UnrealBuildTool;

public class UE4UtilsEditor : ModuleRules
{
	public UE4UtilsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"UE4Utils"
		});

		PrivateDependencyModuleNames.AddRange(
		new string[]
		{
			"Engine", "UnrealEd", "KismetCompiler", "BlueprintGraph",
		});
	}
}
