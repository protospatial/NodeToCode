// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestContentFolder.h"
#include "Core/N2CUserSettings.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

/**
 * The content browser tools that move things (card 0163): move-asset and rename-asset with package paths and object
 * paths, a project plugin's content mount, empty folders, and move-folder into itself. Everything is saved for real
 * under /Game/__N2CTest (and the test plugin's /N2CTestContent mount), which is deleted before and after every case,
 * also when one fails. Name rules are NodeToCode.Names; the file jail is NodeToCode.PathPolicy; the content write
 * policy (/Game and project plugins only, card 0182) is "the write path policy" here. Cases that fail until their E12
 * card lands are KnownDefectIt (N2C_RUN_KNOWN_DEFECTS=1 runs them); folder moves are card 0281's. Review ids: imp-mcp-tools-contentbrowser-fs-1,
 * -9, mcp-tools-contentbrowser-fs-3, -5, -6, -7, -8, -9, -10, -12, -13, -21, -23, -28, mcp-tools-bp-structure-30.
 */
namespace N2CContentToolsSpec
{
	/** The test plugin: a content-only project plugin, explicitly loaded, so it can be mounted and unmounted in a case */
	const TCHAR* PluginName = TEXT("N2CTestContent");
	const TCHAR* PluginMount = TEXT("/N2CTestContent");

	FString PluginDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / PluginName);
	}

	FString PluginFile()
	{
		return PluginDir() / FString(PluginName) + TEXT(".uplugin");
	}

	/** Every asset package under Folder (recursive), sorted, as the asset registry lists them */
	TArray<FString> PackagesUnder(const FString& Folder)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*Folder), Assets, /*bRecursive*/ true);
		TArray<FString> Names;
		for (const FAssetData& Asset : Assets)
		{
			Names.AddUnique(Asset.PackageName.ToString() + TEXT(" : ") + Asset.AssetClassPath.GetAssetName().ToString());
		}
		Names.Sort();
		return Names;
	}

	/** The Blueprint loaded at this object path, without following a redirector, or null */
	UBlueprint* BlueprintAt(const FString& ObjectPath)
	{
		return FindObject<UBlueprint>(nullptr, *ObjectPath);
	}
}

BEGIN_DEFINE_SPEC(FN2CContentToolsSpec, "NodeToCode.MCP.Content", N2C_TEST_FLAGS)

	/** The scratch folder every case works in */
	const FString Folder = TEXT("/Game/__N2CTest");

	bool bPluginMounted = false;

	/** An xIt, or an It when N2C_RUN_KNOWN_DEFECTS=1 is in the editor's environment. The fixing card makes it an It. */
	void KnownDefectIt(const FString& CaseText, TFunction<void()> Body)
	{
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_RUN_KNOWN_DEFECTS")) == TEXT("1"))
		{
			It(CaseText, MoveTemp(Body));
		}
		else
		{
			xIt(CaseText, MoveTemp(Body));
		}
	}

	/** Runs a tool with showNotification off */
	FMcpToolCallResult Run(const TCHAR* Tool, TSharedPtr<FJsonObject> Arguments)
	{
		Arguments->SetBoolField(TEXT("showNotification"), false);
		return N2CTest::RunTool(Tool, Arguments);
	}

	/** Runs a tool and checks it succeeded; its JSON, or null */
	TSharedPtr<FJsonObject> ExpectOk(const FString& What, const TCHAR* Tool, const TSharedPtr<FJsonObject>& Arguments)
	{
		const FMcpToolCallResult Result = Run(Tool, Arguments);
		if (!TestFalse(What + TEXT(" succeeds (") + N2CTest::ResultText(Result).Left(400) + TEXT(")"), Result.bIsError))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		TestTrue(What + TEXT(": result JSON"), Json.IsValid());
		return Json;
	}

	struct FArg
	{
		const TCHAR* Key;
		FString Value;
	};

	/** A tool's arguments, all strings */
	static TSharedPtr<FJsonObject> Args(std::initializer_list<FArg> Fields)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		for (const FArg& Field : Fields)
		{
			Object->SetStringField(Field.Key, Field.Value);
		}
		return Object;
	}

	/** create-folder with createParents */
	bool MakeFolder(const FString& Path)
	{
		TSharedPtr<FJsonObject> Arguments = Args({ { TEXT("folderPath"), Path } });
		Arguments->SetBoolField(TEXT("createParents"), true);
		return ExpectOk(TEXT("create-folder ") + Path, TEXT("create-folder"), Arguments).IsValid()
			&& TestTrue(Path + TEXT(" exists"), N2CTest::ContentFolderExists(Path));
	}

	/** A saved Actor Blueprint Name in InFolder, made by create-blueprint-class; its object path, or empty */
	FString MakeAsset(const FString& Name, const FString& InFolder)
	{
		TSharedPtr<FJsonObject> Arguments = Args({ { TEXT("blueprintName"), Name }, { TEXT("parentClassPath"), TEXT("/Script/Engine.Actor") },
			{ TEXT("assetPath"), InFolder } });
		Arguments->SetBoolField(TEXT("addPrefix"), false);
		Arguments->SetBoolField(TEXT("openInEditor"), false);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunTool(TEXT("create-blueprint-class"), Arguments));
		const FString ObjectPath = InFolder / Name + TEXT(".") + Name;
		if (!TestTrue(TEXT("test asset ") + ObjectPath + TEXT(" created"), Json.IsValid() && BlueprintAtPath(ObjectPath) != nullptr))
		{
			return FString();
		}
		return ObjectPath;
	}

	static UBlueprint* BlueprintAtPath(const FString& ObjectPath)
	{
		return N2CContentToolsSpec::BlueprintAt(ObjectPath);
	}

	/** The Blueprint that was at From is at To (same object), and nothing at From is a Blueprint any more */
	void ExpectMoved(const FString& What, UBlueprint* Asset, const FString& From, const FString& To)
	{
		UBlueprint* AtTo = BlueprintAtPath(To);
		TestNotNull(What + TEXT(": a Blueprint at ") + To, AtTo);
		TestTrue(What + TEXT(": the same asset, moved"), AtTo == Asset);
		TestNull(What + TEXT(": no Blueprint left at ") + From, BlueprintAtPath(From));
		if (AtTo)
		{
			TestEqual(What + TEXT(": the asset's name"), AtTo->GetName(), FPackageName::ObjectPathToObjectName(To));
		}
	}

	/**
	 * A move-folder or rename-folder result reports the real outcome (card 0281): movedAssets are the new packages,
	 * nothing failed, redirectors are exactly the old object paths that hold a redirector now, and sourceRemoved says
	 * whether the old folder still exists
	 */
	void ExpectFolderMoveResult(const FJsonObject& Json, const TArray<FString>& OldObjectPaths, TArray<FString> NewPackages, const FString& OldFolder)
	{
		TArray<FString> Moved;
		TArray<FString> Failed;
		TArray<FString> Redirectors;
		Json.TryGetStringArrayField(TEXT("movedAssets"), Moved);
		Json.TryGetStringArrayField(TEXT("failedAssets"), Failed);
		Json.TryGetStringArrayField(TEXT("redirectors"), Redirectors);
		Moved.Sort();
		NewPackages.Sort();
		TestEqual(TEXT("movedAssets"), FString::Join(Moved, TEXT(", ")), FString::Join(NewPackages, TEXT(", ")));
		TestEqual(TEXT("failedAssets is empty"), Failed.Num(), 0);
		TArray<FString> ActualRedirectors;
		for (const FString& OldObjectPath : OldObjectPaths)
		{
			if (FN2CMcpContentBrowserUtils::IsRedirectorAt(OldObjectPath))
			{
				ActualRedirectors.Add(OldObjectPath);
			}
		}
		Redirectors.Sort();
		ActualRedirectors.Sort();
		TestEqual(TEXT("redirectors are the registry's"), FString::Join(Redirectors, TEXT(", ")), FString::Join(ActualRedirectors, TEXT(", ")));
		TestEqual(TEXT("sourceRemoved"), Json.GetBoolField(TEXT("sourceRemoved")), !N2CTest::ContentFolderExists(OldFolder));
		const TArray<TSharedPtr<FJsonValue>>* Tips = nullptr;
		FString TipText;
		if (Json.TryGetArrayField(TEXT("tips"), Tips))
		{
			for (const TSharedPtr<FJsonValue>& Tip : *Tips)
			{
				TipText += Tip->AsString() + TEXT("\n");
			}
		}
		TestEqual(TEXT("the tips say whether the source was removed"), TipText.Contains(TEXT(" was removed")), Json.GetBoolField(TEXT("sourceRemoved")));
	}

	/** Writes and mounts the test plugin; false (and an error) when the engine refuses */
	bool MountPlugin()
	{
		using namespace N2CContentToolsSpec;
		IFileManager::Get().MakeDirectory(*(PluginDir() / TEXT("Content")), /*Tree*/ true);
		const FString Descriptor = TEXT("{\n\t\"FileVersion\": 3,\n\t\"Version\": 1,\n\t\"VersionName\": \"1.0\",\n")
			TEXT("\t\"FriendlyName\": \"NodeToCode test content\",\n\t\"Description\": \"Made and deleted by NodeToCode.MCP.Content\",\n")
			TEXT("\t\"CanContainContent\": true,\n\t\"ExplicitlyLoaded\": true\n}\n");
		if (!TestTrue(TEXT("test plugin descriptor written"), FFileHelper::SaveStringToFile(Descriptor, *PluginFile())))
		{
			return false;
		}
		IPluginManager& Plugins = IPluginManager::Get();
		FText Reason;
		if (!TestTrue(TEXT("test plugin added (") + Reason.ToString() + TEXT(")"), Plugins.AddToPluginsList(PluginFile(), &Reason))
			|| !TestTrue(TEXT("test plugin mounted"), Plugins.MountExplicitlyLoadedPlugin(PluginName)))
		{
			return false;
		}
		bPluginMounted = true;
		const TSharedPtr<IPlugin> Plugin = Plugins.FindPlugin(PluginName);
		TestTrue(TEXT("the test plugin is a project plugin with content"), Plugin.IsValid() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project
			&& Plugin->CanContainContent());
		return TestTrue(TEXT("its content is mounted at /N2CTestContent"), FPackageName::MountPointExists(FString(PluginMount) + TEXT("/")));
	}

	/** Deletes the test plugin's content, unmounts it, forgets it and deletes its files */
	void UnmountPlugin()
	{
		using namespace N2CContentToolsSpec;
		IPluginManager& Plugins = IPluginManager::Get();
		if (Plugins.FindPlugin(PluginName).IsValid())
		{
			if (FPackageName::MountPointExists(FString(PluginMount) + TEXT("/")))
			{
				N2CTest::DeleteContentFolder(PluginMount, /*bKeepRoot*/ true);
			}
			FText Reason;
			if (!Plugins.UnmountExplicitlyLoadedPlugin(PluginName, &Reason))
			{
				AddError(TEXT("test plugin unmount: ") + Reason.ToString());
			}
			Plugins.RemoveFromPluginsList(PluginFile());
		}
		bPluginMounted = false;
		IFileManager::Get().DeleteDirectory(*PluginDir(), /*RequireExists*/ false, /*Tree*/ true);
	}

END_DEFINE_SPEC(FN2CContentToolsSpec)

void FN2CContentToolsSpec::Define()
{
	using namespace N2CContentToolsSpec;

	BeforeEach([this]()
	{
		N2CTest::DeleteContentFolder(Folder);
		TestTrue(TEXT("scratch folder made"), MakeFolder(Folder));
	});

	AfterEach([this]()
	{
		N2CTest::DeleteContentFolder(Folder);
		TestFalse(TEXT("scratch folder deleted"), N2CTest::ContentFolderOnDisk(Folder));
	});

	Describe(TEXT("move-asset"), [this]()
	{
		It(TEXT("moves an asset by package path into another folder, keeping its name"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CMoveByPackage"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("move"), TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CMoveByPackage") }, { TEXT("destinationPath"), Folder / TEXT("Moved") } }));
			if (Json.IsValid())
			{
				ExpectMoved(TEXT("move"), Asset, From, Folder / TEXT("Moved/BP_N2CMoveByPackage.BP_N2CMoveByPackage"));
				TestEqual(TEXT("newPath"), Json->GetStringField(TEXT("newPath")), Folder / TEXT("Moved/BP_N2CMoveByPackage"));
				TestEqual(TEXT("operation"), Json->GetStringField(TEXT("operation")), FString(TEXT("move")));
				TestEqual(TEXT("leftRedirector is what the registry holds at the old path"), Json->GetBoolField(TEXT("leftRedirector")),
					FN2CMcpContentBrowserUtils::IsRedirectorAt(From));
			}
		});

		It(TEXT("moves and renames an asset by package path with newName"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CMoveRename"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			TSharedPtr<FJsonObject> Arguments = Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CMoveRename") },
				{ TEXT("destinationPath"), Folder / TEXT("Moved") }, { TEXT("newName"), TEXT("BP_N2CMoveRenamed") } });
			if (ExpectOk(TEXT("move and rename"), TEXT("move-asset"), Arguments).IsValid())
			{
				ExpectMoved(TEXT("move and rename"), Asset, From, Folder / TEXT("Moved/BP_N2CMoveRenamed.BP_N2CMoveRenamed"));
			}
		});

		// mcp-tools-contentbrowser-fs-3 (card 0284): with an object path and no newName the default name was
		// GetCleanFilename ("A.A"), so the destination became /Game/Dest/A.A.A.A and the move failed. The name now comes
		// from the registry's asset.
		It(TEXT("moves an asset by object path into another folder, keeping its name"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CMoveByObject"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("move by object path"), TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Folder / TEXT("Moved") } }));
			if (Json.IsValid())
			{
				ExpectMoved(TEXT("move by object path"), Asset, From, Folder / TEXT("Moved/BP_N2CMoveByObject.BP_N2CMoveByObject"));
				TestEqual(TEXT("oldPath"), Json->GetStringField(TEXT("oldPath")), Folder / TEXT("BP_N2CMoveByObject"));
				TestEqual(TEXT("newPath, read back"), Json->GetStringField(TEXT("newPath")), Folder / TEXT("Moved/BP_N2CMoveByObject"));
				TestEqual(TEXT("objectPath, read back"), Json->GetStringField(TEXT("objectPath")), Folder / TEXT("Moved/BP_N2CMoveByObject.BP_N2CMoveByObject"));
			}
		});

		It(TEXT("moves and renames an asset by object path with newName"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CMoveObjectRename"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			TSharedPtr<FJsonObject> Arguments = Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Folder / TEXT("Moved") },
				{ TEXT("newName"), TEXT("BP_N2CMoveObjectRenamed") } });
			if (ExpectOk(TEXT("move and rename by object path"), TEXT("move-asset"), Arguments).IsValid())
			{
				ExpectMoved(TEXT("move and rename by object path"), Asset, From, Folder / TEXT("Moved/BP_N2CMoveObjectRenamed.BP_N2CMoveObjectRenamed"));
			}
		});
	});

	Describe(TEXT("rename-asset"), [this]()
	{
		It(TEXT("renames an asset by package path and by object path with newName"), [this]()
		{
			const FString First = MakeAsset(TEXT("BP_N2CRenamePackage"), Folder);
			const FString Second = MakeAsset(TEXT("BP_N2CRenameObject"), Folder);
			UBlueprint* FirstAsset = BlueprintAtPath(First);
			UBlueprint* SecondAsset = BlueprintAtPath(Second);
			if (!FirstAsset || !SecondAsset)
			{
				return;
			}
			const TSharedPtr<FJsonObject> ByPackage = ExpectOk(TEXT("rename by package path"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CRenamePackage") }, { TEXT("newName"), TEXT("BP_N2CRenamedPackage") } }));
			if (ByPackage.IsValid())
			{
				ExpectMoved(TEXT("rename by package path"), FirstAsset, First, Folder / TEXT("BP_N2CRenamedPackage.BP_N2CRenamedPackage"));
				TestFalse(TEXT("wasMoved"), ByPackage->GetBoolField(TEXT("wasMoved")));
			}
			if (ExpectOk(TEXT("rename by object path"), TEXT("rename-asset"), Args({ { TEXT("sourcePath"), Second }, { TEXT("newName"), TEXT("BP_N2CRenamedObject") } })).IsValid())
			{
				ExpectMoved(TEXT("rename by object path"), SecondAsset, Second, Folder / TEXT("BP_N2CRenamedObject.BP_N2CRenamedObject"));
			}
		});

		It(TEXT("moves and renames an asset by package path and by object path with destinationPath"), [this]()
		{
			const FString First = MakeAsset(TEXT("BP_N2CRelocatePackage"), Folder);
			const FString Second = MakeAsset(TEXT("BP_N2CRelocateObject"), Folder);
			UBlueprint* FirstAsset = BlueprintAtPath(First);
			UBlueprint* SecondAsset = BlueprintAtPath(Second);
			if (!FirstAsset || !SecondAsset)
			{
				return;
			}
			const TSharedPtr<FJsonObject> ByPackage = ExpectOk(TEXT("relocate by package path"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CRelocatePackage") }, { TEXT("destinationPath"), Folder / TEXT("Sub/BP_N2CRelocatedPackage") } }));
			if (ByPackage.IsValid())
			{
				ExpectMoved(TEXT("relocate by package path"), FirstAsset, First, Folder / TEXT("Sub/BP_N2CRelocatedPackage.BP_N2CRelocatedPackage"));
				TestTrue(TEXT("wasMoved"), ByPackage->GetBoolField(TEXT("wasMoved")));
			}
			if (ExpectOk(TEXT("relocate by object path"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), Second }, { TEXT("destinationPath"), Folder / TEXT("Sub/BP_N2CRelocatedObject") } })).IsValid())
			{
				ExpectMoved(TEXT("relocate by object path"), SecondAsset, Second, Folder / TEXT("Sub/BP_N2CRelocatedObject.BP_N2CRelocatedObject"));
			}
		});
	});

	// Card 0284: names checked as given, nothing created before the checks, nothing left behind by a failed call, case-only
	// renames refused, results read back from the registry. Review ids mcp-tools-contentbrowser-fs-3, -5, -8, -12, -13,
	// -21, -23, -28.
	Describe(TEXT("asset names and paths"), [this]()
	{
		// mcp-tools-contentbrowser-fs-12: the dot check ran on GetBaseFilename, which had already dropped ".Final", and
		// the engine renamed the asset to "Final" while the result reported "BP_New"
		It(TEXT("rename-asset refuses newName 'BP_New.Final' and other names that are not a plain name, changing nothing"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CDotted"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			const TArray<FString> Before = PackagesUnder(Folder);
			for (const TCHAR* Name : { TEXT("BP_New.Final"), TEXT("BP_New.uasset"), TEXT("Sub/BP_New"), TEXT("Sub\\BP_New"), TEXT("BP New"), TEXT("BP:New") })
			{
				const FMcpToolCallResult Result = Run(TEXT("rename-asset"), Args({ { TEXT("sourcePath"), From }, { TEXT("newName"), Name } }));
				TestTrue(FString::Printf(TEXT("newName '%s' refused (%s)"), Name, *N2CTest::ResultText(Result).Left(200)), Result.bIsError);
			}
			const FMcpToolCallResult Moved = Run(TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Folder / TEXT("NotMade") }, { TEXT("newName"), TEXT("BP_New.Final") } }));
			TestTrue(TEXT("move-asset newName 'BP_New.Final' refused (") + N2CTest::ResultText(Moved).Left(200) + TEXT(")"), Moved.bIsError);
			TestTrue(TEXT("the asset is in place"), BlueprintAtPath(From) == Asset);
			TestNull(TEXT("no asset named Final"), BlueprintAtPath(Folder / TEXT("Final.Final")));
			TestEqual(TEXT("nothing renamed"), FString::Join(PackagesUnder(Folder), TEXT("\n")), FString::Join(Before, TEXT("\n")));
			TestFalse(TEXT("move-asset created no destination folder"), N2CTest::ContentFolderExists(Folder / TEXT("NotMade")));
		});

		// mcp-tools-contentbrowser-fs-12, -21: the result reports the asset the registry holds, and assetSelected says
		// whether the content browser selected it
		It(TEXT("rename-asset reports the renamed asset read back from the registry"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CReadBack"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("rename to an object-form destinationPath"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Folder / TEXT("Deep/Er/BP_N2CReadBackNew.BP_N2CReadBackNew") } }));
			if (Json.IsValid())
			{
				ExpectMoved(TEXT("rename"), Asset, From, Folder / TEXT("Deep/Er/BP_N2CReadBackNew.BP_N2CReadBackNew"));
				TestEqual(TEXT("oldPath"), Json->GetStringField(TEXT("oldPath")), Folder / TEXT("BP_N2CReadBack"));
				TestEqual(TEXT("newPath"), Json->GetStringField(TEXT("newPath")), Folder / TEXT("Deep/Er/BP_N2CReadBackNew"));
				TestEqual(TEXT("newName"), Json->GetStringField(TEXT("newName")), FString(TEXT("BP_N2CReadBackNew")));
				TestTrue(TEXT("wasMoved"), Json->GetBoolField(TEXT("wasMoved")));
				TestTrue(TEXT("assetSelected"), Json->GetBoolField(TEXT("assetSelected")));
			}
			// An object-form destination whose object name is another name is not a plain asset name
			const FMcpToolCallResult Mismatched = Run(TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Deep/Er/BP_N2CReadBackNew") }, { TEXT("destinationPath"), Folder / TEXT("BP_A.BP_B") } }));
			TestTrue(TEXT("destinationPath BP_A.BP_B refused (") + N2CTest::ResultText(Mismatched).Left(200) + TEXT(")"), Mismatched.bIsError);
		});

		// mcp-tools-contentbrowser-fs-21: a package path was looked up as an object path, which finds no asset
		It(TEXT("SelectAssetAtPath selects by package path and by object path, and says when there is nothing to select"), [this]()
		{
			const FString Asset = MakeAsset(TEXT("BP_N2CSelect"), Folder);
			if (Asset.IsEmpty())
			{
				return;
			}
			TestTrue(TEXT("by package path"), FN2CMcpContentBrowserUtils::SelectAssetAtPath(Folder / TEXT("BP_N2CSelect")));
			TestTrue(TEXT("by object path"), FN2CMcpContentBrowserUtils::SelectAssetAtPath(Asset));
			TestFalse(TEXT("a missing asset"), FN2CMcpContentBrowserUtils::SelectAssetAtPath(Folder / TEXT("BP_N2CNoSuchAsset")));
		});

		// mcp-tools-contentbrowser-fs-28: package and folder names are case-insensitive, so a case-only rename met the
		// asset or folder itself and failed with a misleading "already exists"
		It(TEXT("case-only renames of an asset or a folder are refused with an explicit error"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2Ccase"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset || !MakeFolder(Folder / TEXT("lower")))
			{
				return;
			}
			const FMcpToolCallResult Renamed = Run(TEXT("rename-asset"), Args({ { TEXT("sourcePath"), From }, { TEXT("newName"), TEXT("BP_N2CCase") } }));
			TestTrue(TEXT("rename-asset refuses it (") + N2CTest::ResultText(Renamed).Left(300) + TEXT(")"),
				Renamed.bIsError && N2CTest::ResultText(Renamed).Contains(TEXT("Case-only")));
			const FMcpToolCallResult Moved = Run(TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Folder }, { TEXT("newName"), TEXT("BP_N2CCASE") } }));
			TestTrue(TEXT("move-asset refuses it (") + N2CTest::ResultText(Moved).Left(300) + TEXT(")"),
				Moved.bIsError && N2CTest::ResultText(Moved).Contains(TEXT("Case-only")));
			const FMcpToolCallResult Folded = Run(TEXT("rename-folder"), Args({ { TEXT("sourcePath"), Folder / TEXT("lower") }, { TEXT("newName"), TEXT("Lower") } }));
			TestTrue(TEXT("rename-folder refuses it (") + N2CTest::ResultText(Folded).Left(300) + TEXT(")"),
				Folded.bIsError && N2CTest::ResultText(Folded).Contains(TEXT("Case-only")));
			TestTrue(TEXT("the asset is in place"), BlueprintAtPath(From) == Asset);
		});

		// mcp-tools-contentbrowser-fs-8, -23: overwriteExisting never overwrote (and is gone); the destination folder was
		// created before the name was checked and stayed behind when the copy failed
		It(TEXT("a failed copy leaves no new folder and gives the real reason"), [this]()
		{
			const FString Source = MakeAsset(TEXT("BP_N2CCopySource"), Folder);
			const FString Existing = MakeAsset(TEXT("BP_N2CCopyExisting"), Folder);
			UBlueprint* ExistingAsset = BlueprintAtPath(Existing);
			if (Source.IsEmpty() || !ExistingAsset)
			{
				return;
			}
			const FMcpToolCallResult Dotted = Run(TEXT("copy-asset"),
				Args({ { TEXT("sourcePath"), Source }, { TEXT("destinationPath"), Folder / TEXT("Fresh/Deeper/BP_Copy.Final") } }));
			TestTrue(TEXT("a dotted name is refused (") + N2CTest::ResultText(Dotted).Left(200) + TEXT(")"), Dotted.bIsError);
			TestFalse(TEXT("no Fresh folder"), N2CTest::ContentFolderExists(Folder / TEXT("Fresh")));
			TestFalse(TEXT("no Fresh folder on disk"), N2CTest::ContentFolderOnDisk(Folder / TEXT("Fresh")));

			TSharedPtr<FJsonObject> Overwrite = Args({ { TEXT("sourcePath"), Source }, { TEXT("destinationPath"), Folder / TEXT("BP_N2CCopyExisting") } });
			Overwrite->SetBoolField(TEXT("overwriteExisting"), true);
			const FMcpToolCallResult Exists = Run(TEXT("copy-asset"), Overwrite);
			const FString ExistsText = N2CTest::ResultText(Exists);
			TestTrue(TEXT("an existing destination is refused, saying so (") + ExistsText.Left(300) + TEXT(")"),
				Exists.bIsError && ExistsText.Contains(TEXT("already exists")) && !ExistsText.Contains(TEXT("permission")));
			TestTrue(TEXT("the existing asset is untouched"), BlueprintAtPath(Existing) == ExistingAsset);

			// And a copy that works creates its folder and reports the copy as the registry holds it
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("copy into a new folder"), TEXT("copy-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CCopySource") }, { TEXT("destinationPath"), Folder / TEXT("Made/BP_N2CCopied") } }));
			if (Json.IsValid())
			{
				TestNotNull(TEXT("the copy"), BlueprintAtPath(Folder / TEXT("Made/BP_N2CCopied.BP_N2CCopied")));
				TestEqual(TEXT("destinationPath"), Json->GetStringField(TEXT("destinationPath")), Folder / TEXT("Made/BP_N2CCopied"));
				TestEqual(TEXT("assetName"), Json->GetStringField(TEXT("assetName")), FString(TEXT("BP_N2CCopied")));
			}
		});

		// mcp-tools-contentbrowser-fs-23: CreateFolderForMutation reports the highest folder it made, and
		// RemoveFolderCreatedForMutation takes the chain away again unless an asset is in it
		It(TEXT("a folder made for a failed mutation is removed, with its parents, and never one holding an asset"), [this]()
		{
			FString CreatedRoot;
			FString Error;
			if (!TestTrue(TEXT("made (") + Error + TEXT(")"), FN2CMcpContentBrowserUtils::CreateFolderForMutation(Folder / TEXT("A/B/C"), CreatedRoot, Error)))
			{
				return;
			}
			TestEqual(TEXT("the highest folder made"), CreatedRoot, Folder / TEXT("A"));
			TestTrue(TEXT("A/B/C exists"), N2CTest::ContentFolderExists(Folder / TEXT("A/B/C")));
			FN2CMcpContentBrowserUtils::RemoveFolderCreatedForMutation(CreatedRoot);
			TestFalse(TEXT("A is gone"), N2CTest::ContentFolderOnDisk(Folder / TEXT("A")));
			TestTrue(TEXT("the scratch folder stays"), N2CTest::ContentFolderExists(Folder));

			FString Existing;
			TestTrue(TEXT("an existing folder"), FN2CMcpContentBrowserUtils::CreateFolderForMutation(Folder, Existing, Error));
			TestTrue(TEXT("nothing made"), Existing.IsEmpty());

			FString Kept;
			if (FN2CMcpContentBrowserUtils::CreateFolderForMutation(Folder / TEXT("K/L"), Kept, Error) && !MakeAsset(TEXT("BP_N2CKeepMe"), Folder / TEXT("K/L")).IsEmpty())
			{
				FN2CMcpContentBrowserUtils::RemoveFolderCreatedForMutation(Kept);
				TestNotNull(TEXT("the asset in it is kept"), BlueprintAtPath(Folder / TEXT("K/L/BP_N2CKeepMe.BP_N2CKeepMe")));
			}
		});

		// mcp-tools-contentbrowser-fs-13: the source-control check passed /Game/... as a file name
		It(TEXT("the source-control check asks about the package's file"), [this]()
		{
			const FString File = FN2CMcpContentBrowserUtils::SourceControlFilename(Folder / TEXT("BP_N2CAny"));
			TestTrue(TEXT("a .uasset file: ") + File, File.EndsWith(TEXT("/BP_N2CAny.uasset")));
			TestFalse(TEXT("an absolute file path, not the package name: ") + File, FPaths::IsRelative(File) || File.StartsWith(TEXT("/Game")));
			TestTrue(TEXT("under the project's Content folder: ") + File,
				File.StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), ESearchCase::IgnoreCase));
		});
	});

	Describe(TEXT("a project plugin's content mount"), [this]()
	{
		BeforeEach([this]()
		{
			UnmountPlugin();
			MountPlugin();
		});

		AfterEach([this]()
		{
			UnmountPlugin();
			TestFalse(TEXT("test plugin files deleted"), IFileManager::Get().DirectoryExists(*N2CContentToolsSpec::PluginDir()));
			TestFalse(TEXT("test plugin unmounted"), FPackageName::MountPointExists(FString(N2CContentToolsSpec::PluginMount) + TEXT("/")));
		});

		It(TEXT("move-asset moves an asset into the plugin's content and back"), [this]()
		{
			if (!bPluginMounted)
			{
				return;
			}
			const FString From = MakeAsset(TEXT("BP_N2CToPlugin"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			const FString InPlugin = FString(N2CContentToolsSpec::PluginMount) / TEXT("Props/BP_N2CToPlugin.BP_N2CToPlugin");
			if (!ExpectOk(TEXT("into the plugin"), TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CToPlugin") }, { TEXT("destinationPath"), FString(N2CContentToolsSpec::PluginMount) / TEXT("Props") } })).IsValid())
			{
				return;
			}
			ExpectMoved(TEXT("into the plugin"), Asset, From, InPlugin);
			if (ExpectOk(TEXT("back to /Game"), TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), FString(N2CContentToolsSpec::PluginMount) / TEXT("Props/BP_N2CToPlugin") }, { TEXT("destinationPath"), Folder / TEXT("Back") } })).IsValid())
			{
				ExpectMoved(TEXT("back to /Game"), Asset, InPlugin, Folder / TEXT("Back/BP_N2CToPlugin.BP_N2CToPlugin"));
			}
		});

		// mcp-tools-contentbrowser-fs-7 (card 0182): these tools accepted only /Game/ or a path containing "/Plugins/",
		// and a plugin's content mounts at /<PluginName>/, so every plugin was refused. The write policy
		// (FN2CMcpContentBrowserUtils::IsWritableContentPath) allows a project plugin's content.
		It(TEXT("create-folder, rename-asset, move-folder and rename-folder accept the plugin's content"), [this]()
		{
			if (!bPluginMounted)
			{
				return;
			}
			const FString Mount = N2CContentToolsSpec::PluginMount;
			TSharedPtr<FJsonObject> CreateArgs = Args({ { TEXT("folderPath"), Mount / TEXT("Made") } });
			if (ExpectOk(TEXT("create-folder in the plugin"), TEXT("create-folder"), CreateArgs).IsValid())
			{
				TestTrue(TEXT("plugin folder made"), N2CTest::ContentFolderExists(Mount / TEXT("Made")));
			}
			const FString From = MakeAsset(TEXT("BP_N2CRenameToPlugin"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (Asset && ExpectOk(TEXT("rename-asset into the plugin"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Mount / TEXT("Made/BP_N2CRenamedInPlugin") } })).IsValid())
			{
				ExpectMoved(TEXT("rename-asset into the plugin"), Asset, From, Mount / TEXT("Made/BP_N2CRenamedInPlugin.BP_N2CRenamedInPlugin"));
			}
			const FString Boxed = MakeAsset(TEXT("BP_N2CBoxed"), Folder / TEXT("Box"));
			UBlueprint* BoxedAsset = BlueprintAtPath(Boxed);
			if (BoxedAsset && ExpectOk(TEXT("move-folder into the plugin"), TEXT("move-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Box") }, { TEXT("destinationPath"), Mount / TEXT("Made") } })).IsValid())
			{
				ExpectMoved(TEXT("move-folder into the plugin"), BoxedAsset, Boxed, Mount / TEXT("Made/Box/BP_N2CBoxed.BP_N2CBoxed"));
			}
			if (ExpectOk(TEXT("rename-folder in the plugin"), TEXT("rename-folder"),
				Args({ { TEXT("sourcePath"), Mount / TEXT("Made") }, { TEXT("newName"), TEXT("Renamed") } })).IsValid())
			{
				TestTrue(TEXT("renamed plugin folder"), N2CTest::ContentFolderExists(Mount / TEXT("Renamed")));
			}
		});

		It(TEXT("create-blueprint-class writes in the plugin's content, and the plugin's root cannot be moved or renamed"), [this]()
		{
			if (!bPluginMounted)
			{
				return;
			}
			const FString Mount = N2CContentToolsSpec::PluginMount;
			FString Error;
			TestTrue(TEXT("the policy allows the project plugin (") + Error + TEXT(")"),
				FN2CMcpContentBrowserUtils::IsWritableContentPath(Mount / TEXT("Props/BP_A.BP_A"), /*bAllowOutsideProject*/ false, Error));
			// MakeAsset runs create-blueprint-class and checks the saved Blueprint is there
			MakeAsset(TEXT("BP_N2CMadeInPlugin"), Mount / TEXT("Props"));
			TestTrue(TEXT("the plugin's mount is a content root"), FN2CMcpContentBrowserUtils::IsContentMountRoot(Mount));
			const FMcpToolCallResult Renamed = Run(TEXT("rename-folder"), Args({ { TEXT("sourcePath"), Mount }, { TEXT("newName"), TEXT("Elsewhere") } }));
			TestTrue(TEXT("rename-folder of the plugin's root refused (") + N2CTest::ResultText(Renamed).Left(200) + TEXT(")"), Renamed.bIsError);
			const FMcpToolCallResult Moved = Run(TEXT("move-folder"), Args({ { TEXT("sourcePath"), Mount }, { TEXT("destinationPath"), Folder } }));
			TestTrue(TEXT("move-folder of the plugin's root refused (") + N2CTest::ResultText(Moved).Left(200) + TEXT(")"), Moved.bIsError);
			TestTrue(TEXT("the plugin is still mounted"), FPackageName::MountPointExists(Mount + TEXT("/")));
		});
	});

	Describe(TEXT("the write path policy"), [this]()
	{
		It(TEXT("allows /Game and refuses /Engine, an engine plugin, /NodeToCode, read-only and unmounted roots"), [this]()
		{
			auto Allowed = [this](const FString& Path, bool bOutside)
			{
				FString Error;
				const bool bAllowed = FN2CMcpContentBrowserUtils::IsWritableContentPath(Path, bOutside, Error);
				TestTrue(FString::Printf(TEXT("refused%s with a reason: '%s'"), bOutside ? TEXT(" (outside on)") : TEXT(""), *Path), bAllowed || !Error.IsEmpty());
				return bAllowed;
			};
			for (const TCHAR* Path : { TEXT("/Game"), TEXT("/Game/"), TEXT("/Game/__N2CTest"), TEXT("/Game/__N2CTest/BP_A"),
				TEXT("/Game/__N2CTest/BP_A.BP_A"), TEXT("\\Game\\__N2CTest"), TEXT("/game/__N2CTest") })
			{
				TestTrue(FString::Printf(TEXT("allowed: '%s'"), Path), Allowed(Path, false));
			}
			for (const TCHAR* Path : { TEXT("/Engine"), TEXT("/Engine/N2CTest"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("/engine/N2CTest"),
				TEXT("/NodeToCode"), TEXT("/NodeToCode/N2CTest"), TEXT("/Game/../Engine/N2CTest"), TEXT("/Game//N2CTest"),
				TEXT("/Script/Engine"), TEXT("/Temp/N2CTest"), TEXT("/Memory/N2CTest"), TEXT("/N2CNotMounted/N2CTest"), TEXT("") })
			{
				TestFalse(FString::Printf(TEXT("refused: '%s'"), Path), Allowed(Path, false));
			}
			FString Error;
			FN2CMcpContentBrowserUtils::IsWritableContentPath(TEXT("/Engine/N2CTest"), false, Error);
			TestTrue(TEXT("the refusal names the setting: ") + Error, Error.Contains(TEXT("Allow MCP Writes Outside Project")));

			// With the user's opt-in: any mounted root the engine saves to, still never /Script, /Temp, /Memory or an unmounted one
			for (const TCHAR* Path : { TEXT("/Game/__N2CTest"), TEXT("/Engine/N2CTest"), TEXT("/NodeToCode/N2CTest") })
			{
				TestTrue(FString::Printf(TEXT("allowed with the setting: '%s'"), Path), Allowed(Path, true));
			}
			for (const TCHAR* Path : { TEXT("/Script/Engine"), TEXT("/Temp/N2CTest"), TEXT("/Memory/N2CTest"), TEXT("/N2CNotMounted/N2CTest"),
				TEXT("/Engine/../Game") })
			{
				TestFalse(FString::Printf(TEXT("refused with the setting: '%s'"), Path), Allowed(Path, true));
			}

			// An engine plugin's content (any enabled one that has a mount)
			for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
			{
				if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Engine && FPackageName::MountPointExists(Plugin->GetMountedAssetPath()))
				{
					const FString InPlugin = Plugin->GetMountedAssetPath() + TEXT("N2CTest");
					TestFalse(TEXT("refused: an engine plugin's ") + InPlugin, Allowed(InPlugin, false));
					TestTrue(TEXT("allowed with the setting: an engine plugin's ") + InPlugin, Allowed(InPlugin, true));
					break;
				}
			}

			TestTrue(TEXT("/Game is a content root"), FN2CMcpContentBrowserUtils::IsContentMountRoot(TEXT("/Game")));
			TestFalse(TEXT("/Game/__N2CTest is not a content root"), FN2CMcpContentBrowserUtils::IsContentMountRoot(TEXT("/Game/__N2CTest")));
		});

		It(TEXT("is off by default, and Project Settings show the value in effect"), [this]()
		{
			TestFalse(TEXT("the code default"), UN2CUserSettings::bDefaultAllowMcpWritesOutsideProject);
			TestEqual(TEXT("Project Settings show the value in effect"),
				GetDefault<UN2CUserSettings>()->bAllowMcpWritesOutsideProject, UN2CUserSettings::AreMcpWritesOutsideProjectAllowed());
			TestFalse(TEXT("off in the test host (the refusal cases need it off)"), UN2CUserSettings::AreMcpWritesOutsideProjectAllowed());
			// What Project Settings > Plugins > Node to Code (User) shows
			const FProperty* Property = UN2CUserSettings::StaticClass()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bAllowMcpWritesOutsideProject));
			if (TestNotNull(TEXT("the property"), Property))
			{
				TestTrue(TEXT("a config property"), Property->HasAnyPropertyFlags(CPF_Config));
				TestEqual(TEXT("its display name"), Property->GetMetaData(TEXT("DisplayName")), FString(TEXT("Allow MCP Writes Outside Project")));
				TestEqual(TEXT("its category"), Property->GetMetaData(TEXT("Category")), FString(TEXT("MCP Server")));
				TestTrue(TEXT("it needs a restart"), Property->HasMetaData(TEXT("ConfigRestartRequired")));
			}
		});

		It(TEXT("every writing tool refuses /Engine and /NodeToCode and creates nothing there"), [this]()
		{
			if (!TestFalse(TEXT("Allow MCP Writes Outside Project is off"), UN2CUserSettings::AreMcpWritesOutsideProjectAllowed()))
			{
				return;
			}
			const FString Asset = MakeAsset(TEXT("BP_N2CStaysInGame"), Folder);
			UBlueprint* Blueprint = BlueprintAtPath(Asset);
			if (!Blueprint || !MakeFolder(Folder / TEXT("Box")))
			{
				return;
			}
			const FString CubePackage = TEXT("/Engine/BasicShapes/Cube");
			TestTrue(TEXT("the engine's Cube exists"), FPackageName::DoesPackageExist(CubePackage));
			const FString StandardMacrosFile = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir() / TEXT("EditorBlueprintResources/StandardMacros.uasset"));
			const FDateTime MacrosStamp = IFileManager::Get().GetTimeStamp(*StandardMacrosFile);

			auto ExpectRefused = [this](const FString& What, const TCHAR* Tool, TSharedPtr<FJsonObject> Arguments)
			{
				const FMcpToolCallResult Result = Run(Tool, Arguments);
				const FString Text = N2CTest::ResultText(Result);
				TestTrue(What + TEXT(" is refused (") + Text.Left(300) + TEXT(")"), Result.bIsError);
				return Text;
			};

			TSharedPtr<FJsonObject> CreateArgs = Args({ { TEXT("blueprintName"), TEXT("BP_N2CEngine") }, { TEXT("parentClassPath"), TEXT("/Script/Engine.Actor") },
				{ TEXT("assetPath"), TEXT("/Engine/N2CTest") } });
			CreateArgs->SetBoolField(TEXT("openInEditor"), false);
			const FString CreateText = ExpectRefused(TEXT("create-blueprint-class at /Engine/N2CTest"), TEXT("create-blueprint-class"), CreateArgs);
			TestTrue(TEXT("create-blueprint-class names the setting"), CreateText.Contains(TEXT("Allow MCP Writes Outside Project")));

			ExpectRefused(TEXT("move-asset of the engine's Cube to /Game"), TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), CubePackage }, { TEXT("destinationPath"), Folder / TEXT("X") } }));
			ExpectRefused(TEXT("move-asset of a /Game asset to /Engine"), TEXT("move-asset"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("BP_N2CStaysInGame") }, { TEXT("destinationPath"), TEXT("/Engine/N2CTestMoved") } }));
			TSharedPtr<FJsonObject> OpenArgs = Args({ { TEXT("path"), TEXT("/Engine/N2CNew") } });
			OpenArgs->SetBoolField(TEXT("create_if_missing"), true);
			ExpectRefused(TEXT("open-content-browser-path /Engine/N2CNew with create_if_missing"), TEXT("open-content-browser-path"), OpenArgs);
			ExpectRefused(TEXT("create-folder under /NodeToCode"), TEXT("create-folder"), Args({ { TEXT("folderPath"), TEXT("/NodeToCode/N2CTest") } }));
			ExpectRefused(TEXT("create-folder under /Engine"), TEXT("create-folder"), Args({ { TEXT("folderPath"), TEXT("/Engine/N2CTest") } }));
			ExpectRefused(TEXT("copy-asset to /Engine"), TEXT("copy-asset"),
				Args({ { TEXT("sourcePath"), Asset }, { TEXT("destinationPath"), TEXT("/Engine/N2CTestCopy/BP_N2CCopy") } }));
			ExpectRefused(TEXT("rename-asset to /Engine"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), Asset }, { TEXT("destinationPath"), TEXT("/Engine/N2CTestRenamed/BP_N2CRenamed") } }));
			ExpectRefused(TEXT("rename-asset of the engine's Cube"), TEXT("rename-asset"),
				Args({ { TEXT("sourcePath"), CubePackage }, { TEXT("newName"), TEXT("N2CCube") } }));
			ExpectRefused(TEXT("move-folder to /Engine"), TEXT("move-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Box") }, { TEXT("destinationPath"), TEXT("/Engine/N2CTestFolder") } }));
			ExpectRefused(TEXT("rename-folder of /Engine/BasicShapes"), TEXT("rename-folder"),
				Args({ { TEXT("sourcePath"), TEXT("/Engine/BasicShapes") }, { TEXT("newName"), TEXT("N2CShapes") } }));
			const FString SaveText = ExpectRefused(TEXT("save-blueprint of the engine's StandardMacros"), TEXT("save-blueprint"),
				Args({ { TEXT("blueprintPath"), TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros") } }));
			TestTrue(TEXT("save-blueprint says SAVE_REFUSED"), SaveText.Contains(TEXT("SAVE_REFUSED")));

			// Nothing moved, nothing made
			TestTrue(TEXT("the /Game asset is in place"), BlueprintAtPath(Asset) == Blueprint);
			TestTrue(TEXT("the engine's Cube is in place"), FPackageName::DoesPackageExist(CubePackage));
			TestFalse(TEXT("no /Game/__N2CTest/X"), N2CTest::ContentFolderExists(Folder / TEXT("X")));
			TestTrue(TEXT("/Game/__N2CTest/Box is in place"), N2CTest::ContentFolderExists(Folder / TEXT("Box")));
			TestEqual(TEXT("StandardMacros.uasset not rewritten"), IFileManager::Get().GetTimeStamp(*StandardMacrosFile), MacrosStamp);
			const FString EngineContent = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir());
			for (const TCHAR* Name : { TEXT("N2CTest"), TEXT("N2CTestMoved"), TEXT("N2CNew"), TEXT("N2CTestCopy"), TEXT("N2CTestRenamed"),
				TEXT("N2CTestFolder"), TEXT("N2CShapes") })
			{
				TestFalse(FString::Printf(TEXT("nothing at Engine/Content/%s"), Name), IFileManager::Get().DirectoryExists(*(EngineContent / Name)));
			}
			TestFalse(TEXT("no Engine/Content/BasicShapes/N2CCube.uasset"), IFileManager::Get().FileExists(*(EngineContent / TEXT("BasicShapes/N2CCube.uasset"))));
			if (const TSharedPtr<IPlugin> NodeToCode = IPluginManager::Get().FindPlugin(TEXT("NodeToCode")))
			{
				TestFalse(TEXT("nothing at NodeToCode/Content/N2CTest"), IFileManager::Get().DirectoryExists(*(NodeToCode->GetContentDir() / TEXT("N2CTest"))));
			}
		});
	});

	Describe(TEXT("folders"), [this]()
	{
		It(TEXT("move-folder moves every asset of a folder and its subfolders"), [this]()
		{
			const FString Top = MakeAsset(TEXT("BP_N2CFolderTop"), Folder / TEXT("Source"));
			const FString Deep = MakeAsset(TEXT("BP_N2CFolderDeep"), Folder / TEXT("Source/Deep"));
			UBlueprint* TopAsset = BlueprintAtPath(Top);
			UBlueprint* DeepAsset = BlueprintAtPath(Deep);
			if (!TopAsset || !DeepAsset || !MakeFolder(Folder / TEXT("Target")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("move-folder"), TEXT("move-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Source") }, { TEXT("destinationPath"), Folder / TEXT("Target") } }));
			if (Json.IsValid())
			{
				ExpectMoved(TEXT("top asset"), TopAsset, Top, Folder / TEXT("Target/Source/BP_N2CFolderTop.BP_N2CFolderTop"));
				ExpectMoved(TEXT("subfolder asset"), DeepAsset, Deep, Folder / TEXT("Target/Source/Deep/BP_N2CFolderDeep.BP_N2CFolderDeep"));
				TestEqual(TEXT("assetsMoved"), static_cast<int32>(Json->GetNumberField(TEXT("assetsMoved"))), 2);
				TestEqual(TEXT("destinationPath"), Json->GetStringField(TEXT("destinationPath")), Folder / TEXT("Target/Source"));
				ExpectFolderMoveResult(*Json, { Top, Deep }, { Folder / TEXT("Target/Source/BP_N2CFolderTop"),
					Folder / TEXT("Target/Source/Deep/BP_N2CFolderDeep") }, Folder / TEXT("Source"));
			}
		});

		// mcp-tools-contentbrowser-fs-10 (card 0281): move-folder and rename-folder moved only assets, so an empty
		// folder stayed where it was, nothing appeared at the destination, and the tools reported success
		It(TEXT("move-folder and rename-folder move an empty folder and remove the source"), [this]()
		{
			if (!MakeFolder(Folder / TEXT("Empty")) || !MakeFolder(Folder / TEXT("Target")) || !MakeFolder(Folder / TEXT("Blank")))
			{
				return;
			}
			if (ExpectOk(TEXT("move-folder of an empty folder"), TEXT("move-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Empty") }, { TEXT("destinationPath"), Folder / TEXT("Target") } })).IsValid())
			{
				TestTrue(TEXT("the empty folder is at the destination"), N2CTest::ContentFolderExists(Folder / TEXT("Target/Empty")));
				TestFalse(TEXT("the source folder is gone"), N2CTest::ContentFolderExists(Folder / TEXT("Empty")));
			}
			if (ExpectOk(TEXT("rename-folder of an empty folder"), TEXT("rename-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Blank") }, { TEXT("newName"), TEXT("Renamed") } })).IsValid())
			{
				TestTrue(TEXT("the renamed folder exists"), N2CTest::ContentFolderExists(Folder / TEXT("Renamed")));
				TestFalse(TEXT("the old name is gone"), N2CTest::ContentFolderExists(Folder / TEXT("Blank")));
			}
		});

		// mcp-tools-contentbrowser-fs-10 (card 0281): an empty subfolder of a moved folder was not recreated at the
		// destination, and the emptied source folder stayed
		It(TEXT("move-folder keeps an empty subfolder and removes the source"), [this]()
		{
			const FString Kept = MakeAsset(TEXT("BP_N2CFolderKept"), Folder / TEXT("Source"));
			const FString Other = MakeAsset(TEXT("BP_N2CFolderOther"), Folder / TEXT("Source"));
			if (Kept.IsEmpty() || Other.IsEmpty() || !MakeFolder(Folder / TEXT("Source/EmptySub/Deeper")) || !MakeFolder(Folder / TEXT("Target")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("move-folder"), TEXT("move-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Source") }, { TEXT("destinationPath"), Folder / TEXT("Target") } }));
			if (Json.IsValid())
			{
				TestNotNull(TEXT("the asset moved"), BlueprintAtPath(Folder / TEXT("Target/Source/BP_N2CFolderKept.BP_N2CFolderKept")));
				TestNotNull(TEXT("the other asset moved"), BlueprintAtPath(Folder / TEXT("Target/Source/BP_N2CFolderOther.BP_N2CFolderOther")));
				TestTrue(TEXT("the empty subfolder is at the destination"), N2CTest::ContentFolderExists(Folder / TEXT("Target/Source/EmptySub")));
				TestTrue(TEXT("and its empty subfolder"), N2CTest::ContentFolderExists(Folder / TEXT("Target/Source/EmptySub/Deeper")));
				TestFalse(TEXT("the source folder is gone"), N2CTest::ContentFolderExists(Folder / TEXT("Source")));
				TestFalse(TEXT("the source folder is gone from disk"), N2CTest::ContentFolderOnDisk(Folder / TEXT("Source")));
				ExpectFolderMoveResult(*Json, { Kept, Other }, { Folder / TEXT("Target/Source/BP_N2CFolderKept"),
					Folder / TEXT("Target/Source/BP_N2CFolderOther") }, Folder / TEXT("Source"));
			}
		});

		It(TEXT("rename-folder keeps an empty subfolder and reports what moved"), [this]()
		{
			const FString Asset = MakeAsset(TEXT("BP_N2CRenamedFolderAsset"), Folder / TEXT("Before/Sub"));
			if (Asset.IsEmpty() || !MakeFolder(Folder / TEXT("Before/EmptySub")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("rename-folder"), TEXT("rename-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Before") }, { TEXT("newName"), TEXT("After") } }));
			if (Json.IsValid())
			{
				TestNotNull(TEXT("the asset moved"), BlueprintAtPath(Folder / TEXT("After/Sub/BP_N2CRenamedFolderAsset.BP_N2CRenamedFolderAsset")));
				TestTrue(TEXT("the empty subfolder is at the new name"), N2CTest::ContentFolderExists(Folder / TEXT("After/EmptySub")));
				TestFalse(TEXT("the old name is gone"), N2CTest::ContentFolderExists(Folder / TEXT("Before")));
				TestEqual(TEXT("assetsRenamed"), static_cast<int32>(Json->GetNumberField(TEXT("assetsRenamed"))), 1);
				ExpectFolderMoveResult(*Json, { Asset }, { Folder / TEXT("After/Sub/BP_N2CRenamedFolderAsset") }, Folder / TEXT("Before"));
			}
		});

		// mcp-tools-contentbrowser-fs-9 (card 0281): move-folder never compared the destination with the source, so a
		// folder moved into itself or its own subfolder re-nested every asset under it
		It(TEXT("move-folder refuses to move a folder into itself or its own subfolder, changing nothing"), [this]()
		{
			const FString Outer = MakeAsset(TEXT("BP_N2CNestOuter"), Folder / TEXT("Nest"));
			const FString Inner = MakeAsset(TEXT("BP_N2CNestInner"), Folder / TEXT("Nest/Inner"));
			if (Outer.IsEmpty() || Inner.IsEmpty())
			{
				return;
			}
			const TArray<FString> Before = PackagesUnder(Folder);
			for (const FString& Destination : { Folder / TEXT("Nest"), Folder / TEXT("Nest/Inner"), Folder / TEXT("nest/inner") })
			{
				const FMcpToolCallResult Result = Run(TEXT("move-folder"), Args({ { TEXT("sourcePath"), Folder / TEXT("Nest") }, { TEXT("destinationPath"), Destination } }));
				TestTrue(TEXT("refused into ") + Destination + TEXT(" (") + N2CTest::ResultText(Result).Left(200) + TEXT(")"), Result.bIsError);
			}
			TestEqual(TEXT("no asset moved"), FString::Join(PackagesUnder(Folder), TEXT("\n")), FString::Join(Before, TEXT("\n")));
			TestNotNull(TEXT("outer asset in place"), BlueprintAtPath(Outer));
			TestNotNull(TEXT("inner asset in place"), BlueprintAtPath(Inner));
			TestFalse(TEXT("no Nest/Nest folder"), N2CTest::ContentFolderExists(Folder / TEXT("Nest/Nest")));
			TestFalse(TEXT("no Nest/Inner/Nest folder"), N2CTest::ContentFolderExists(Folder / TEXT("Nest/Inner/Nest")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
