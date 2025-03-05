// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

using UnrealBuildTool;

public class NodeToCode : ModuleRules
{
	public NodeToCode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
        
		PrivateIncludePaths.AddRange(
			new string[] {
				"NodeToCode/Private/Utils/Validators",
				"NodeToCode/Private/Utils/Processors",
				"NodeToCode/Public/Utils/Processors"
			}
		);
        
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine", 
				"InputCore",
				"Json",
				"UnrealEd",
				"BlueprintGraph",
				"Slate",
				"SlateCore",
				"Kismet",
				"GraphEditor",
				"HTTP",
				"UMG",
				"ToolMenus",
				"ApplicationCore",
				"Projects"
			}
		);
        
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DeveloperSettings", "Blutility", "UMGEditor"
			}
		);
	}
}
