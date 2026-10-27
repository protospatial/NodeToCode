// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/Widgets/SN2CMainWindow.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "LLM/N2CLLMModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

// The graph state singleton, swapped for an isolated manager so the host project's BlueprintGraphState.json is untouched
N2C_TEST_PRIVATE_MEMBER(FN2CViewerJsonStateInstance, UN2CGraphStateManager**, &UN2CGraphStateManager::Instance)

// The main window's single-translate state and completion, and the viewer it shows
N2C_TEST_PRIVATE_MEMBER(FN2CViewerJsonWindowViewer, TSharedPtr<SN2CTranslationViewer> SN2CMainWindow::*,
	&SN2CMainWindow::TranslationViewer)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerJsonPendingGraph, FN2CTagInfo SN2CMainWindow::*, &SN2CMainWindow::PendingSingleTranslateGraph)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerJsonPendingJson, FString SN2CMainWindow::*, &SN2CMainWindow::PendingSingleTranslateJson)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerJsonComplete, void (SN2CMainWindow::*)(const FN2CTranslationResponse&, bool, const FN2CArchivedResponse&),
	&SN2CMainWindow::OnSingleTranslationComplete)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerJsonContent, FString SN2CTranslationViewer::*, &SN2CTranslationViewer::CurrentJsonContent)

/**
 * The translation viewer's JSON tab holds the N2C JSON a translation was made from (card 0501). Before, it was empty
 * on both paths that open the viewer: the single translate showed the translation without the JSON it sent, and a
 * recorded single translation (Tag Manager "View translation") looked for <graph folder>/<graph>.json and
 * <graph folder>/N2C_BP_<graph folder>.json, while the single translate writes the capture as N2C_BP_<run>.json in
 * the run folder, one folder above the entry folder the main window records (card 0252's archive layout).
 * Checks, with the real writer (SN2CMainWindow::SaveSingleTranslation, through FN2CTranslationArchive) into a scratch
 * output folder: UN2CGraphStateManager::LoadN2CJson returns the saved capture, also when the LLM named the graph
 * differently; and on a headless SN2CMainWindow, the single translate's completion shows the sent JSON and reopening
 * the recorded translation shows the saved capture. The batch layout is NodeToCode.Core.GraphState's.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationViewerJsonSpec, "NodeToCode.UI.TranslationViewerJson", N2C_TEST_FLAGS)

	/** An isolated graph state manager as UN2CGraphStateManager::Get(), C++ output into OutputDir, for the scope */
	struct FScopedState
	{
		TStrongObjectPtr<UN2CGraphStateManager> Manager;
		UN2CGraphStateManager* SavedInstance = nullptr;
		EN2CCodeLanguage SavedLanguage;
		FString SavedOutput;

		FScopedState(const FString& StateDir, const FString& OutputDir)
		{
			UN2CGraphStateManager*& Instance = *N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonStateInstance());
			SavedInstance = Instance;
			Manager.Reset(UN2CGraphStateManager::CreateIsolated(StateDir));
			Instance = Manager.Get();

			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedLanguage = Settings->TargetLanguage;
			SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
			Settings->TargetLanguage = EN2CCodeLanguage::Cpp;
			Settings->CustomTranslationOutputDirectory.Path = OutputDir;
		}

		~FScopedState()
		{
			*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonStateInstance()) = SavedInstance;
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->TargetLanguage = SavedLanguage;
			Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
		}
	};

	static FN2CTagInfo MakeTag(const FString& GraphName)
	{
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("N2CViewerJsonTest");
		Tag.GraphGuid = FGuid::NewGuid().ToString();
		Tag.GraphName = GraphName;
		Tag.BlueprintPath = TEXT("/Game/N2CTest/BP_N2CViewerJson.BP_N2CViewerJson");
		return Tag;
	}

	static FN2CTranslationResponse MakeResponse(const FString& GraphName)
	{
		FN2CTranslationResponse Response;
		FN2CGraphTranslation& Graph = Response.Graphs.AddDefaulted_GetRef();
		Graph.GraphName = GraphName;
		Graph.GraphType = TEXT("EventGraph");
		Graph.Code.GraphDeclaration = TEXT("void Run();");
		Graph.Code.GraphImplementation = TEXT("void AViewerJson::Run()\n{\n}\n");
		Graph.Code.ImplementationNotes = TEXT("notes");
		return Response;
	}

	/** A valid capture (the checked-in FanOutData fixture) named BlueprintName */
	bool LoadCapture(const FString& BlueprintName, FN2CBlueprint& OutBlueprint)
	{
		FString FixtureJson;
		const bool bLoaded = FFileHelper::LoadFileToString(FixtureJson, *(N2CTest::FixturesDir(TEXT("Capture/V1")) / TEXT("FanOutData.json")))
			&& FN2CSerializer::FromJson(FixtureJson, OutBlueprint);
		OutBlueprint.Metadata.Name = BlueprintName;
		return TestTrue(TEXT("the capture fixture loads"), bLoaded);
	}

	static FN2CGraphKey KeyOf(const FN2CTagInfo& Tag)
	{
		return FN2CGraphKey::FromStrings(Tag.BlueprintPath, Tag.GraphGuid);
	}

	bool SlateReady()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Slate is not initialized; skipping"));
			return false;
		}
		FN2CCodeEditorStyle::Initialize(); // no-op when the module already did it
		return true;
	}

END_DEFINE_SPEC(FN2CTranslationViewerJsonSpec)

void FN2CTranslationViewerJsonSpec::Define()
{
	It(TEXT("LoadN2CJson finds the capture a single translation saved one folder above the recorded graph folder"), [this]()
	{
		N2CTest::FScratchDir StateDir(TEXT("N2CViewerJsonState"));
		N2CTest::FScratchDir Output(TEXT("N2CViewerJsonOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);
		UN2CLLMModule* Module = UN2CLLMModule::Get();

		for (const bool bRenamed : {false, true})
		{
			const FString What = bRenamed ? TEXT("renamed by the LLM") : TEXT("same name");
			const FN2CTagInfo Tag = MakeTag(TEXT("EventGraph"));
			const FN2CTranslationResponse Response = MakeResponse(bRenamed ? TEXT("ReceiveBeginPlay") : *Tag.GraphName);
			FN2CBlueprint Blueprint;
			if (!LoadCapture(bRenamed ? TEXT("BP_N2CViewerJsonR") : TEXT("BP_N2CViewerJson"), Blueprint))
			{
				return;
			}
			if (!TestTrue(What + TEXT(": saved and recorded"), SN2CMainWindow::SaveSingleTranslation(Tag, Response, Blueprint)))
			{
				continue;
			}

			// The real save layout: the capture in the run folder, the code in <run>/<entry>
			const FString Root = Module->GetLatestTranslationPath();
			TArray<FString> Captures;
			IFileManager::Get().FindFiles(Captures, *(Root / TEXT("N2C_BP_*.json")), true, false);
			Captures.RemoveAll([](const FString& Name) { return Name.StartsWith(TEXT("N2C_BP_Minified_")); });
			if (!TestEqual(What + TEXT(": one capture in the translation root"), Captures.Num(), 1))
			{
				continue;
			}
			FString Saved;
			FFileHelper::LoadFileToString(Saved, *(Root / Captures[0]));
			TestEqual(What + TEXT(": the saved capture is the Blueprint"), Saved, FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Pretty));

			FString Json;
			if (TestTrue(What + TEXT(": LoadN2CJson finds it"), UN2CGraphStateManager::Get().LoadN2CJson(KeyOf(Tag), Json)))
			{
				TestEqual(What + TEXT(": LoadN2CJson returns the saved capture"), Json, Saved);
			}
		}
	});

	It(TEXT("the main window shows the JSON after a single translate and when the translation is reopened"), [this]()
	{
		if (!SlateReady())
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CViewerJsonState"));
		N2CTest::FScratchDir Output(TEXT("N2CViewerJsonOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);
		UN2CLLMModule* Module = UN2CLLMModule::Get();

		const FN2CTagInfo Tag = MakeTag(TEXT("EventGraph"));
		const FN2CTranslationResponse Response = MakeResponse(Tag.GraphName);
		FN2CBlueprint Blueprint;
		if (!LoadCapture(TEXT("BP_N2CViewerJsonWindow"), Blueprint))
		{
			return;
		}
		// What HandleOverlayTranslationRequest sends, and what its completion saves and records when the response arrives
		const FString Sent = FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed);
		FN2CArchivedResponse Saved;
		if (!TestTrue(TEXT("saved"), SN2CMainWindow::SaveSingleTranslation(Tag, Response, Blueprint, nullptr, &Saved)))
		{
			return;
		}

		const TSharedRef<SN2CMainWindow> Window = SNew(SN2CMainWindow);
		const TSharedPtr<SN2CTranslationViewer> Viewer = Window.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonWindowViewer());
		if (!TestTrue(TEXT("the window has a viewer"), Viewer.IsValid()))
		{
			return;
		}
		Window.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonPendingGraph()) = Tag;
		Window.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonPendingJson()) = Sent;
		(Window.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonComplete()))(Response, true, Saved);

		TestTrue(TEXT("after the translate: the viewer is shown"), Window->IsTranslationViewerVisible());
		TestEqual(TEXT("after the translate: the JSON tab holds the JSON sent"),
			Viewer.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonContent()), Sent);
		TestTrue(TEXT("after the translate: the pending JSON is cleared"),
			(Window.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonPendingJson())).IsEmpty());

		// The Tag Manager's "View translation": the recorded translation, loaded from disk
		Window->HideTranslationViewer();
		TestTrue(TEXT("closed: the JSON is cleared"), (Viewer.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonContent())).IsEmpty());
		Window->ShowTranslationViewer(Tag);
		TestTrue(TEXT("reopened: the viewer is shown"), Window->IsTranslationViewerVisible());
		TestEqual(TEXT("reopened: the JSON tab holds the saved capture"),
			Viewer.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CViewerJsonContent()), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Pretty));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
