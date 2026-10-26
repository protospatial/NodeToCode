// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

struct FAssetData;
struct FContentBrowserItem;
class FJsonObject;

/**
 * @class FN2CMcpContentBrowserUtils
 * @brief Utility class for content browser operations in MCP tools
 * 
 * Provides common functionality for navigating, creating, and managing
 * content browser paths and assets.
 */
class FN2CMcpContentBrowserUtils
{
public:
	/**
	 * Validates a content browser path format. This is the read policy: any mounted root may be read (/Game,
	 * /Engine, any plugin's /<PluginName>). Tools that write use IsWritableContentPath as well.
	 * @param Path The path to validate (e.g., "/Game/Blueprints")
	 * @param OutErrorMsg Error message if validation fails
	 * @return True if the path is valid
	 */
	static bool ValidateContentPath(const FString& Path, FString& OutErrorMsg);

	/**
	 * The write policy of every MCP tool that creates, saves, copies, moves or renames content (card 0182). Path is a
	 * folder, package or object path. Allowed: /Game and the content mount of a project plugin (loaded from the
	 * project's Plugins folder, CanContainContent), except /NodeToCode, whose content ships with the plugin. Refused:
	 * everything else (/Engine, engine and Fab plugins, /Script, /Temp, /Memory, unmounted roots) unless the user's
	 * own Allow MCP Writes Outside Project setting is on; then any mounted root that may be saved to is allowed.
	 * Check every destination, and the source of a move or rename.
	 * @param Path The path to check (e.g., "/Game/Blueprints" or "/Game/Blueprints/BP_A.BP_A")
	 * @param OutErrorMsg Why the path is refused, naming the setting
	 * @return True if MCP tools may write there
	 */
	static bool IsWritableContentPath(const FString& Path, FString& OutErrorMsg);

	/** IsWritableContentPath with the setting given: bAllowOutsideProject stands for Allow MCP Writes Outside Project */
	static bool IsWritableContentPath(const FString& Path, bool bAllowOutsideProject, FString& OutErrorMsg);

	/** True when Path is a mount root itself (/Game, /<PluginName>), which no tool may move or rename */
	static bool IsContentMountRoot(const FString& Path);

	/** True for the Windows device names CON, PRN, AUX, NUL, COM1-9 and LPT1-9, in any case; never for Common or Components */
	static bool IsReservedDeviceName(const FString& Name);
	
	/**
	 * Checks if a content browser path exists
	 * @param Path The path to check
	 * @return True if the path exists
	 */
	static bool DoesPathExist(const FString& Path);
	
	/**
	 * Creates a folder at the specified path
	 * @param Path The path where the folder should be created
	 * @param OutErrorMsg Error message if creation fails
	 * @return True if the folder was created successfully
	 */
	static bool CreateContentFolder(const FString& Path, FString& OutErrorMsg);
	
	/**
	 * Navigates the primary content browser to the specified path
	 * @param Path The path to navigate to
	 * @return True if navigation was initiated (doesn't guarantee UI update)
	 */
	static bool NavigateToPath(const FString& Path);
	
	/**
	 * Selects an asset in the content browser
	 * @param AssetPath The full path to the asset (e.g., "/Game/Blueprints/MyAsset")
	 * @return True if the asset was found and selected
	 */
	static bool SelectAssetAtPath(const FString& AssetPath);
	
	/**
	 * Gets the currently selected paths in the content browser
	 * @param OutSelectedPaths Array to fill with selected paths
	 * @return True if any paths are selected
	 */
	static bool GetSelectedPaths(TArray<FString>& OutSelectedPaths);
	
	/**
	 * Normalizes a content browser path (removes trailing slashes, ensures proper format)
	 * @param Path The path to normalize
	 * @return The normalized path
	 */
	static FString NormalizeContentPath(const FString& Path);
	
	/**
	 * The read policy's first check: a rooted path with no ".." segment. Any mounted root may be read;
	 * ValidateContentPath then checks the root is mounted (FPackageName::IsValidLongPackageName). Writes are
	 * IsWritableContentPath's job.
	 * @param Path The path to check
	 * @return True if the path may be read
	 */
	static bool IsPathAllowed(const FString& Path);
	
	/**
	 * Enumerates items at a content browser path
	 * @param Path The path to enumerate
	 * @param bIncludeFolders Whether to include folders in results
	 * @param bIncludeFiles Whether to include files/assets in results
	 * @param OutItems Array to fill with content browser items
	 * @return True if enumeration succeeded
	 */
	static bool EnumerateItemsAtPath(const FString& Path, bool bIncludeFolders, bool bIncludeFiles, TArray<struct FContentBrowserItem>& OutItems);
	
	/**
	 * Filters content browser items by type
	 * @param Items Input array of items to filter
	 * @param FilterType Type filter (All, Blueprint, Material, Texture, StaticMesh, Folder)
	 * @param OutFilteredItems Output array of filtered items
	 */
	static void FilterItemsByType(const TArray<struct FContentBrowserItem>& Items, const FString& FilterType, TArray<struct FContentBrowserItem>& OutFilteredItems);
	
	/**
	 * Filters content browser items by name
	 * @param Items Input array of items to filter
	 * @param NameFilter Case-insensitive substring to match
	 * @param OutFilteredItems Output array of filtered items
	 */
	static void FilterItemsByName(const TArray<struct FContentBrowserItem>& Items, const FString& NameFilter, TArray<struct FContentBrowserItem>& OutFilteredItems);
	
	/**
	 * Converts a content browser item to JSON representation
	 * @param Item The item to convert
	 * @return JSON object representing the item
	 */
	static TSharedPtr<FJsonObject> ConvertItemToJson(const struct FContentBrowserItem& Item);
	
	/**
	 * Applies pagination to an array
	 * @param TotalItems Total number of items
	 * @param Page Current page (1-based)
	 * @param PageSize Items per page
	 * @param OutStartIndex Start index for pagination
	 * @param OutEndIndex End index for pagination
	 * @param OutHasMore Whether more pages exist
	 * @return True if pagination parameters are valid
	 */
	static bool CalculatePagination(int32 TotalItems, int32 Page, int32 PageSize, int32& OutStartIndex, int32& OutEndIndex, bool& OutHasMore);
	
	/**
	 * Create a JSON object for a folder (fallback when ContentBrowserItem is not available)
	 * @param FolderPath Virtual path to the folder
	 * @param FolderName Display name of the folder
	 * @return JSON object representing the folder
	 */
	static TSharedPtr<FJsonObject> CreateFolderJson(const FString& FolderPath, const FString& FolderName);
	
	/**
	 * Create a JSON object for an asset (fallback when ContentBrowserItem is not available)
	 * @param AssetData Asset data from the asset registry
	 * @return JSON object representing the asset
	 */
	static TSharedPtr<FJsonObject> CreateAssetJson(const struct FAssetData& AssetData);
	
	/**
	 * Enumerates items at a content browser path and returns them as JSON objects
	 * @param Path The path to enumerate
	 * @param bIncludeFolders Whether to include folders in results
	 * @param bIncludeFiles Whether to include files/assets in results
	 * @param OutJsonItems Array to fill with JSON objects representing items
	 * @return True if enumeration succeeded
	 */
	static bool EnumerateItemsAtPathAsJson(const FString& Path, bool bIncludeFolders, bool bIncludeFiles, TArray<TSharedPtr<FJsonObject>>& OutJsonItems);
	
	/**
	 * Filters JSON item objects by type
	 * @param Items Input array of JSON items to filter
	 * @param FilterType Type filter (All, Blueprint, Material, Texture, StaticMesh, Folder)
	 * @param OutFilteredItems Output array of filtered JSON items
	 */
	static void FilterJsonItemsByType(const TArray<TSharedPtr<FJsonObject>>& Items, const FString& FilterType, TArray<TSharedPtr<FJsonObject>>& OutFilteredItems);
	
	/**
	 * Filters JSON item objects by name
	 * @param Items Input array of JSON items to filter
	 * @param NameFilter Case-insensitive substring to match
	 * @param OutFilteredItems Output array of filtered JSON items
	 */
	static void FilterJsonItemsByName(const TArray<TSharedPtr<FJsonObject>>& Items, const FString& NameFilter, TArray<TSharedPtr<FJsonObject>>& OutFilteredItems);
	
	/**
	 * Ensures a directory exists by creating it if necessary
	 * @param Path The content browser path to ensure exists
	 * @param OutErrorMsg Error message if creation fails
	 * @return True if the directory exists or was created successfully
	 */
	static bool EnsureDirectoryExists(const FString& Path, FString& OutErrorMsg);

	/**
	 * The asset at Path, from the asset registry (card 0284). Path is an object path (/Game/A/BP.BP) or a package path
	 * (/Game/A/BP); a package path gives the package's asset named like the package, or its only asset. A redirector
	 * is not an asset here. Callers take every name and path after this from OutAsset, never from Path.
	 * @return False with a reason when there is no such asset
	 */
	static bool ResolveAsset(const FString& Path, FAssetData& OutAsset, FString& OutErrorMsg);

	/**
	 * The rules for the name of an asset a tool creates, renames or copies to (card 0284): not empty, and none of the
	 * characters object names and package names refuse (INVALID_OBJECTNAME_CHARACTERS, INVALID_LONGPACKAGE_CHARACTERS:
	 * '.', '/', '\\', spaces, ...), and a name a file can have. Check the raw name before composing any path with it.
	 */
	static bool ValidateNewAssetName(const FString& Name, FString& OutErrorMsg);

	/** True when the package PackageName (/Game/A/BP) exists, in the asset registry or on disk, compared case-insensitively */
	static bool DoesAssetPackageExist(const FString& PackageName);

	/**
	 * Creates Folder and its missing parents right before a mutation (card 0284). OutCreatedRoot is the highest folder
	 * this call created, empty when Folder already existed: pass it to RemoveFolderCreatedForMutation if the mutation fails.
	 */
	static bool CreateFolderForMutation(const FString& Folder, FString& OutCreatedRoot, FString& OutErrorMsg);

	/** After a failed mutation: deletes CreatedRoot (from CreateFolderForMutation) unless it holds an asset. Empty does nothing. */
	static void RemoveFolderCreatedForMutation(const FString& CreatedRoot);

	/** The file source control knows the package by (USourceControlHelpers::PackageFilename): /Game/A/BP gives <Project>/Content/A/BP.uasset */
	static FString SourceControlFilename(const FString& PackageName);

	/** True when source control is on and PackageName is checked out by someone else */
	static bool IsCheckedOutByAnother(const FString& PackageName);

private:
	// Prevent instantiation
	FN2CMcpContentBrowserUtils() = delete;
};

/**
 * Collects the warnings and errors the engine logs while it lives (UEditorAssetSubsystem and the asset tools say why a
 * rename or duplicate failed only in the log), so a tool can return the engine's reason instead of a guess (card 0284).
 * Game thread only.
 */
class FN2CMcpEngineErrorCapture : public FOutputDevice
{
public:
	FN2CMcpEngineErrorCapture();
	virtual ~FN2CMcpEngineErrorCapture() override;
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;

	/** The captured lines, errors first, joined; empty when the engine said nothing */
	FString GetReason();

private:
	TArray<FString> Errors;
	TArray<FString> Warnings;
};
