// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CTagTypes.h"
#include "Core/N2CUserSettings.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"

/**
 * Export JSON writes to the folder the Node to Code window's Output field shows (card 0258). Before, BatchExportJson
 * always made its BatchJson_<time> folder under the translation output directory, whatever Browse... had chosen, and
 * the window dropped the result without a word. The window now passes its Output folder and shows the counts and the
 * folder (SN2CMainWindow::NotifyJsonExportResult); this spec exports a fixture graph into scratch folders.
 */
BEGIN_DEFINE_SPEC(FN2CBatchExportJsonSpec, "NodeToCode.Batch.ExportJson", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FTestBlueprint> BP;
	FN2CTagInfo Tag;

	bool MakeFixture()
	{
		BP = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CExportJson"));
		UEdGraph* Graph = BP->GetBlueprint() ? BP->EventGraph() : nullptr;
		if (!TestNotNull(TEXT("fixture Blueprint"), Graph))
		{
			return false;
		}
		BP->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		Tag = FN2CTagInfo();
		Tag.Tag = TEXT("N2CExportJsonSpec");
		Tag.GraphGuid = Graph->GraphGuid.ToString();
		Tag.GraphName = Graph->GetName();
		Tag.BlueprintPath = BP->GetBlueprint()->GetPathName();
		return BP->Errors().Num() == 0;
	}

	static bool IsUnder(const FString& Path, const FString& Folder)
	{
		return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Path), FPaths::ConvertRelativePathToFull(Folder));
	}

END_DEFINE_SPEC(FN2CBatchExportJsonSpec)

void FN2CBatchExportJsonSpec::Define()
{
	AfterEach([this]()
	{
		BP.Reset();
	});

	It(TEXT("writes into the chosen output folder"), [this]()
	{
		if (!MakeFixture()) { return; }
		N2CTest::FScratchDir Chosen(TEXT("N2CExportJsonChosen"));
		FN2CBatchJsonExportResult Result;
		const bool bRan = UN2CBatchTranslationOrchestrator::Get().BatchExportJson({ Tag }, Result, false, Chosen.Path);
		if (!TestTrue(TEXT("the export ran"), bRan)) { return; }
		TestEqual(TEXT("one graph exported"), Result.SuccessCount, 1);
		TestEqual(TEXT("none failed"), Result.FailureCount, 0);
		TestTrue(FString::Printf(TEXT("the export folder %s is in the chosen folder"), *Result.OutputPath), IsUnder(Result.OutputPath, Chosen.Path));
		TestTrue(TEXT("its folder is a new BatchJson_ folder"), FPaths::GetCleanFilename(Result.OutputPath).StartsWith(TEXT("BatchJson_")));
		TArray<FString> JsonFiles;
		IFileManager::Get().FindFiles(JsonFiles, *(Result.OutputPath / TEXT("*.json")), true, false);
		TestEqual(TEXT("the graph's JSON file is there"), JsonFiles.Num(), 1);
		TestTrue(TEXT("and the combined markdown"), FPaths::FileExists(Result.CombinedMarkdownPath) && IsUnder(Result.CombinedMarkdownPath, Chosen.Path));
		TestTrue(TEXT("no error"), Result.ErrorMessage.IsEmpty());
	});

	It(TEXT("uses the user's translation output folder when no folder is given"), [this]()
	{
		if (!MakeFixture()) { return; }
		N2CTest::FScratchDir Custom(TEXT("N2CExportJsonCustom"));
		UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
		const FString Before = UserSettings->CustomTranslationOutputDirectory.Path;
		ON_SCOPE_EXIT { UserSettings->CustomTranslationOutputDirectory.Path = Before; };
		UserSettings->CustomTranslationOutputDirectory.Path = Custom.Path;

		FN2CBatchJsonExportResult Result;
		if (!TestTrue(TEXT("the export ran"), UN2CBatchTranslationOrchestrator::Get().BatchExportJson({ Tag }, Result, true))) { return; }
		TestTrue(TEXT("into the custom translation output directory"), IsUnder(Result.OutputPath, Custom.Path));
	});

	It(TEXT("says why it did not run"), [this]()
	{
		AddExpectedMessage(TEXT("Cannot export with empty TagInfos array"), EAutomationExpectedMessageFlags::Contains, 1);
		FN2CBatchJsonExportResult Result;
		TestFalse(TEXT("nothing to export"), UN2CBatchTranslationOrchestrator::Get().BatchExportJson({}, Result));
		TestFalse(TEXT("with a reason for the notification"), Result.ErrorMessage.IsEmpty());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
