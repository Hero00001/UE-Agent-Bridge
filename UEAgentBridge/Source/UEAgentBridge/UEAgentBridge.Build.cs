// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UEAgentBridge : ModuleRules
{
	public UEAgentBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"HTTPServer",
			"HTTP",
			"Json",
			"UnrealEd",
			"PythonScriptPlugin",
			"Projects"
		});
	}
}
