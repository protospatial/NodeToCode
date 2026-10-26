// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NodeToCode : ModuleRules
{
	public NodeToCode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// cpp-httplib (ThirdParty/cpp-httplib). No source file includes httplib.h since the port+1 notification server
		// was removed (card 0113), so nothing of it is compiled. The vendored copy and this include path stay for the
		// UE 5.6/5.7 /mcp transport that card 0111 adds if decision 0085 picks httplib streaming; if 0085 keeps the held
		// JSON response, delete ThirdParty/cpp-httplib with this path, its NOTICE section and its FilterPlugin.ini and
		// Scripts/package-release.ps1 entries.
		// ModuleDirectory (Source folder) up to the plugin root is more robust than PluginDirectory with symlinks/junctions.
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
				"Sockets",                // MCP server port check before binding (N2CMcpHttpServerManager)
				"SubobjectDataInterface"  // USubobjectDataSubsystem for the component tools (card 0167)
			}
		);

		// Windows Credential Manager (CredReadW, CredWriteW, CredDeleteW, CredEnumerateW) holds the API keys on Windows
		// (FN2CCredentialManagerBackend, card 0161). Other platforms keep the per-user secrets file and link nothing extra.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Advapi32.lib");
		}

		// --- httplib.h Integration (unused today, see the include path above) ---
		// httplib.h (cpp-httplib, version and hash in ThirdParty/cpp-httplib/README.md) is header-only and requires C++11 at a minimum.
		// httplib.h handles its own Windows socket library linking via #pragma comment(lib, "ws2_32.lib").
	}
}
