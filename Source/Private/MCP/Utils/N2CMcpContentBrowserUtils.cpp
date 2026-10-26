// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpContentBrowserUtils.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor/ContentBrowser/Public/IContentBrowserSingleton.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Editor.h"
#include "AssetRegistry/AssetData.h"
#include "Utils/N2CLogger.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataFilter.h"
#include "ContentBrowserItem.h"
#include "Dom/JsonObject.h"
#include "Core/N2CUserSettings.h"
#include "Interfaces/IPluginManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"

bool FN2CMcpContentBrowserUtils::ValidateContentPath(const FString& Path, FString& OutErrorMsg)
{
	// Check if empty
	if (Path.IsEmpty())
	{
		OutErrorMsg = TEXT("Path cannot be empty");
		return false;
	}
	
	// Normalize the path first
	FString NormalizedPath = NormalizeContentPath(Path);
	
	// Check if it starts with /
	if (!NormalizedPath.StartsWith(TEXT("/")))
	{
		OutErrorMsg = TEXT("Path must start with '/'");
		return false;
	}
	
	// Check if it's in an allowed directory
	if (!IsPathAllowed(NormalizedPath))
	{
		OutErrorMsg = FString::Printf(TEXT("Path '%s' is not in an allowed content directory"), *NormalizedPath);
		return false;
	}
	
	// Validate package name format
	FText FailureReason;
	if (!FPackageName::IsValidLongPackageName(NormalizedPath, false, &FailureReason))
	{
		OutErrorMsg = FString::Printf(TEXT("Invalid package path: %s"), *FailureReason.ToString());
		return false;
	}
	
	return true;
}

bool FN2CMcpContentBrowserUtils::DoesPathExist(const FString& Path)
{
	// Get the editor asset subsystem
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (!EditorAssetSubsystem)
	{
		return false;
	}
	
	// Check if the directory exists
	return EditorAssetSubsystem->DoesDirectoryExist(Path);
}

bool FN2CMcpContentBrowserUtils::CreateContentFolder(const FString& Path, FString& OutErrorMsg)
{
	// Validate the path first: a mounted root, and one MCP tools may write to
	if (!ValidateContentPath(Path, OutErrorMsg) || !IsWritableContentPath(Path, OutErrorMsg))
	{
		return false;
	}
	
	// Get the editor asset subsystem
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (!EditorAssetSubsystem)
	{
		OutErrorMsg = TEXT("Failed to get EditorAssetSubsystem");
		return false;
	}
	
	// Check if it already exists
	if (DoesPathExist(Path))
	{
		OutErrorMsg = FString::Printf(TEXT("Path already exists: %s"), *Path);
		return false;
	}
	
	// Make the directory
	if (!EditorAssetSubsystem->MakeDirectory(Path))
	{
		OutErrorMsg = FString::Printf(TEXT("Failed to create directory: %s"), *Path);
		return false;
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Created content folder: %s"), *Path), EN2CLogSeverity::Info);
	return true;
}

bool FN2CMcpContentBrowserUtils::NavigateToPath(const FString& Path)
{
	FContentBrowserModule& ContentBrowserModule = 
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	
	IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();
	
	// Create array of folders to sync to
	TArray<FString> FoldersToSync;
	FoldersToSync.Add(Path);
	
	// Sync the primary content browser to the folder
	ContentBrowser.SyncBrowserToFolders(FoldersToSync);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Navigated content browser to: %s"), *Path), EN2CLogSeverity::Info);
	
	// SyncBrowserToFolders doesn't return success/failure, so we assume success
	return true;
}

bool FN2CMcpContentBrowserUtils::SelectAssetAtPath(const FString& AssetPath)
{
	// A package path (/Game/A/BP) or an object path (/Game/A/BP.BP): the registry's asset either way (card 0284; a
	// package path used to be looked up as an object path, which finds no asset)
	FAssetData AssetData;
	FString ErrorMsg;
	if (!ResolveAsset(AssetPath, AssetData, ErrorMsg))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SelectAssetAtPath: %s"), *ErrorMsg));
		return false;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>{ AssetData });

	FN2CLogger::Get().Log(FString::Printf(TEXT("Selected asset: %s"), *AssetData.GetObjectPathString()), EN2CLogSeverity::Info);
	return true;
}

bool FN2CMcpContentBrowserUtils::GetSelectedPaths(TArray<FString>& OutSelectedPaths)
{
	FContentBrowserModule& ContentBrowserModule = 
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	
	// Get the selected paths from the content browser
	ContentBrowserModule.Get().GetSelectedPathViewFolders(OutSelectedPaths);
	
	return OutSelectedPaths.Num() > 0;
}

FString FN2CMcpContentBrowserUtils::NormalizeContentPath(const FString& Path)
{
	FString NormalizedPath = Path;
	
	// Replace backslashes with forward slashes
	NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	
	// Remove trailing slash if present (unless it's just "/")
	if (NormalizedPath.Len() > 1 && NormalizedPath.EndsWith(TEXT("/")))
	{
		NormalizedPath = NormalizedPath.Left(NormalizedPath.Len() - 1);
	}
	
	// Ensure it starts with /
	if (!NormalizedPath.StartsWith(TEXT("/")))
	{
		NormalizedPath = TEXT("/") + NormalizedPath;
	}
	
	return NormalizedPath;
}

bool FN2CMcpContentBrowserUtils::IsPathAllowed(const FString& Path)
{
	// Any mounted root may be read (/Game, /Engine, a plugin's /<PluginName>): ValidateContentPath checks the root is
	// mounted. Here only the shape: rooted, and no ".." that could climb out of it. Writes go through
	// IsWritableContentPath.
	return Path.StartsWith(TEXT("/")) && !Path.Contains(TEXT("..")) && !Path.Contains(TEXT("\\"));
}

namespace N2CContentPathPolicy
{
	/** NodeToCode's own content mount: it ships with the plugin, so a write there is lost (or shipped) on update */
	const TCHAR* PluginMountName = TEXT("NodeToCode");

	/** The package part of a folder, package or object path: "/Game/A/B.B:Sub" gives "/Game/A/B" */
	FString PackagePart(const FString& NormalizedPath)
	{
		FString Result = NormalizedPath;
		int32 Index = INDEX_NONE;
		if (Result.FindChar(TEXT('.'), Index))
		{
			Result.LeftInline(Index);
		}
		if (Result.FindChar(TEXT(':'), Index))
		{
			Result.LeftInline(Index);
		}
		return Result;
	}

	/** The mount point Path is under ("Game", "Engine", "<PluginName>"), or NAME_None when no root is mounted there */
	FName MountOf(const FString& PackagePath)
	{
		return FPackageName::GetPackageMountPoint(PackagePath + TEXT("/"));
	}

	/** The plugin whose content is mounted at /MountName/, or null */
	TSharedPtr<IPlugin> PluginOfMount(const FString& MountName)
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(MountName);
		if (Plugin.IsValid() && Plugin->CanContainContent()
			&& Plugin->GetMountedAssetPath().Equals(TEXT("/") + MountName + TEXT("/"), ESearchCase::IgnoreCase))
		{
			return Plugin;
		}
		return nullptr;
	}
}

bool FN2CMcpContentBrowserUtils::IsWritableContentPath(const FString& Path, FString& OutErrorMsg)
{
	return IsWritableContentPath(Path, UN2CUserSettings::AreMcpWritesOutsideProjectAllowed(), OutErrorMsg);
}

bool FN2CMcpContentBrowserUtils::IsWritableContentPath(const FString& Path, bool bAllowOutsideProject, FString& OutErrorMsg)
{
	using namespace N2CContentPathPolicy;

	if (Path.IsEmpty())
	{
		OutErrorMsg = TEXT("Path cannot be empty");
		return false;
	}
	// Rooted, as the callers use the path as given (move-asset and create-blueprint-class normalize first)
	if (!Path.StartsWith(TEXT("/")) && !Path.StartsWith(TEXT("\\")))
	{
		OutErrorMsg = FString::Printf(TEXT("Path '%s' must start with '/', for example /Game/Folder"), *Path);
		return false;
	}
	// ".." before PackagePart, which cuts at the first dot
	const FString Normalized = NormalizeContentPath(Path);
	const FString PackagePath = PackagePart(Normalized);
	if (Normalized.Contains(TEXT("..")) || PackagePath.Contains(TEXT("//")))
	{
		OutErrorMsg = FString::Printf(TEXT("Path '%s' may not contain '..' or an empty folder name"), *Path);
		return false;
	}

	const FName Mount = MountOf(PackagePath);
	if (Mount.IsNone())
	{
		OutErrorMsg = FString::Printf(TEXT("Path '%s' is not under a mounted content root (for example /Game)"), *Path);
		return false;
	}
	const FString MountName = Mount.ToString();

	// The project's own content: /Game, and a project plugin's content other than NodeToCode's
	if (MountName.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	const TSharedPtr<IPlugin> Plugin = PluginOfMount(MountName);
	const bool bNodeToCode = MountName.Equals(PluginMountName, ESearchCase::IgnoreCase);
	if (Plugin.IsValid() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project && !bNodeToCode)
	{
		return true;
	}

	// Everywhere else only with the user's own opt-in, and never to a root the engine does not save to (/Script,
	// /Temp, /Memory)
	if (bAllowOutsideProject && FPackageName::IsValidLongPackageName(PackagePath, /*bIncludeReadOnlyRoots*/ false))
	{
		return true;
	}

	FString Whose;
	if (bNodeToCode)
	{
		Whose = TEXT("NodeToCode's own content, which ships with the plugin");
	}
	else if (MountName.Equals(TEXT("Engine"), ESearchCase::IgnoreCase))
	{
		Whose = TEXT("the engine's content, shared by every project on this machine");
	}
	else if (Plugin.IsValid())
	{
		Whose = FString::Printf(TEXT("the content of the %s plugin, installed with the engine and not part of this project"), *Plugin->GetName());
	}
	else
	{
		Whose = TEXT("not this project's content");
	}
	OutErrorMsg = FString::Printf(
		TEXT("Cannot write to '%s': /%s is %s. MCP tools write only to /Game and to the content of this project's own plugins (/<PluginName>)%s"),
		*Path, *MountName, *Whose,
		bAllowOutsideProject ? TEXT(", and the engine never saves to this root")
			: TEXT(". To allow it, turn on Allow MCP Writes Outside Project in Project Settings > Plugins > Node to Code (User) and restart the editor"));
	return false;
}

bool FN2CMcpContentBrowserUtils::IsContentMountRoot(const FString& Path)
{
	const FString PackagePath = NormalizeContentPath(Path);
	const FName Mount = N2CContentPathPolicy::MountOf(PackagePath);
	return !Mount.IsNone() && PackagePath.Equals(TEXT("/") + Mount.ToString(), ESearchCase::IgnoreCase);
}

bool FN2CMcpContentBrowserUtils::IsReservedDeviceName(const FString& Name)
{
	for (const TCHAR* Device : { TEXT("CON"), TEXT("PRN"), TEXT("AUX"), TEXT("NUL") })
	{
		if (Name.Equals(Device, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	// COM1-COM9 and LPT1-LPT9 exactly: Common, Components, COM10 and Lpthing are ordinary names
	return Name.Len() == 4 && (Name.StartsWith(TEXT("COM"), ESearchCase::IgnoreCase) || Name.StartsWith(TEXT("LPT"), ESearchCase::IgnoreCase))
		&& Name[3] >= TEXT('1') && Name[3] <= TEXT('9');
}

bool FN2CMcpContentBrowserUtils::EnumerateItemsAtPath(const FString& Path, bool bIncludeFolders, bool bIncludeFiles, TArray<FContentBrowserItem>& OutItems)
{
	OutItems.Reset();
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: Path='%s', IncludeFolders=%d, IncludeFiles=%d"), 
		*Path, bIncludeFolders ? 1 : 0, bIncludeFiles ? 1 : 0), EN2CLogSeverity::Info);
	
	// Use a hybrid approach: AssetRegistry for assets, EditorAssetSubsystem for folders
	
	// First, get folders if requested
	if (bIncludeFolders)
	{
		UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
		if (EditorAssetSubsystem)
		{
			// Get sub-folders directly
			TArray<FString> SubFolders;
			EditorAssetSubsystem->ListAssets(Path, false, true); // This doesn't return anything, we need a different approach
			
			// Use IFileManager to find folders
			TArray<FString> FoundFolders;
			FString DiskPath;
			if (FPackageName::TryConvertLongPackageNameToFilename(Path, DiskPath))
			{
				// Make sure path ends with / for directory search
				if (!DiskPath.EndsWith(TEXT("/")))
				{
					DiskPath += TEXT("/");
				}
				
				// Find all subdirectories
				IFileManager::Get().FindFiles(FoundFolders, *DiskPath, false, true);
				
				// Convert folder names to ContentBrowserItems
				UContentBrowserDataSubsystem* ContentBrowserData = GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>();
				if (ContentBrowserData)
				{
					for (const FString& FolderName : FoundFolders)
					{
						FString SubFolderPath = Path / FolderName;
						
						// Try to get folder item from content browser data
						FContentBrowserItem FolderItem = ContentBrowserData->GetItemAtPath(FName(*SubFolderPath), EContentBrowserItemTypeFilter::IncludeFolders);
						if (FolderItem.IsValid())
						{
							OutItems.Add(FolderItem);
							FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: Found folder '%s'"), *FolderName), EN2CLogSeverity::Info);
						}
						else
						{
							// Log warning but continue - we'll handle missing items differently
							FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: Could not get ContentBrowserItem for folder '%s'"), *SubFolderPath), EN2CLogSeverity::Warning);
						}
					}
				}
			}
		}
	}
	
	// Then, get assets if requested
	if (bIncludeFiles)
	{
		// Get the asset registry
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		
		// Set up the filter
		FARFilter Filter;
		Filter.bRecursivePaths = false; // Only get immediate children
		Filter.PackagePaths.Add(FName(*Path));
		
		// Get assets
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: AssetRegistry found %d assets"), AssetDataList.Num()), EN2CLogSeverity::Info);
		
		// Convert to ContentBrowserItems
		UContentBrowserDataSubsystem* ContentBrowserData = GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>();
		if (ContentBrowserData)
		{
			for (const FAssetData& AssetData : AssetDataList)
			{
				// Try to get the item from content browser data using the object path
				FString ObjectPath = AssetData.GetObjectPathString();
				FContentBrowserItem AssetItem = ContentBrowserData->GetItemAtPath(FName(*ObjectPath), EContentBrowserItemTypeFilter::IncludeFiles);
				if (AssetItem.IsValid())
				{
					OutItems.Add(AssetItem);
					FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: Found asset '%s'"), *AssetData.AssetName.ToString()), EN2CLogSeverity::Info);
				}
				else
				{
					FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: Could not get ContentBrowserItem for asset '%s'"), *AssetData.GetSoftObjectPath().ToString()), EN2CLogSeverity::Warning);
				}
			}
		}
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPath: Total items found: %d"), OutItems.Num()), EN2CLogSeverity::Info);
	
	return true;
}

void FN2CMcpContentBrowserUtils::FilterItemsByType(const TArray<FContentBrowserItem>& Items, const FString& FilterType, TArray<FContentBrowserItem>& OutFilteredItems)
{
	OutFilteredItems.Reset();
	
	// If filter is "All", just copy everything
	if (FilterType.Equals(TEXT("All"), ESearchCase::IgnoreCase))
	{
		OutFilteredItems = Items;
		return;
	}
	
	// Filter by specific type
	for (const FContentBrowserItem& Item : Items)
	{
		bool bInclude = false;
		
		if (Item.IsFolder())
		{
			// Check if we want folders
			if (FilterType.Equals(TEXT("Folder"), ESearchCase::IgnoreCase))
			{
				bInclude = true;
			}
		}
		else
		{
			// Get asset data to check type
			FAssetData AssetData;
			if (Item.Legacy_TryGetAssetData(AssetData))
			{
				FString AssetClass = AssetData.AssetClassPath.GetAssetName().ToString();
				
				// Check against filter type
				if (FilterType.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase) && AssetClass.Contains(TEXT("Blueprint")))
				{
					bInclude = true;
				}
				else if (FilterType.Equals(TEXT("Material"), ESearchCase::IgnoreCase) && 
					(AssetClass.Contains(TEXT("Material")) && !AssetClass.Contains(TEXT("MaterialFunction"))))
				{
					bInclude = true;
				}
				else if (FilterType.Equals(TEXT("Texture"), ESearchCase::IgnoreCase) && AssetClass.Contains(TEXT("Texture")))
				{
					bInclude = true;
				}
				else if (FilterType.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase) && AssetClass.Contains(TEXT("StaticMesh")))
				{
					bInclude = true;
				}
			}
		}
		
		if (bInclude)
		{
			OutFilteredItems.Add(Item);
		}
	}
}

void FN2CMcpContentBrowserUtils::FilterItemsByName(const TArray<FContentBrowserItem>& Items, const FString& NameFilter, TArray<FContentBrowserItem>& OutFilteredItems)
{
	OutFilteredItems.Reset();
	
	// If no filter, copy everything
	if (NameFilter.IsEmpty())
	{
		OutFilteredItems = Items;
		return;
	}
	
	// Filter by name (case-insensitive substring match)
	for (const FContentBrowserItem& Item : Items)
	{
		FString ItemName = Item.GetDisplayName().ToString();
		if (ItemName.Contains(NameFilter, ESearchCase::IgnoreCase))
		{
			OutFilteredItems.Add(Item);
		}
	}
}

TSharedPtr<FJsonObject> FN2CMcpContentBrowserUtils::ConvertItemToJson(const FContentBrowserItem& Item)
{
	TSharedPtr<FJsonObject> ItemJson = MakeShareable(new FJsonObject);
	
	// Basic info
	ItemJson->SetStringField(TEXT("path"), Item.GetVirtualPath().ToString());
	ItemJson->SetStringField(TEXT("name"), Item.GetDisplayName().ToString());
	ItemJson->SetBoolField(TEXT("is_folder"), Item.IsFolder());
	
	if (!Item.IsFolder())
	{
		// Get asset-specific information
		FAssetData AssetData;
		if (Item.Legacy_TryGetAssetData(AssetData))
		{
			// Asset type from class name
			FString AssetClassName = AssetData.AssetClassPath.GetAssetName().ToString();
			ItemJson->SetStringField(TEXT("type"), AssetClassName);
			
			// Full class path
			ItemJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
			
			// Additional metadata if needed
			if (AssetData.GetClass())
			{
				ItemJson->SetStringField(TEXT("native_class"), AssetData.GetClass()->GetName());
			}
		}
		else
		{
			// Fallback if we can't get asset data
			ItemJson->SetStringField(TEXT("type"), TEXT("UnknownFile"));
			ItemJson->SetStringField(TEXT("class"), TEXT("UnknownFileClass"));
		}
	}
	else
	{
		// It's a folder
		ItemJson->SetStringField(TEXT("type"), TEXT("Folder"));
		ItemJson->SetStringField(TEXT("class"), TEXT("Folder"));
	}
	
	return ItemJson;
}

bool FN2CMcpContentBrowserUtils::CalculatePagination(int32 TotalItems, int32 Page, int32 PageSize, int32& OutStartIndex, int32& OutEndIndex, bool& OutHasMore)
{
	// Validate inputs
	if (Page < 1 || PageSize < 1)
	{
		FN2CLogger::Get().LogWarning(TEXT("Invalid pagination parameters: Page and PageSize must be >= 1"));
		return false;
	}
	
	// Calculate indices in 64 bits so a huge page number cannot wrap negative
	const int64 Start = (static_cast<int64>(Page) - 1) * static_cast<int64>(PageSize);
	if (Start >= TotalItems && TotalItems > 0)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Page %d is beyond available items"), Page));
		return false;
	}
	OutStartIndex = static_cast<int32>(FMath::Clamp<int64>(Start, 0, FMath::Max(TotalItems, 0)));
	OutEndIndex = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(OutStartIndex) + PageSize, TotalItems));
	
	// Check if start is beyond total items
	if (OutStartIndex >= TotalItems && TotalItems > 0)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Page %d is beyond available items"), Page));
		return false;
	}
	
	// Determine if there are more pages
	OutHasMore = OutEndIndex < TotalItems;
	
	return true;
}

TSharedPtr<FJsonObject> FN2CMcpContentBrowserUtils::CreateFolderJson(const FString& FolderPath, const FString& FolderName)
{
	TSharedPtr<FJsonObject> ItemJson = MakeShareable(new FJsonObject);
	
	ItemJson->SetStringField(TEXT("path"), FolderPath);
	ItemJson->SetStringField(TEXT("name"), FolderName);
	ItemJson->SetBoolField(TEXT("is_folder"), true);
	ItemJson->SetStringField(TEXT("type"), TEXT("Folder"));
	ItemJson->SetStringField(TEXT("class"), TEXT("Folder"));
	
	return ItemJson;
}

TSharedPtr<FJsonObject> FN2CMcpContentBrowserUtils::CreateAssetJson(const FAssetData& AssetData)
{
	TSharedPtr<FJsonObject> ItemJson = MakeShareable(new FJsonObject);
	
	// Basic info
	ItemJson->SetStringField(TEXT("path"), AssetData.PackageName.ToString());
	ItemJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
	ItemJson->SetBoolField(TEXT("is_folder"), false);
	
	// Asset type from class name
	FString AssetClassName = AssetData.AssetClassPath.GetAssetName().ToString();
	ItemJson->SetStringField(TEXT("type"), AssetClassName);
	
	// Full class path
	ItemJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
	
	// Additional metadata if available
	if (AssetData.GetClass())
	{
		ItemJson->SetStringField(TEXT("native_class"), AssetData.GetClass()->GetName());
	}
	
	return ItemJson;
}

bool FN2CMcpContentBrowserUtils::EnumerateItemsAtPathAsJson(const FString& Path, bool bIncludeFolders, bool bIncludeFiles, TArray<TSharedPtr<FJsonObject>>& OutJsonItems)
{
	OutJsonItems.Reset();
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: Path='%s', IncludeFolders=%d, IncludeFiles=%d"), 
		*Path, bIncludeFolders ? 1 : 0, bIncludeFiles ? 1 : 0), EN2CLogSeverity::Info);
	
	// Get folders if requested
	if (bIncludeFolders)
	{
		UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
		if (EditorAssetSubsystem)
		{
			// Get all sub-paths (which includes both folders and assets)
			TArray<FString> SubPaths = EditorAssetSubsystem->ListAssets(Path, false, false);
			
			// Track unique folders we've already added
			TSet<FString> AddedFolders;
			
			// Extract folder paths from asset paths
			for (const FString& SubPath : SubPaths)
			{
				// Get the directory part of the path
				FString Directory = FPaths::GetPath(SubPath);
				
				// Check if this is a direct subfolder of our path
				if (Directory.StartsWith(Path) && Directory != Path)
				{
					// Extract just the immediate subfolder
					FString RelativePath = Directory.Mid(Path.Len());
					if (RelativePath.StartsWith(TEXT("/")))
					{
						RelativePath = RelativePath.Mid(1);
					}
					
					// Get just the first folder level
					int32 SlashIndex;
					if (RelativePath.FindChar('/', SlashIndex))
					{
						RelativePath = RelativePath.Left(SlashIndex);
					}
					
					if (!RelativePath.IsEmpty() && !AddedFolders.Contains(RelativePath))
					{
						AddedFolders.Add(RelativePath);
						FString FolderPath = Path / RelativePath;
						TSharedPtr<FJsonObject> FolderJson = CreateFolderJson(FolderPath, RelativePath);
						OutJsonItems.Add(FolderJson);
						FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: Found folder '%s'"), *RelativePath), EN2CLogSeverity::Info);
					}
				}
			}
			
			// Also check for empty folders using DoesDirectoryExist
			TArray<FString> DirectoryNames;
			FString DiskPath;
			if (FPackageName::TryConvertLongPackageNameToFilename(Path, DiskPath))
			{
				if (!DiskPath.EndsWith(TEXT("/")))
				{
					DiskPath += TEXT("/");
				}
				
				// Find all subdirectories
				IFileManager::Get().FindFiles(DirectoryNames, *(DiskPath + TEXT("*")), false, true);
				
				for (const FString& DirName : DirectoryNames)
				{
					if (!AddedFolders.Contains(DirName))
					{
						FString FolderPath = Path / DirName;
						TSharedPtr<FJsonObject> FolderJson = CreateFolderJson(FolderPath, DirName);
						OutJsonItems.Add(FolderJson);
						FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: Found empty folder '%s'"), *DirName), EN2CLogSeverity::Info);
					}
				}
			}
		}
	}
	
	// Get assets if requested
	if (bIncludeFiles)
	{
		// Get the asset registry
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		
		// Set up the filter
		FARFilter Filter;
		Filter.bRecursivePaths = false;
		Filter.PackagePaths.Add(FName(*Path));
		
		// Get assets
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: AssetRegistry found %d assets"), AssetDataList.Num()), EN2CLogSeverity::Info);
		
		// Convert to JSON objects
		for (const FAssetData& AssetData : AssetDataList)
		{
			TSharedPtr<FJsonObject> AssetJson = CreateAssetJson(AssetData);
			OutJsonItems.Add(AssetJson);
			FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: Found asset '%s' of type '%s'"), 
				*AssetData.AssetName.ToString(), *AssetData.AssetClassPath.GetAssetName().ToString()), EN2CLogSeverity::Info);
		}
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: Total items found: %d"), OutJsonItems.Num()), EN2CLogSeverity::Info);
	
	return true;
}

void FN2CMcpContentBrowserUtils::FilterJsonItemsByType(const TArray<TSharedPtr<FJsonObject>>& Items, const FString& FilterType, TArray<TSharedPtr<FJsonObject>>& OutFilteredItems)
{
	OutFilteredItems.Reset();
	
	// If filter is "All", just copy everything
	if (FilterType.Equals(TEXT("All"), ESearchCase::IgnoreCase))
	{
		OutFilteredItems = Items;
		return;
	}
	
	// Filter by specific type
	for (const TSharedPtr<FJsonObject>& Item : Items)
	{
		if (!Item.IsValid())
		{
			continue;
		}
		
		bool bInclude = false;
		bool bIsFolder = false;
		Item->TryGetBoolField(TEXT("is_folder"), bIsFolder);
		
		if (bIsFolder)
		{
			// Check if we want folders
			if (FilterType.Equals(TEXT("Folder"), ESearchCase::IgnoreCase))
			{
				bInclude = true;
			}
		}
		else
		{
			// Get asset type
			FString AssetType;
			if (Item->TryGetStringField(TEXT("type"), AssetType))
			{
				// Check against filter type
				if (FilterType.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase) && AssetType.Contains(TEXT("Blueprint")))
				{
					bInclude = true;
				}
				else if (FilterType.Equals(TEXT("Material"), ESearchCase::IgnoreCase) && 
					(AssetType.Contains(TEXT("Material")) && !AssetType.Contains(TEXT("MaterialFunction"))))
				{
					bInclude = true;
				}
				else if (FilterType.Equals(TEXT("Texture"), ESearchCase::IgnoreCase) && AssetType.Contains(TEXT("Texture")))
				{
					bInclude = true;
				}
				else if (FilterType.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase) && AssetType.Contains(TEXT("StaticMesh")))
				{
					bInclude = true;
				}
			}
		}
		
		if (bInclude)
		{
			OutFilteredItems.Add(Item);
		}
	}
}

void FN2CMcpContentBrowserUtils::FilterJsonItemsByName(const TArray<TSharedPtr<FJsonObject>>& Items, const FString& NameFilter, TArray<TSharedPtr<FJsonObject>>& OutFilteredItems)
{
	OutFilteredItems.Reset();
	
	// If no filter, copy everything
	if (NameFilter.IsEmpty())
	{
		OutFilteredItems = Items;
		return;
	}
	
	// Filter by name (case-insensitive substring match)
	for (const TSharedPtr<FJsonObject>& Item : Items)
	{
		if (!Item.IsValid())
		{
			continue;
		}
		
		FString ItemName;
		if (Item->TryGetStringField(TEXT("name"), ItemName))
		{
			if (ItemName.Contains(NameFilter, ESearchCase::IgnoreCase))
			{
				OutFilteredItems.Add(Item);
			}
		}
	}
}

bool FN2CMcpContentBrowserUtils::EnsureDirectoryExists(const FString& Path, FString& OutErrorMsg)
{
	// Validate the path first
	if (!ValidateContentPath(Path, OutErrorMsg))
	{
		return false;
	}
	
	// Check if the path already exists
	if (DoesPathExist(Path))
	{
		return true;
	}
	
	// Try to create the folder
	return CreateContentFolder(Path, OutErrorMsg);
}

bool FN2CMcpContentBrowserUtils::ResolveAsset(const FString& Path, FAssetData& OutAsset, FString& OutErrorMsg)
{
	OutAsset = FAssetData();
	if (Path.IsEmpty())
	{
		OutErrorMsg = TEXT("Asset path cannot be empty");
		return false;
	}
	const FString Normalized = NormalizeContentPath(Path);

	// Split /Game/A/BP.BP into the package and the object name; a package path has no object name
	FString PackageName = Normalized;
	FString ObjectName;
	int32 DotIndex = INDEX_NONE;
	if (Normalized.FindChar(TEXT('.'), DotIndex))
	{
		PackageName = Normalized.Left(DotIndex);
		ObjectName = Normalized.Mid(DotIndex + 1);
	}
	const TCHAR* Expected = TEXT("Expected /Game/Folder/AssetName or /Game/Folder/AssetName.AssetName");
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots*/ true, &Reason))
	{
		OutErrorMsg = FString::Printf(TEXT("Invalid asset path '%s': %s. %s"), *Path, *Reason.ToString(), Expected);
		return false;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPackageName(FName(*PackageName), Assets);

	// The object asked for, else the asset named like its package, else the package's only asset
	const FString WantedName = ObjectName.IsEmpty() ? FPackageName::GetShortName(PackageName) : ObjectName;
	const FAssetData* Found = Assets.FindByPredicate([&WantedName](const FAssetData& Asset)
	{
		return Asset.AssetName.ToString().Equals(WantedName, ESearchCase::IgnoreCase);
	});
	if (!Found && ObjectName.IsEmpty() && Assets.Num() == 1)
	{
		Found = &Assets[0];
	}
	if (!Found)
	{
		OutErrorMsg = FString::Printf(TEXT("Asset not found: %s. %s"), *Normalized, Expected);
		return false;
	}
	if (Found->IsRedirector())
	{
		OutErrorMsg = FString::Printf(TEXT("%s is a redirector a move left behind, not an asset: use the asset's new path"), *Normalized);
		return false;
	}
	OutAsset = *Found;
	return true;
}

bool FN2CMcpContentBrowserUtils::ValidateNewAssetName(const FString& Name, FString& OutErrorMsg)
{
	if (Name.IsEmpty())
	{
		OutErrorMsg = TEXT("The asset name cannot be empty");
		return false;
	}
	// The asset name is the object name, and the last part of the package name and of the file name
	FText Reason;
	const FString InvalidCharacters = FString(INVALID_OBJECTNAME_CHARACTERS) + INVALID_LONGPACKAGE_CHARACTERS;
	if (!FName::IsValidXName(Name, InvalidCharacters, &Reason))
	{
		OutErrorMsg = FString::Printf(TEXT("Invalid asset name '%s': %s. Give the name only, with no '.', '/', '\\' or spaces"),
			*Name, *Reason.ToString());
		return false;
	}
	if (IsReservedDeviceName(Name))
	{
		OutErrorMsg = FString::Printf(TEXT("Invalid asset name '%s': a Windows device name"), *Name);
		return false;
	}
	if (!FFileHelper::IsFilenameValidForSaving(Name, Reason))
	{
		OutErrorMsg = FString::Printf(TEXT("Invalid asset name '%s': %s"), *Name, *Reason.ToString());
		return false;
	}
	return true;
}

bool FN2CMcpContentBrowserUtils::DoesAssetPackageExist(const FString& PackageName)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPackageName(FName(*PackageName), Assets);
	return Assets.Num() > 0 || FPackageName::DoesPackageExist(PackageName);
}

bool FN2CMcpContentBrowserUtils::CreateFolderForMutation(const FString& Folder, FString& OutCreatedRoot, FString& OutErrorMsg)
{
	OutCreatedRoot.Reset();
	const FString Normalized = NormalizeContentPath(Folder);
	if (!ValidateContentPath(Normalized, OutErrorMsg) || !IsWritableContentPath(Normalized, OutErrorMsg))
	{
		return false;
	}
	if (DoesPathExist(Normalized))
	{
		return true;
	}

	// The highest missing folder: MakeDirectory creates the whole chain, so a failed call removes the whole chain
	FString Highest = Normalized;
	for (FString Parent = FPaths::GetPath(Normalized); !Parent.IsEmpty() && !IsContentMountRoot(Parent) && !DoesPathExist(Parent);
		Parent = FPaths::GetPath(Parent))
	{
		Highest = Parent;
	}
	if (!CreateContentFolder(Normalized, OutErrorMsg))
	{
		return false;
	}
	OutCreatedRoot = Highest;
	return true;
}

void FN2CMcpContentBrowserUtils::RemoveFolderCreatedForMutation(const FString& CreatedRoot)
{
	if (CreatedRoot.IsEmpty())
	{
		return;
	}
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	if (!EditorAssetSubsystem)
	{
		return;
	}
	// DeleteDirectory deletes the assets inside: never let it
	if (EditorAssetSubsystem->DoesDirectoryContainAssets(CreatedRoot, /*bRecursive*/ true))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Kept %s: a failed operation created it, but it holds assets now"), *CreatedRoot));
		return;
	}
	if (EditorAssetSubsystem->DeleteDirectory(CreatedRoot))
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Removed %s, created for an operation that failed"), *CreatedRoot), EN2CLogSeverity::Info);
	}
}

FString FN2CMcpContentBrowserUtils::SourceControlFilename(const FString& PackageName)
{
	// Providers take file names: a long package name passed as is counts as an absolute path, of a file that does not exist
	return USourceControlHelpers::PackageFilename(PackageName);
}

bool FN2CMcpContentBrowserUtils::IsCheckedOutByAnother(const FString& PackageName)
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	if (!SourceControlModule.IsEnabled() || !SourceControlModule.GetProvider().IsAvailable())
	{
		return false;
	}
	const FSourceControlStatePtr State = SourceControlModule.GetProvider().GetState(SourceControlFilename(PackageName), EStateCacheUsage::Use);
	return State.IsValid() && State->IsCheckedOutOther();
}

FN2CMcpEngineErrorCapture::FN2CMcpEngineErrorCapture()
{
	GLog->AddOutputDevice(this);
}

FN2CMcpEngineErrorCapture::~FN2CMcpEngineErrorCapture()
{
	GLog->RemoveOutputDevice(this);
}

void FN2CMcpEngineErrorCapture::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	if (Level == ELogVerbosity::Error || Level == ELogVerbosity::Fatal)
	{
		Errors.Add(Message);
	}
	else if (Level == ELogVerbosity::Warning)
	{
		Warnings.Add(Message);
	}
}

FString FN2CMcpEngineErrorCapture::GetReason()
{
	GLog->Flush();
	TArray<FString> Lines = Errors;
	Lines.Append(Warnings);
	if (Lines.Num() > 3)
	{
		Lines.SetNum(3);
	}
	return FString::Join(Lines, TEXT(" "));
}
