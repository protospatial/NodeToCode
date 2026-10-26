// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestContentFolder.h"
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
 * also when one fails. Name rules are NodeToCode.Names; path policy is NodeToCode.PathPolicy. Cases that fail until
 * their E12 card lands are KnownDefectIt (N2C_RUN_KNOWN_DEFECTS=1 runs them). Review ids:
 * imp-mcp-tools-contentbrowser-fs-9, mcp-tools-contentbrowser-fs-3, -7, -9, -10.
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

		// Known defect mcp-tools-contentbrowser-fs-3: with an object path and no newName the default name is
		// GetCleanFilename ("A.A"), so the destination becomes /Game/Dest/A.A.A.A and the move fails. Enable
		// (KnownDefectIt -> It) with card 0284.
		KnownDefectIt(TEXT("moves an asset by object path into another folder, keeping its name"), [this]()
		{
			const FString From = MakeAsset(TEXT("BP_N2CMoveByObject"), Folder);
			UBlueprint* Asset = BlueprintAtPath(From);
			if (!Asset)
			{
				return;
			}
			if (ExpectOk(TEXT("move by object path"), TEXT("move-asset"), Args({ { TEXT("sourcePath"), From }, { TEXT("destinationPath"), Folder / TEXT("Moved") } })).IsValid())
			{
				ExpectMoved(TEXT("move by object path"), Asset, From, Folder / TEXT("Moved/BP_N2CMoveByObject.BP_N2CMoveByObject"));
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

		// Known defect mcp-tools-contentbrowser-fs-7: create-folder, rename-asset, move-folder and rename-folder accept
		// only /Game/ or a path that contains "/Plugins/", and a plugin's content mounts at /<PluginName>/, so every
		// plugin is refused. Enable (KnownDefectIt -> It) with card 0182, whose policy allows project plugins' content.
		KnownDefectIt(TEXT("create-folder, rename-asset, move-folder and rename-folder accept the plugin's content"), [this]()
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
			}
		});

		// Known defect mcp-tools-contentbrowser-fs-10: move-folder and rename-folder move only assets, so an empty
		// folder stays where it was and nothing appears at the destination, and the tools report success. Enable
		// (KnownDefectIt -> It) with card 0281 (UEditorAssetSubsystem::RenameDirectory).
		KnownDefectIt(TEXT("move-folder and rename-folder move an empty folder and remove the source"), [this]()
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

		// Known defect mcp-tools-contentbrowser-fs-10: an empty subfolder of a moved folder is not recreated at the
		// destination. Enable (KnownDefectIt -> It) with card 0281.
		KnownDefectIt(TEXT("move-folder keeps an empty subfolder"), [this]()
		{
			const FString Kept = MakeAsset(TEXT("BP_N2CFolderKept"), Folder / TEXT("Source"));
			if (Kept.IsEmpty() || !MakeFolder(Folder / TEXT("Source/EmptySub")) || !MakeFolder(Folder / TEXT("Target")))
			{
				return;
			}
			if (ExpectOk(TEXT("move-folder"), TEXT("move-folder"),
				Args({ { TEXT("sourcePath"), Folder / TEXT("Source") }, { TEXT("destinationPath"), Folder / TEXT("Target") } })).IsValid())
			{
				TestNotNull(TEXT("the asset moved"), BlueprintAtPath(Folder / TEXT("Target/Source/BP_N2CFolderKept.BP_N2CFolderKept")));
				TestTrue(TEXT("the empty subfolder is at the destination"), N2CTest::ContentFolderExists(Folder / TEXT("Target/Source/EmptySub")));
			}
		});

		// Known defect mcp-tools-contentbrowser-fs-9: move-folder never compares the destination with the source, so a
		// folder moved into itself or its own subfolder re-nests every asset under it. Enable (KnownDefectIt -> It)
		// with card 0281.
		KnownDefectIt(TEXT("move-folder refuses to move a folder into itself or its own subfolder, changing nothing"), [this]()
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
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
