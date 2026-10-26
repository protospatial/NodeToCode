// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Tools/Implementations/Blueprint/Classes/N2CMcpCreateBlueprintClassTool.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

/**
 * create-blueprint-class (card 0166): an existing folder is accepted, the asset is made by IAssetTools and
 * UBlueprintFactory (so a UserWidget parent gives a Widget Blueprint), an AnimInstance parent and wrong type and
 * parent pairs are refused, blueprintType is matched in any case, ifExists decides what an existing name does, and
 * a refused call creates nothing, not even its folder. The Blueprints are saved for real under /Game/__N2CTest, which
 * AfterEach deletes with everything in it. Review ids: mcp-tools-bp-structure-3, -4, -14, -23,
 * imp-mcp-tools-bp-structure-2, -12.
 */
BEGIN_DEFINE_SPEC(FN2CCreateBlueprintClassSpec, "NodeToCode.MCP.CreateBlueprintClass", N2C_TEST_FLAGS)

	/** The scratch folder every case creates in */
	const FString Folder = TEXT("/Game/__N2CTest");

	/** Arguments for a Blueprint in Folder, never opened in an editor */
	TSharedPtr<FJsonObject> Args(const FString& Name, const FString& Parent, const FString& InFolder = FString()) const
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("blueprintName"), Name);
		Arguments->SetStringField(TEXT("parentClassPath"), Parent);
		Arguments->SetStringField(TEXT("assetPath"), InFolder.IsEmpty() ? Folder : InFolder);
		Arguments->SetBoolField(TEXT("openInEditor"), false);
		return Arguments;
	}

	FMcpToolCallResult Create(const TSharedPtr<FJsonObject>& Arguments)
	{
		return N2CTest::RunTool(TEXT("create-blueprint-class"), Arguments);
	}

	/** The Blueprint a successful result names, loaded */
	UBlueprint* CreatedBlueprint(const FMcpToolCallResult& Result)
	{
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		FString AssetPath;
		if (!Json.IsValid() || !Json->TryGetStringField(TEXT("assetPath"), AssetPath))
		{
			return nullptr;
		}
		return LoadObject<UBlueprint>(nullptr, *AssetPath);
	}

	/** The error text of a result (its first text item) */
	static FString ErrorText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}

	static bool FolderExistsOnDisk(const FString& ContentFolder)
	{
		FString Filename;
		return FPackageName::TryConvertLongPackageNameToFilename(ContentFolder, Filename)
			&& IFileManager::Get().DirectoryExists(*Filename);
	}

	static bool AssetExists(const FString& ObjectPath)
	{
		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath))
			|| FindObject<UObject>(nullptr, *ObjectPath) != nullptr;
	}

	/** Deletes every asset in the scratch folder, its files and the folder */
	void DeleteScratchFolder()
	{
		TArray<UObject*> Assets;
		ForEachObjectOfClass(UBlueprint::StaticClass(), [this, &Assets](UObject* Object)
		{
			if (Object->GetPathName().StartsWith(Folder + TEXT("/")))
			{
				Assets.Add(Object);
			}
		});
		if (Assets.Num() > 0)
		{
			for (UObject* Asset : Assets)
			{
				Asset->GetOutermost()->SetDirtyFlag(false);
			}
			ObjectTools::ForceDeleteObjects(Assets, /*ShowConfirmation*/ false);
		}
		if (UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr)
		{
			if (AssetSubsystem->DoesDirectoryExist(Folder))
			{
				AssetSubsystem->DeleteDirectory(Folder);
			}
		}
		FString Filename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Folder, Filename))
		{
			IFileManager::Get().DeleteDirectory(*Filename, /*RequireExists*/ false, /*Tree*/ true);
		}
	}

END_DEFINE_SPEC(FN2CCreateBlueprintClassSpec)

void FN2CCreateBlueprintClassSpec::Define()
{
	BeforeEach([this]()
	{
		DeleteScratchFolder();
	});

	AfterEach([this]()
	{
		DeleteScratchFolder();
		TestFalse(TEXT("scratch folder deleted"), FolderExistsOnDisk(Folder));
	});

	It(TEXT("creates an Actor Blueprint in an existing folder and saves it"), [this]()
	{
		UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
		if (!TestTrue(TEXT("scratch folder made"), AssetSubsystem->MakeDirectory(Folder) && AssetSubsystem->DoesDirectoryExist(Folder)))
		{
			return;
		}

		const FMcpToolCallResult Result = Create(Args(TEXT("Door"), TEXT("/Script/Engine.Actor")));
		if (!TestFalse(TEXT("succeeds: ") + ErrorText(Result), Result.bIsError))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		TestTrue(TEXT("saved"), Json->GetBoolField(TEXT("saved")));
		TestEqual(TEXT("BP_ prefix added"), Json->GetStringField(TEXT("blueprintName")), FString(TEXT("BP_Door")));
		TestEqual(TEXT("package path"), Json->GetStringField(TEXT("packagePath")), Folder + TEXT("/BP_Door"));

		UBlueprint* Blueprint = CreatedBlueprint(Result);
		if (!TestNotNull(TEXT("Blueprint"), Blueprint))
		{
			return;
		}
		TestEqual(TEXT("a plain Blueprint"), Blueprint->GetClass(), UBlueprint::StaticClass());
		TestEqual(TEXT("parent"), Blueprint->ParentClass.Get(), AActor::StaticClass());
		TestNotNull(TEXT("generated class"), Blueprint->GeneratedClass.Get());
		TestTrue(TEXT("package file on disk"), FPackageName::DoesPackageExist(Folder + TEXT("/BP_Door")));
		TestFalse(TEXT("package saved clean"), Blueprint->GetOutermost()->IsDirty());
		N2CTest::ExpectCompilesClean(*this, TEXT("created Blueprint"), Blueprint);

		// A second Blueprint in the folder the first one made
		const FMcpToolCallResult Second = Create(Args(TEXT("Window"), TEXT("/Script/Engine.Actor")));
		TestFalse(TEXT("second Blueprint in the same folder: ") + ErrorText(Second), Second.bIsError);
	});

	It(TEXT("creates a Widget Blueprint from a UserWidget parent"), [this]()
	{
		const FMcpToolCallResult Result = Create(Args(TEXT("MainMenu"), TEXT("/Script/UMG.UserWidget")));
		if (!TestFalse(TEXT("succeeds: ") + ErrorText(Result), Result.bIsError))
		{
			return;
		}
		UBlueprint* Blueprint = CreatedBlueprint(Result);
		const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
		if (!TestNotNull(TEXT("a UWidgetBlueprint"), WidgetBlueprint))
		{
			return;
		}
		TestNotNull(TEXT("with a widget tree"), WidgetBlueprint->WidgetTree.Get());
		TestEqual(TEXT("assetClass in the result"),
			N2CTest::ResultJson(Result)->GetObjectField(TEXT("blueprintInfo"))->GetStringField(TEXT("assetClass")),
			FString(TEXT("WidgetBlueprint")));
	});

	It(TEXT("refuses an AnimInstance parent and creates nothing"), [this]()
	{
		const FMcpToolCallResult Result = Create(Args(TEXT("Anim"), TEXT("/Script/Engine.AnimInstance")));
		TestTrue(TEXT("refused"), Result.bIsError);
		TestTrue(TEXT("names the skeleton: ") + ErrorText(Result), ErrorText(Result).Contains(TEXT("skeleton")));
		TestFalse(TEXT("no folder created"), FolderExistsOnDisk(Folder));
		TestFalse(TEXT("no asset"), AssetExists(Folder + TEXT("/BP_Anim.BP_Anim")));
	});

	It(TEXT("refuses the interface type with an Actor parent and creates nothing"), [this]()
	{
		TSharedPtr<FJsonObject> Arguments = Args(TEXT("NotAnInterface"), TEXT("/Script/Engine.Actor"));
		Arguments->SetStringField(TEXT("blueprintType"), TEXT("interface"));
		const FMcpToolCallResult Result = Create(Arguments);
		TestTrue(TEXT("refused"), Result.bIsError);
		TestTrue(TEXT("names the Interface parent: ") + ErrorText(Result),
			ErrorText(Result).Contains(TEXT("/Script/CoreUObject.Interface")));
		TestFalse(TEXT("no folder created"), FolderExistsOnDisk(Folder));
		TestFalse(TEXT("no asset"), AssetExists(Folder + TEXT("/BP_NotAnInterface.BP_NotAnInterface")));
	});

	It(TEXT("refuses the normal type with the Interface parent"), [this]()
	{
		TSharedPtr<FJsonObject> Arguments = Args(TEXT("NormalInterface"), TEXT("/Script/CoreUObject.Interface"));
		Arguments->SetStringField(TEXT("blueprintType"), TEXT("normal"));
		const FMcpToolCallResult Result = Create(Arguments);
		TestTrue(TEXT("refused"), Result.bIsError);
		TestFalse(TEXT("no folder created"), FolderExistsOnDisk(Folder));
	});

	It(TEXT("creates a Blueprint interface from the Interface parent, with the type in any case"), [this]()
	{
		TSharedPtr<FJsonObject> Arguments = Args(TEXT("Usable"), TEXT("/Script/CoreUObject.Interface"));
		Arguments->SetStringField(TEXT("blueprintType"), TEXT("Interface"));
		Arguments->SetBoolField(TEXT("addPrefix"), false);
		const FMcpToolCallResult Result = Create(Arguments);
		if (!TestFalse(TEXT("succeeds: ") + ErrorText(Result), Result.bIsError))
		{
			return;
		}
		UBlueprint* Blueprint = CreatedBlueprint(Result);
		if (!TestNotNull(TEXT("Blueprint"), Blueprint))
		{
			return;
		}
		TestEqual(TEXT("name kept without addPrefix"), Blueprint->GetName(), FString(TEXT("Usable")));
		TestEqual(TEXT("an interface Blueprint"), (int32)Blueprint->BlueprintType, (int32)BPTYPE_Interface);
	});

	It(TEXT("refuses an unknown blueprintType and creates nothing"), [this]()
	{
		TSharedPtr<FJsonObject> Arguments = Args(TEXT("Typo"), TEXT("/Script/Engine.Actor"));
		Arguments->SetStringField(TEXT("blueprintType"), TEXT("actor"));
		const FMcpToolCallResult Result = Create(Arguments);
		TestTrue(TEXT("refused"), Result.bIsError);
		TestTrue(TEXT("lists the types: ") + ErrorText(Result), ErrorText(Result).Contains(TEXT("auto, normal, const or interface")));
		TestFalse(TEXT("no folder created"), FolderExistsOnDisk(Folder));
	});

	It(TEXT("refuses the same name twice with ifExists error, and picks a free name with unique"), [this]()
	{
		const FMcpToolCallResult First = Create(Args(TEXT("Twice"), TEXT("/Script/Engine.Actor")));
		if (!TestFalse(TEXT("first succeeds: ") + ErrorText(First), First.bIsError))
		{
			return;
		}
		UBlueprint* Original = CreatedBlueprint(First);

		const FMcpToolCallResult Again = Create(Args(TEXT("Twice"), TEXT("/Script/Engine.Actor")));
		TestTrue(TEXT("second refused (ifExists defaults to error)"), Again.bIsError);
		TestTrue(TEXT("says it exists: ") + ErrorText(Again), ErrorText(Again).Contains(TEXT("already exists")));
		TestEqual(TEXT("original kept"), CreatedBlueprint(First), Original);

		TSharedPtr<FJsonObject> ErrorArgs = Args(TEXT("BP_Twice"), TEXT("/Script/Engine.Actor"));
		ErrorArgs->SetStringField(TEXT("ifExists"), TEXT("ERROR"));
		TestTrue(TEXT("explicit ifExists error refused too"), Create(ErrorArgs).bIsError);

		TSharedPtr<FJsonObject> UniqueArgs = Args(TEXT("Twice"), TEXT("/Script/Engine.Actor"));
		UniqueArgs->SetStringField(TEXT("ifExists"), TEXT("unique"));
		const FMcpToolCallResult Unique = Create(UniqueArgs);
		if (!TestFalse(TEXT("unique succeeds: ") + ErrorText(Unique), Unique.bIsError))
		{
			return;
		}
		const FString UniqueName = N2CTest::ResultJson(Unique)->GetStringField(TEXT("blueprintName"));
		TestNotEqual(TEXT("a free name"), UniqueName, FString(TEXT("BP_Twice")));
		TestTrue(TEXT("based on the name: ") + UniqueName, UniqueName.StartsWith(TEXT("BP_Twice")));

		TSharedPtr<FJsonObject> BadArgs = Args(TEXT("Twice"), TEXT("/Script/Engine.Actor"));
		BadArgs->SetStringField(TEXT("ifExists"), TEXT("overwrite"));
		TestTrue(TEXT("unknown ifExists refused"), Create(BadArgs).bIsError);
	});

	It(TEXT("refuses invalid names before creating the folder"), [this]()
	{
		for (const FString& Name : { FString(TEXT("Has Space")), FString(TEXT("Dot.Name")), FString(TEXT("Slash/Name")),
			FString(TEXT("")), FString::ChrN(120, TEXT('A')) })
		{
			const FMcpToolCallResult Result = Create(Args(Name, TEXT("/Script/Engine.Actor")));
			TestTrue(FString::Printf(TEXT("'%s' refused"), *Name.Left(20)), Result.bIsError);
		}
		TestFalse(TEXT("no folder created"), FolderExistsOnDisk(Folder));
	});

	// Undoing a transaction that created the Blueprint destroyed it in memory (an ensure in
	// FBlueprintCompilationManager, SkeletonCompiledBlueprints.Num() == 1) while its saved package stayed on disk, so
	// creation is not an undo step, as in the Content Browser
	It(TEXT("is not an undo step, so Undo cannot destroy the saved Blueprint"), [this]()
	{
		const int32 QueueBefore = N2CTest::UndoQueueLength();
		TSharedPtr<FJsonObject> Arguments = Args(TEXT("NotUndone"), TEXT("/Script/Engine.Actor"));
		Arguments->SetStringField(TEXT("description"), TEXT("kept"));
		const FMcpToolCallResult Result = Create(Arguments);
		if (!TestFalse(TEXT("succeeds: ") + ErrorText(Result), Result.bIsError))
		{
			return;
		}
		UBlueprint* Blueprint = CreatedBlueprint(Result);
		if (!TestNotNull(TEXT("Blueprint"), Blueprint))
		{
			return;
		}
		TestEqual(TEXT("undo queue unchanged"), N2CTest::UndoQueueLength(), QueueBefore);
		TestEqual(TEXT("description set"), Blueprint->BlueprintDescription, FString(TEXT("kept")));
		TestTrue(TEXT("has its event graph"), Blueprint->UbergraphPages.Num() > 0);
	});

	It(TEXT("advertises ifExists and addPrefix and no longer generateConstructionScript"), [this]()
	{
		const FMcpToolDefinition Definition = FN2CMcpCreateBlueprintClassTool().GetDefinition();
		const TSharedPtr<FJsonObject> Properties = Definition.InputSchema->GetObjectField(TEXT("properties"));
		TestTrue(TEXT("ifExists"), Properties->HasField(TEXT("ifExists")));
		TestTrue(TEXT("addPrefix"), Properties->HasField(TEXT("addPrefix")));
		TestFalse(TEXT("generateConstructionScript"), Properties->HasField(TEXT("generateConstructionScript")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
