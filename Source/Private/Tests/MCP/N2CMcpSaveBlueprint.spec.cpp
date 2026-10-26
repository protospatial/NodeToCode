// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestContentFolder.h"
#include "MCP/Tools/Implementations/Blueprint/Compilation/N2CMcpSaveBlueprint.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/**
 * save-blueprint never prompts (card 0290, review mcp-tools-bp-structure-10, imp-mcp-tools-bp-structure-5). It used
 * FEditorFileUtils::PromptForCheckoutAndSave with bAlreadyCheckedOut=false, which shows the modal 'Check Out Assets'
 * or make-writable dialog on the game thread for a file under source control that is not checked out or read-only on
 * disk, so an unattended MCP call hung until someone clicked. Now it checks the file out itself, refuses what needs a
 * person with CHECKOUT_REQUIRED, CHECKED_OUT_BY_OTHER or READ_ONLY, saves through the editor's non-interactive save and
 * reports the file's source-control state. The provider cases run through FN2CMcpSaveBlueprint::Decide with the
 * states Perforce and Git report (the test editor has no provider); the read-only file is real, saved under
 * /Game/__N2CTestSave, which every case deletes before and after it runs.
 */
BEGIN_DEFINE_SPEC(FN2CMcpSaveBlueprintSpec, "NodeToCode.MCP.SaveBlueprint", N2C_TEST_FLAGS)

	const FString Folder = TEXT("/Game/__N2CTestSave");
	FString ReadOnlyFile;

	void Cleanup()
	{
		if (!ReadOnlyFile.IsEmpty())
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ReadOnlyFile, false);
			ReadOnlyFile.Reset();
		}
		N2CTest::DeleteContentFolder(Folder);
	}

	/** A Blueprint saved under Folder by create-blueprint-class, never opened in an editor */
	UBlueprint* CreateSaved(const FString& Name)
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("blueprintName"), Name);
		Arguments->SetStringField(TEXT("parentClassPath"), TEXT("/Script/Engine.Actor"));
		Arguments->SetStringField(TEXT("assetPath"), Folder);
		Arguments->SetBoolField(TEXT("openInEditor"), false);
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-blueprint-class"), Arguments);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		FString AssetPath;
		if (Result.bIsError || !Json.IsValid() || !Json->TryGetStringField(TEXT("assetPath"), AssetPath))
		{
			AddError(FString::Printf(TEXT("create-blueprint-class %s: %s"), *Name, *ErrorText(Result)));
			return nullptr;
		}
		return LoadObject<UBlueprint>(nullptr, *AssetPath);
	}

	static FString ErrorText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}

	static FString PackageFile(const UBlueprint* Blueprint)
	{
		FString Filename;
		FPackageName::DoesPackageExist(Blueprint->GetOutermost()->GetName(), &Filename);
		return FPaths::ConvertRelativePathToFull(Filename);
	}

	FMcpToolCallResult Save(const UBlueprint* Blueprint)
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		return N2CTest::RunTool(TEXT("save-blueprint"), Arguments);
	}

	/** A file on disk, under a provider that is connected */
	static FN2CSaveFileState Controlled(const TCHAR* Provider, bool bUsesLocalReadOnlyState)
	{
		FN2CSaveFileState State;
		State.Filename = TEXT("C:/Project/Content/BP_Door.uasset");
		State.bFileExists = true;
		State.bSourceControlAvailable = true;
		State.Provider = Provider;
		State.bUsesLocalReadOnlyState = bUsesLocalReadOnlyState;
		State.bHasState = true;
		State.bSourceControlled = true;
		return State;
	}

	void ExpectDecision(const FString& What, const FN2CSaveFileState& State, FN2CSaveDecision::EAction Action, const FString& Code)
	{
		const FN2CSaveDecision Decision = FN2CMcpSaveBlueprint::Decide(State);
		TestEqual(What + TEXT(": action"), static_cast<int32>(Decision.Action), static_cast<int32>(Action));
		TestEqual(What + TEXT(": error code"), Decision.ErrorCode, Code);
		if (Action == FN2CSaveDecision::EAction::Refuse)
		{
			TestTrue(What + TEXT(": the message names the file"), Decision.Message.Contains(State.Filename));
		}
	}

END_DEFINE_SPEC(FN2CMcpSaveBlueprintSpec)

void FN2CMcpSaveBlueprintSpec::Define()
{
	using EAction = FN2CSaveDecision::EAction;

	Describe(TEXT("decides without a dialog"), [this]()
	{
		It(TEXT("checks out a Perforce file that is not checked out, and saves one that is"), [this]()
		{
			FN2CSaveFileState State = Controlled(TEXT("Perforce"), true);
			State.bReadOnly = true;
			State.bCanCheckOut = true;
			ExpectDecision(TEXT("read-only, can check out"), State, EAction::CheckOut, FString());

			State.bCanCheckOut = false;
			State.bCheckedOut = true;
			State.bCanCheckIn = true;
			State.bReadOnly = false;
			ExpectDecision(TEXT("checked out"), State, EAction::Save, FString());
		});

		It(TEXT("refuses a file checked out by someone else with CHECKED_OUT_BY_OTHER"), [this]()
		{
			FN2CSaveFileState State = Controlled(TEXT("Perforce"), true);
			State.bReadOnly = true;
			State.bCheckedOutByOther = true;
			State.OtherUser = TEXT("pat");
			ExpectDecision(TEXT("exclusive lock by pat"), State, EAction::Refuse, TEXT("CHECKED_OUT_BY_OTHER"));
			TestTrue(TEXT("names who has it"), FN2CMcpSaveBlueprint::Decide(State).Message.Contains(TEXT("pat")));
		});

		It(TEXT("refuses with CHECKOUT_REQUIRED what a checkout cannot fix"), [this]()
		{
			FN2CSaveFileState State = Controlled(TEXT("Perforce"), true);
			State.bReadOnly = true;
			State.bCanCheckOut = true;
			State.bCurrent = false;
			ExpectDecision(TEXT("not at the head revision"), State, EAction::Refuse, TEXT("CHECKOUT_REQUIRED"));

			State.bCurrent = true;
			State.bCanCheckOut = false;
			ExpectDecision(TEXT("cannot be checked out"), State, EAction::Refuse, TEXT("CHECKOUT_REQUIRED"));

			State.bCanCheckOut = true;
			State.bDeleted = true;
			State.bCanCheckIn = true;
			ExpectDecision(TEXT("marked for delete"), State, EAction::Refuse, TEXT("CHECKOUT_REQUIRED"));
		});

		It(TEXT("saves a Git file, which Git reports as always checked out"), [this]()
		{
			FN2CSaveFileState State = Controlled(TEXT("Git"), false);
			State.bCheckedOut = true;
			ExpectDecision(TEXT("tracked, unmodified"), State, EAction::Save, FString());
		});

		It(TEXT("saves a Perforce file made writable by hand, as the editor does"), [this]()
		{
			FN2CSaveFileState State = Controlled(TEXT("Perforce"), true);
			State.bCanCheckOut = true;
			ExpectDecision(TEXT("writable, not checked out"), State, EAction::Save, FString());
		});

		It(TEXT("refuses a read-only file with READ_ONLY when no checkout can make it writable"), [this]()
		{
			FN2CSaveFileState State;
			State.Filename = TEXT("C:/Project/Content/BP_Door.uasset");
			State.bFileExists = true;
			State.bReadOnly = true;
			ExpectDecision(TEXT("no provider"), State, EAction::Refuse, TEXT("READ_ONLY"));

			State = Controlled(TEXT("Git"), false);
			State.bCheckedOut = true;
			State.bReadOnly = true;
			ExpectDecision(TEXT("Git, read-only"), State, EAction::Refuse, TEXT("READ_ONLY"));

			State = Controlled(TEXT("Perforce"), true);
			State.bSourceControlled = false;
			State.bReadOnly = true;
			ExpectDecision(TEXT("Perforce, not in the depot"), State, EAction::Refuse, TEXT("READ_ONLY"));
		});

		It(TEXT("saves a package that has never been saved"), [this]()
		{
			FN2CSaveFileState State;
			ExpectDecision(TEXT("no file"), State, EAction::Save, FString());
		});
	});

	Describe(TEXT("on disk"), [this]()
	{
		BeforeEach([this]()
		{
			Cleanup();
		});

		AfterEach([this]()
		{
			Cleanup();
			TestFalse(TEXT("scratch folder deleted"), N2CTest::ContentFolderOnDisk(Folder));
		});

		It(TEXT("returns READ_ONLY for a read-only file without a dialog, and writes nothing"), [this]()
		{
			UBlueprint* Blueprint = CreateSaved(TEXT("ReadOnly"));
			if (!Blueprint)
			{
				return;
			}
			const FString Filename = PackageFile(Blueprint);
			if (!TestTrue(TEXT("the file is on disk"), FPaths::FileExists(Filename)))
			{
				return;
			}
			ReadOnlyFile = Filename;
			if (!TestTrue(TEXT("file made read-only, as a source-controlled file that is not checked out"),
				FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, true)))
			{
				return;
			}
			const FDateTime Written = IFileManager::Get().GetTimeStamp(*Filename);
			const int64 Size = IFileManager::Get().FileSize(*Filename);
			Blueprint->MarkPackageDirty();

			// The old path, FEditorFileUtils::PromptForCheckoutAndSave, showed the make-writable dialog here (and,
			// under -unattended, cancelled without saying why)
			const FMcpToolCallResult Result = Save(Blueprint);
			const FString Text = ErrorText(Result);
			TestTrue(TEXT("an error"), Result.bIsError);
			TestTrue(TEXT("READ_ONLY: ") + Text, Text.StartsWith(TEXT("READ_ONLY: ")));
			TestTrue(TEXT("names the file"), Text.Contains(FPaths::GetCleanFilename(Filename)));
			TestTrue(TEXT("the package is still dirty"), Blueprint->GetOutermost()->IsDirty());
			TestTrue(TEXT("the file is still read-only"), IFileManager::Get().IsReadOnly(*Filename));
			TestEqual(TEXT("the file is unchanged (time)"), IFileManager::Get().GetTimeStamp(*Filename), Written);
			TestEqual(TEXT("the file is unchanged (size)"), IFileManager::Get().FileSize(*Filename), Size);

			// Writable again, the same call saves
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false);
			ReadOnlyFile.Reset();
			const FMcpToolCallResult Again = Save(Blueprint);
			TestFalse(TEXT("saves once writable: ") + ErrorText(Again), Again.bIsError);
			TestFalse(TEXT("the package is clean"), Blueprint->GetOutermost()->IsDirty());
		});

		It(TEXT("saves a writable file and reports its source-control state"), [this]()
		{
			UBlueprint* Blueprint = CreateSaved(TEXT("Writable"));
			if (!Blueprint)
			{
				return;
			}
			Blueprint->MarkPackageDirty();
			const FMcpToolCallResult Result = Save(Blueprint);
			if (!TestFalse(TEXT("saves: ") + ErrorText(Result), Result.bIsError))
			{
				return;
			}
			TestFalse(TEXT("the package is clean"), Blueprint->GetOutermost()->IsDirty());
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			const TSharedPtr<FJsonObject>* SourceControl = nullptr;
			if (!TestTrue(TEXT("a sourceControl object"), Json.IsValid() && Json->TryGetObjectField(TEXT("sourceControl"), SourceControl)))
			{
				return;
			}
			const bool bProvider = ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable();
			TestEqual(TEXT("enabled"), (*SourceControl)->GetBoolField(TEXT("enabled")), bProvider);
			if (!bProvider)
			{
				TestEqual(TEXT("status without a provider"), (*SourceControl)->GetStringField(TEXT("status")), FString(TEXT("none")));
			}
			TestFalse(TEXT("checkedOutNow"), (*SourceControl)->GetBoolField(TEXT("checkedOutNow")));
			TestFalse(TEXT("readOnly"), (*SourceControl)->GetBoolField(TEXT("readOnly")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
