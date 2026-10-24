// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NodeToCode : ModuleRules
{
	public NodeToCode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Get the absolute path to the ThirdParty directory
		// Using ModuleDirectory (Source folder) and navigating up to plugin root
		// This is more robust than PluginDirectory when using symlinks/junctions
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty", "cpp-httplib"));
        
		PrivateIncludePaths.AddRange(
			new string[] {
				ThirdPartyPath  // For httplib.h
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
				"EditorFramework",
				"BlueprintGraph",
				"Slate",
				"SlateCore",
				"Kismet",
				"GraphEditor",
				"HTTP",
				"UMG",
				"ToolMenus",
				"ApplicationCore",
				"Projects",
				"HTTPServer"
			}
		);
        
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DeveloperSettings",
				"Blutility",
				"UMGEditor",
				"KismetWidgets",          // For action menu widgets
				"EditorSubsystem",        // For editor subsystem access
				"EditorWidgets",          // For editor widgets
				"AssetRegistry",          // For asset path resolution
				"ContentBrowserData",     // For content browser data subsystem
				"DesktopPlatform",        // For folder browser dialog in Tag Manager
				"PythonScriptPlugin",     // For Python script execution in Code Mode
				"EditorScriptingUtilities", // For EditorAssetLibrary in Python bridge
				"Sockets"                 // MCP server port check before binding (N2CMcpHttpServerManager)
			}
		);

		// --- httplib.h Integration ---
		// httplib.h (cpp-httplib, version and hash in ThirdParty/cpp-httplib/README.md) is header-only and requires C++11 at a minimum.
		// Modern UE (5.x) uses C++17 or C++20 by default, so this should be fine.
		// httplib.h handles its own Windows socket library linking via #pragma comment(lib, "ws2_32.lib").
	}
}
