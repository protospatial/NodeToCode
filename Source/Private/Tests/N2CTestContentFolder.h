// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

/**
 * Saved scratch content for the specs that run the content tools for real (NodeToCode.MCP.Structure,
 * NodeToCode.MCP.Content): a /Game/__N2CTest... folder (or a test plugin's mount) that a case fills with saved assets,
 * folders and the redirectors a move leaves, and that is deleted before and after every case, even a failed one.
 */
namespace N2CTest
{
	/** The folder exists for the content browser (the asset registry knows the path, or it is on disk) */
	inline bool ContentFolderExists(const FString& Folder)
	{
		UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		return AssetSubsystem && AssetSubsystem->DoesDirectoryExist(Folder);
	}

	/** The folder is on disk */
	inline bool ContentFolderOnDisk(const FString& Folder)
	{
		FString Filename;
		return FPackageName::TryConvertLongPackageNameToFilename(Folder, Filename) && IFileManager::Get().DirectoryExists(*Filename);
	}

	/**
	 * Deletes every asset loaded from a package under Folder (Blueprints, the redirectors a move leaves, anything a
	 * tool made), the folder's files on disk and its paths in the asset registry. Folder itself goes too unless it is a
	 * mount root (bKeepRoot): then only what is inside it.
	 */
	inline void DeleteContentFolder(const FString& Folder, bool bKeepRoot = false)
	{
		const FString Prefix = Folder + TEXT("/");
		TArray<UObject*> Assets;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (!Package || !Package->GetName().StartsWith(Prefix))
			{
				continue;
			}
			Package->SetDirtyFlag(false);
			ForEachObjectWithPackage(Package, [&Assets](UObject* Object)
			{
				// A Blueprint's generated classes go with it: deleting both crashes in ForceDeleteObjects
				if (Object && !Object->IsA<UClass>() && (Object->IsAsset() || Object->IsA<UObjectRedirector>()))
				{
					Assets.Add(Object);
				}
				return true;
			}, /*bIncludeNestedObjects*/ false);
		}
		if (Assets.Num() > 0)
		{
			ObjectTools::ForceDeleteObjects(Assets, /*ShowConfirmation*/ false);
		}

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		TArray<FString> SubPaths;
		AssetRegistry.GetSubPaths(Folder, SubPaths, /*bInRecurse*/ true);
		if (bKeepRoot)
		{
			for (const FString& SubPath : SubPaths)
			{
				if (AssetSubsystem && AssetSubsystem->DoesDirectoryExist(SubPath))
				{
					AssetSubsystem->DeleteDirectory(SubPath);
				}
			}
		}
		else if (AssetSubsystem && AssetSubsystem->DoesDirectoryExist(Folder))
		{
			AssetSubsystem->DeleteDirectory(Folder);
		}

		// Whatever the subsystem left: the files, and the registry's paths (deepest first)
		FString Filename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Folder, Filename))
		{
			if (bKeepRoot)
			{
				TArray<FString> Children;
				IFileManager::Get().FindFiles(Children, *(Filename / TEXT("*")), /*Files*/ true, /*Directories*/ true);
				for (const FString& Child : Children)
				{
					const FString ChildPath = Filename / Child;
					if (IFileManager::Get().DirectoryExists(*ChildPath))
					{
						IFileManager::Get().DeleteDirectory(*ChildPath, /*RequireExists*/ false, /*Tree*/ true);
					}
					else
					{
						IFileManager::Get().Delete(*ChildPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
					}
				}
			}
			else
			{
				IFileManager::Get().DeleteDirectory(*Filename, /*RequireExists*/ false, /*Tree*/ true);
			}
		}
		SubPaths.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
		for (const FString& SubPath : SubPaths)
		{
			AssetRegistry.RemovePath(SubPath);
		}
		if (!bKeepRoot)
		{
			AssetRegistry.RemovePath(Folder);
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
