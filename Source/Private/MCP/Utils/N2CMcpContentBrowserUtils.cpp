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
#include "Dom/JsonObject.h"
#include "Core/N2CUserSettings.h"
#include "Interfaces/IPluginManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "UObject/ObjectRedirector.h"

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

bool FN2CMcpContentBrowserUtils::NavigateToPath(const FString& Path, bool bFocusContentBrowser)
{
	FContentBrowserModule& ContentBrowserModule = 
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	
	IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();
	
	// Create array of folders to sync to
	TArray<FString> FoldersToSync;
	FoldersToSync.Add(Path);
	
	// Sync the primary content browser to the folder
	ContentBrowser.SyncBrowserToFolders(FoldersToSync, /*bAllowLockedBrowsers*/ false, bFocusContentBrowser);
	
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

bool FN2CMcpContentBrowserUtils::IsExternalPackagePath(const FString& PackagePath)
{
	// /Game/Maps/__ExternalActors__/..., /Game/__ExternalObjects__/...: one package per actor or object of a World
	// Partition (one file per actor) level, hidden by the content browser
	return PackagePath.Contains(TEXT("/__External"), ESearchCase::IgnoreCase);
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
			: TEXT(". To allow it, turn on Allow MCP Writes Outside Project in Editor Preferences > Plugins > Node to Code (User) and restart the editor"));
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
	ItemJson->SetBoolField(TEXT("isFolder"), true);
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
	ItemJson->SetBoolField(TEXT("isFolder"), false);
	
	// Asset type from class name
	FString AssetClassName = AssetData.AssetClassPath.GetAssetName().ToString();
	ItemJson->SetStringField(TEXT("type"), AssetClassName);
	
	// Full class path
	ItemJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
	
	// Additional metadata if available
	if (AssetData.GetClass())
	{
		ItemJson->SetStringField(TEXT("nativeClass"), AssetData.GetClass()->GetName());
	}
	
	return ItemJson;
}

bool FN2CMcpContentBrowserUtils::EnumerateItemsAtPathAsJson(const FString& Path, bool bIncludeFolders, bool bIncludeFiles, TArray<TSharedPtr<FJsonObject>>& OutJsonItems)
{
	OutJsonItems.Reset();
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("EnumerateItemsAtPathAsJson: Path='%s', IncludeFolders=%d, IncludeFiles=%d"), 
		*Path, bIncludeFolders ? 1 : 0, bIncludeFiles ? 1 : 0), EN2CLogSeverity::Info);
	
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Folders: the asset registry's direct sub-paths (card 0285), as the content browser lists them. Paths that exist
	// only in the registry are included, empty folders too (MakeDirectory adds the path), and the World Partition /
	// OFPA folders the content browser hides (__ExternalActors__, __ExternalObjects__) are left out. The registry spells
	// each path as it holds it, whatever case the caller used.
	if (bIncludeFolders)
	{
		TArray<FString> SubPaths;
		AssetRegistry.GetSubPaths(Path, SubPaths, /*bInRecurse*/ false);
		SubPaths.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::IgnoreCase) < 0; });
		for (const FString& SubPath : SubPaths)
		{
			if (IsExternalPackagePath(SubPath))
			{
				continue;
			}
			OutJsonItems.Add(CreateFolderJson(SubPath, FPackageName::GetShortName(SubPath)));
		}
	}
	
	// Get assets if requested
	if (bIncludeFiles)
	{
		// Set up the filter
		FARFilter Filter;
		Filter.bRecursivePaths = false;
		Filter.PackagePaths.Add(FName(*Path));
		
		// Get assets, by name so pages are stable
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);
		AssetDataList.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		
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
		Item->TryGetBoolField(TEXT("isFolder"), bIsFolder);
		
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
		OutErrorMsg = FString::Printf(TEXT("Invalid asset name '%s' (%s). Give the name only, with no '.', '/', '\\' or spaces"),
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

bool FN2CMcpContentBrowserUtils::IsSameOrInsideFolder(const FString& Folder, const FString& Ancestor)
{
	const FString NormalizedFolder = NormalizeContentPath(Folder);
	const FString NormalizedAncestor = NormalizeContentPath(Ancestor);
	return NormalizedFolder.Equals(NormalizedAncestor, ESearchCase::IgnoreCase)
		|| NormalizedFolder.StartsWith(NormalizedAncestor + TEXT("/"), ESearchCase::IgnoreCase);
}

bool FN2CMcpContentBrowserUtils::IsRedirectorAt(const FString& ObjectPath)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData Asset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (Asset.IsValid())
	{
		return Asset.IsRedirector();
	}
	// A redirector the registry has not been told about yet
	return FindObject<UObjectRedirector>(nullptr, *ObjectPath) != nullptr;
}

bool FN2CMcpContentBrowserUtils::MoveFolder(const FString& Source, const FString& Destination, FN2CMcpFolderMoveResult& OutResult)
{
	OutResult = FN2CMcpFolderMoveResult();
	OutResult.Source = NormalizeContentPath(Source);
	OutResult.Destination = NormalizeContentPath(Destination);
	const FString From = OutResult.Source;
	const FString To = OutResult.Destination;

	// The engine does not check this: a folder moved into itself re-nests every asset under it (review fs-9)
	if (IsSameOrInsideFolder(To, From))
	{
		OutResult.Error = FString::Printf(TEXT("Cannot move %s into itself or one of its own subfolders (%s)"), *From, *To);
		return false;
	}
	if (DoesPathExist(To))
	{
		OutResult.Error = FString::Printf(TEXT("Destination folder already exists: %s"), *To);
		return false;
	}
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	if (!EditorAssetSubsystem)
	{
		OutResult.Error = TEXT("Failed to get EditorAssetSubsystem");
		return false;
	}

	// What is there before the move: the assets (a redirector is not an asset here) and every subfolder, empty or not
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> SourceAssets;
	AssetRegistry.GetAssetsByPath(FName(*From), SourceAssets, /*bRecursive*/ true);
	SourceAssets.RemoveAll([](const FAssetData& Asset) { return Asset.IsRedirector(); });
	TArray<FString> SubPaths;
	AssetRegistry.GetSubPaths(From, SubPaths, /*bInRecurse*/ true);

	// The destination's parent, only now, and removed again if nothing moves
	FString CreatedRoot;
	FString FolderError;
	if (!CreateFolderForMutation(FPaths::GetPath(To), CreatedRoot, FolderError))
	{
		OutResult.Error = FString::Printf(TEXT("Failed to create the destination's parent folder: %s"), *FolderError);
		return false;
	}

	// One engine call: it validates every destination, renames the assets in one batch under a slow task, and deletes
	// the source directory when nothing is left in it (GIsRunningUnattendedScript keeps dialogs away)
	bool bEngineOk = false;
	FString EngineReason;
	{
		FN2CMcpEngineErrorCapture Capture;
		bEngineOk = EditorAssetSubsystem->RenameDirectory(From, To);
		EngineReason = Capture.GetReason();
	}

	// Read the outcome back: each asset is at its new path (the engine's <To><subfolder>/<Name>.<Name>) or it is not
	for (const FAssetData& Asset : SourceAssets)
	{
		const FString OldPackage = Asset.PackageName.ToString();
		const FString AssetName = Asset.AssetName.ToString();
		const FString SubFolder = FPackageName::GetLongPackagePath(OldPackage).Mid(From.Len());
		const FString NewPackage = To + SubFolder + TEXT("/") + AssetName;
		const FAssetData Moved = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NewPackage + TEXT(".") + AssetName));
		if (Moved.IsValid() && !Moved.IsRedirector())
		{
			OutResult.MovedAssets.Add(Moved.PackageName.ToString());
		}
		else
		{
			OutResult.FailedAssets.Add(OldPackage);
		}
		if (IsRedirectorAt(Asset.GetObjectPathString()))
		{
			OutResult.Redirectors.Add(Asset.GetObjectPathString());
		}
	}

	if (bEngineOk)
	{
		// RenameDirectory moves assets, not folders: recreate the subfolders that held none (review fs-10)
		for (const FString& SubPath : SubPaths)
		{
			const FString NewSubPath = To + SubPath.Mid(From.Len());
			if (!DoesPathExist(NewSubPath) && !EditorAssetSubsystem->MakeDirectory(NewSubPath))
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Could not recreate the empty folder %s"), *NewSubPath));
			}
		}
		// The destination folder itself, also when the source held nothing
		if (!DoesPathExist(To))
		{
			EditorAssetSubsystem->MakeDirectory(To);
		}
	}
	else if (OutResult.MovedAssets.IsEmpty())
	{
		// Nothing moved: take back the folders this call made (the engine creates the destination before it validates)
		const FString Made = CreatedRoot.IsEmpty() ? To : CreatedRoot;
		if (DoesPathExist(Made))
		{
			RemoveFolderCreatedForMutation(Made);
		}
	}

	OutResult.bSourceRemoved = !DoesPathExist(From);
	if (!bEngineOk || OutResult.FailedAssets.Num() > 0)
	{
		OutResult.Error = EngineReason.IsEmpty()
			? FString(TEXT("the engine gave no reason (an asset may be in use or locked, or a Play-in-Editor session is running)"))
			: EngineReason;
		return false;
	}
	return true;
}

namespace N2CFolderMoveResult
{
	static TArray<TSharedPtr<FJsonValue>> ToJsonArray(const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& String : Strings)
		{
			Values.Add(MakeShared<FJsonValueString>(String));
		}
		return Values;
	}

	static FString JoinFew(const TArray<FString>& Strings)
	{
		constexpr int32 Shown = 5;
		TArray<FString> Few;
		for (int32 Index = 0; Index < FMath::Min(Strings.Num(), Shown); ++Index)
		{
			Few.Add(Strings[Index]);
		}
		FString Joined = FString::Join(Few, TEXT(", "));
		if (Strings.Num() > Shown)
		{
			Joined += FString::Printf(TEXT(" and %d more"), Strings.Num() - Shown);
		}
		return Joined;
	}
}

void FN2CMcpFolderMoveResult::WriteTo(FJsonObject& Result) const
{
	Result.SetArrayField(TEXT("movedAssets"), N2CFolderMoveResult::ToJsonArray(MovedAssets));
	Result.SetArrayField(TEXT("failedAssets"), N2CFolderMoveResult::ToJsonArray(FailedAssets));
	Result.SetArrayField(TEXT("redirectors"), N2CFolderMoveResult::ToJsonArray(Redirectors));
	Result.SetBoolField(TEXT("sourceRemoved"), bSourceRemoved);

	TArray<TSharedPtr<FJsonValue>> Tips;
	Tips.Add(MakeShared<FJsonValueString>(TEXT("Use 'read-content-browser-path' to explore the folder at its new path")));
	if (Redirectors.IsEmpty())
	{
		Tips.Add(MakeShared<FJsonValueString>(TEXT("No redirectors were left: references saved in the project's assets point at the new paths. ")
			TEXT("Paths written in C++, config or text files are not updated")));
	}
	else
	{
		Tips.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d redirector(s) were left at the old paths ('redirectors'): ")
			TEXT("a referencing package could not be updated or saved. Old references still resolve through them; to remove them, ")
			TEXT("right-click the old folder in the content browser and choose Update Redirector References. Paths written in C++, ")
			TEXT("config or text files are not updated"), Redirectors.Num())));
	}
	if (bSourceRemoved)
	{
		Tips.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("The original folder %s was removed"), *Source)));
	}
	else
	{
		Tips.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("The original folder %s is still there: it holds %s"), *Source,
			FailedAssets.Num() > 0 ? TEXT("the assets that did not move")
			: Redirectors.Num() > 0 ? TEXT("the redirectors left behind")
			: TEXT("files the asset registry does not list"))));
	}
	Result.SetArrayField(TEXT("tips"), Tips);
}

FString FN2CMcpFolderMoveResult::Describe() const
{
	FString Text = Error;
	if (MovedAssets.Num() > 0 || FailedAssets.Num() > 0)
	{
		Text += FString::Printf(TEXT(". Moved %d asset(s) to %s"), MovedAssets.Num(), *Destination);
		if (FailedAssets.Num() > 0)
		{
			Text += FString::Printf(TEXT("; %d did not move and are still under %s: %s"), FailedAssets.Num(), *Source,
				*N2CFolderMoveResult::JoinFew(FailedAssets));
		}
		if (MovedAssets.Num() > 0 && FailedAssets.Num() > 0)
		{
			Text += TEXT(". Move the rest with move-asset");
		}
	}
	if (Redirectors.Num() > 0)
	{
		Text += FString::Printf(TEXT(". Redirectors left at: %s"), *N2CFolderMoveResult::JoinFew(Redirectors));
	}
	return Text;
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
