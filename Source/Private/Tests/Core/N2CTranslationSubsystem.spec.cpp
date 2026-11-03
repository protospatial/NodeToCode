// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Core/Widgets/SN2CMainWindow.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "Subsystems/N2CTranslationSubsystem.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

// The graph state singleton, swapped for an isolated manager so the host project's BlueprintGraphState.json is untouched
N2C_TEST_PRIVATE_MEMBER(FN2CSessionSpecStateInstance, UN2CGraphStateManager**, &UN2CGraphStateManager::Instance)

// The count of open Node to Code windows: a host whose saved layout restores the window has one before any test runs
N2C_TEST_PRIVATE_MEMBER(FN2CSessionSpecPresenters, int32 UN2CTranslationSubsystem::*, &UN2CTranslationSubsystem::PresenterCount)

/**
 * UN2CTranslationSubsystem (card 0254): the overlay's and a row's Translate are sessions of the subsystem, so they work
 * with the Node to Code window closed and one click is one request however many windows are open. Before, the overlay
 * broadcast to every open SN2CMainWindow, each of which sent its own request (two windows, two bills), and with the
 * window closed nothing listened. A fake sender (SenderOverride) stands in for the provider: it records each request
 * and the test delivers the parsed result when it chooses, as an HTTP completion would arrive later. The graph is a
 * Blueprint built in code; results are written into a scratch output folder and an isolated graph state.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationSubsystemSpec, "NodeToCode.Editor.TranslationSubsystem", N2C_TEST_FLAGS)

	/** An isolated graph state manager as UN2CGraphStateManager::Get(), C++ output into OutputDir, for the scope */
	struct FScopedState
	{
		TStrongObjectPtr<UN2CGraphStateManager> Manager;
		UN2CGraphStateManager* SavedInstance = nullptr;
		EN2CCodeLanguage SavedLanguage;
		FString SavedOutput;

		FScopedState(const FString& StateDir, const FString& OutputDir)
		{
			UN2CGraphStateManager*& Instance = *N2CTest::GetPrivateMember(N2CTest::FN2CSessionSpecStateInstance());
			SavedInstance = Instance;
			Manager.Reset(UN2CGraphStateManager::CreateIsolated(StateDir));
			Instance = Manager.Get();

			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedLanguage = Settings->TargetLanguage;
			SavedOutput = GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path;
			Settings->TargetLanguage = EN2CCodeLanguage::Cpp;
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = OutputDir;
		}

		~FScopedState()
		{
			*N2CTest::GetPrivateMember(N2CTest::FN2CSessionSpecStateInstance()) = SavedInstance;
			GetMutableDefault<UN2CSettings>()->TargetLanguage = SavedLanguage;
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = SavedOutput;
		}
	};

	/** One request the fake sender received */
	struct FSent
	{
		FString Json;
		FN2CLLMRequestHandlePtr Handle;
		FOnN2CTranslationParsed OnParsed;
	};

	UN2CTranslationSubsystem* Subsystem = nullptr;
	TArray<FSent> Sent;
	TArray<EN2CTranslationSessionState> States;
	TArray<bool> BusyChanges;
	TArray<FGuid> Started;
	FDelegateHandle StatesHandle;
	FDelegateHandle BusyHandle;

	/** An Actor Blueprint whose event graph has a custom event running CollectGarbage */
	TUniquePtr<N2CTest::FTestBlueprint> Blueprint;
	UEdGraph* Graph = nullptr;

	FN2CTranslationRequest MakeRequest(EN2CTranslationSource Source) const
	{
		FN2CTranslationRequest Request;
		Request.BlueprintPath = Blueprint->GetBlueprint()->GetPathName();
		Request.GraphGuid = Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
		Request.GraphName = Graph->GetName();
		Request.Source = Source;
		return Request;
	}

	FN2CTranslationResponse MakeResponse() const
	{
		FN2CTranslationResponse Response;
		FN2CGraphTranslation& Translation = Response.Graphs.AddDefaulted_GetRef();
		Translation.GraphName = Graph->GetName();
		Translation.GraphType = TEXT("EventGraph");
		Translation.Code.GraphDeclaration = TEXT("void OnRun();");
		Translation.Code.GraphImplementation = TEXT("void ASessionTest::OnRun()\n{\n}\n");
		Response.Usage.InputTokens = 120;
		Response.Usage.OutputTokens = 30;
		return Response;
	}

	/** The provider's answer to request Index, as the HTTP completion would deliver it */
	void Deliver(int32 Index, const FN2CTranslationResponse& Response, bool bSuccess)
	{
		if (Sent.IsValidIndex(Index) && !Sent[Index].Handle->IsCancelled())
		{
			Sent[Index].OnParsed.ExecuteIfBound(Response, bSuccess);
		}
	}

	FN2CTranslationResponse MakeFailure() const
	{
		FN2CTranslationResponse Failed;
		Failed.Error = FN2CTranslationError::Make(TEXT("N2CSessionSpec: the provider refused the request"));
		return Failed;
	}

	static FString Name(EN2CTranslationSessionState State)
	{
		return UEnum::GetValueAsString(State);
	}

	FString StatesText() const
	{
		TArray<FString> Names;
		for (const EN2CTranslationSessionState State : States)
		{
			Names.Add(Name(State).RightChop(Name(State).Find(TEXT("::")) + 2));
		}
		return FString::Join(Names, TEXT(", "));
	}

	FString BusyText() const
	{
		TArray<FString> Names;
		for (const bool bBusy : BusyChanges)
		{
			Names.Add(bBusy ? TEXT("busy") : TEXT("idle"));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** No Node to Code window counts as open for the scope (the host may have restored one with its layout) */
	struct FScopedNoWindow
	{
		UN2CTranslationSubsystem* Subsystem;
		int32 Saved;

		explicit FScopedNoWindow(UN2CTranslationSubsystem* InSubsystem)
			: Subsystem(InSubsystem)
			, Saved(InSubsystem->*N2CTest::GetPrivateMember(N2CTest::FN2CSessionSpecPresenters()))
		{
			Subsystem->*N2CTest::GetPrivateMember(N2CTest::FN2CSessionSpecPresenters()) = 0;
		}

		~FScopedNoWindow()
		{
			Subsystem->*N2CTest::GetPrivateMember(N2CTest::FN2CSessionSpecPresenters()) = Saved;
		}
	};

	/** StartTranslation, remembering the session so AfterEach cancels it if the test left it running */
	FGuid Start(const FN2CTranslationRequest& Request, FString* OutError = nullptr)
	{
		const FGuid Id = Subsystem->StartTranslation(Request, OutError);
		if (Id.IsValid())
		{
			Started.Add(Id);
		}
		return Id;
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

END_DEFINE_SPEC(FN2CTranslationSubsystemSpec)

void FN2CTranslationSubsystemSpec::Define()
{
	BeforeEach([this]()
	{
		Subsystem = UN2CTranslationSubsystem::Get();
		if (!TestNotNull(TEXT("the translation subsystem exists"), Subsystem))
		{
			return;
		}
		Sent.Reset();
		States.Reset();
		BusyChanges.Reset();
		Started.Reset();
		Subsystem->SenderOverride = [this](const FString& Json, const FN2CLLMConfig&, EN2CCodeLanguage, const FOnN2CTranslationParsed& OnParsed)
		{
			FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
			Sent.Add({Json, Handle, OnParsed});
			return Handle;
		};
		StatesHandle = Subsystem->OnSessionChangedNative.AddLambda([this](const FN2CTranslationSession& Session)
		{
			States.Add(Session.Info.State);
		});
		BusyHandle = Subsystem->OnBusyChanged.AddLambda([this](bool bBusy) { BusyChanges.Add(bBusy); });

		Blueprint = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CSessionTest"));
		Graph = Blueprint->EventGraph();
		UK2Node_CustomEvent* Event = Blueprint->AddCustomEvent(Graph, TEXT("OnRun"));
		UK2Node_CallFunction* Call = Blueprint->AddCall(Graph, UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
		TestTrue(TEXT("the test graph is wired"), Event && Call
			&& Blueprint->Link(Event, UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute));
	});

	AfterEach([this]()
	{
		if (Subsystem)
		{
			// Nothing may stay running into the next spec
			for (const FGuid& Id : Started)
			{
				Subsystem->Cancel(Id);
			}
			Subsystem->SenderOverride = nullptr;
			Subsystem->OpenWindowOverride = nullptr;
			Subsystem->OnSessionChangedNative.Remove(StatesHandle);
			Subsystem->OnBusyChanged.Remove(BusyHandle);
		}
		Graph = nullptr;
		Blueprint.Reset();
	});

	It(TEXT("translates an overlay request with no main window open: one request, an archive run and a record"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		FScopedNoWindow NoWindow(Subsystem);
		N2CTest::FScratchDir StateDir(TEXT("N2CSessionSpecState"));
		N2CTest::FScratchDir Output(TEXT("N2CSessionSpecOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);

		const FN2CTranslationRequest Request = MakeRequest(EN2CTranslationSource::Overlay);
		const FGuid Id = Start(Request);
		if (!TestTrue(TEXT("the session started"), Id.IsValid()) || !TestEqual(TEXT("one request was sent"), Sent.Num(), 1))
		{
			return;
		}
		TestTrue(TEXT("the request carries the graph's N2C JSON"), Sent[0].Json.Contains(TEXT("OnRun")));
		TestTrue(TEXT("busy while the request is out"), Subsystem->IsBusy());
		const FN2CTranslationSession* Session = Subsystem->FindSession(Id);
		if (!TestNotNull(TEXT("the session is known"), Session))
		{
			return;
		}
		TestEqual(TEXT("it is sending"), Name(Session->Info.State), Name(EN2CTranslationSessionState::Sending));

		Deliver(0, MakeResponse(), true);
		Session = Subsystem->FindSession(Id);
		if (!TestNotNull(TEXT("the session is still known"), Session))
		{
			return;
		}
		TestEqual(TEXT("it succeeded"), Name(Session->Info.State), Name(EN2CTranslationSessionState::Succeeded));
		TestEqual(TEXT("its states, in order"), StatesText(), FString(TEXT("Queued, Capturing, Sending, Succeeded")));
		TestFalse(TEXT("not busy afterwards"), Subsystem->IsBusy());
		TestEqual(TEXT("busy was broadcast on and off"), BusyText(), FString(TEXT("busy, idle")));

		// Written through the archive into the output folder, and recorded for the tag list and the viewer
		TestTrue(TEXT("the run folder is under the output folder"),
			FPaths::IsUnderDirectory(Session->Info.RunFolder, Output.Path));
		TestTrue(TEXT("the run has its manifest"), FPaths::FileExists(Session->Info.RunFolder / TEXT("manifest.json")));
		TestNotNull(TEXT("the root entry was written"), Session->Saved.GetRoot());
		const FN2CGraphKey Key = FN2CGraphKey::FromStrings(Request.BlueprintPath, Request.GraphGuid);
		TestTrue(TEXT("the graph's translation is recorded"), UN2CGraphStateManager::Get().HasTranslation(Key));
		FN2CGraphTranslation Loaded;
		if (TestTrue(TEXT("and loads again"), UN2CGraphStateManager::Get().LoadTranslation(Key, Loaded)))
		{
			TestEqual(TEXT("with the translated code"), Loaded.Code.GraphDeclaration, FString(TEXT("void OnRun();")));
		}
	});

	It(TEXT("sends one request for one click with two main windows open, and both windows show the result"), [this]()
	{
		if (!Subsystem || !SlateReady())
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CSessionSpecState"));
		N2CTest::FScratchDir Output(TEXT("N2CSessionSpecOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);

		const TSharedRef<SN2CMainWindow> First = SNew(SN2CMainWindow);
		const TSharedRef<SN2CMainWindow> Second = SNew(SN2CMainWindow);
		TestTrue(TEXT("the windows show sessions themselves"), Subsystem->HasPresenter());

		// The overlay's Translate
		const FGuid Id = Start(MakeRequest(EN2CTranslationSource::Overlay));
		TestTrue(TEXT("the session started"), Id.IsValid());
		TestEqual(TEXT("one request for two windows"), Sent.Num(), 1);
		TestTrue(TEXT("the first window shows the progress"), First->IsBatchProgressVisible());
		TestTrue(TEXT("the second window shows the progress"), Second->IsBatchProgressVisible());

		Deliver(0, MakeResponse(), true);
		TestEqual(TEXT("still one request"), Sent.Num(), 1);
		TestTrue(TEXT("the first window shows the result"), First->IsTranslationViewerVisible());
		TestTrue(TEXT("the second window shows the result"), Second->IsTranslationViewerVisible());
	});

	It(TEXT("leaves IsBusy() false after a failed request, and after a graph that cannot be found"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		AddExpectedMessage(TEXT("Translation session"), EAutomationExpectedMessageFlags::Contains, 0);
		AddExpectedMessage(TEXT("BP_N2CSessionMissing"), EAutomationExpectedMessageFlags::Contains, 0);

		const FGuid Id = Start(MakeRequest(EN2CTranslationSource::Row));
		if (!TestEqual(TEXT("one request was sent"), Sent.Num(), 1))
		{
			return;
		}
		Deliver(0, MakeFailure(), false);
		FN2CTranslationSessionInfo Info;
		if (TestTrue(TEXT("the session is known"), Subsystem->GetSessionInfo(Id, Info)))
		{
			TestEqual(TEXT("it failed"), Name(Info.State), Name(EN2CTranslationSessionState::Failed));
			TestTrue(TEXT("with the provider's error"), Info.ErrorMessage.Contains(TEXT("the provider refused the request")));
			TestTrue(TEXT("nothing was written"), Info.RunFolder.IsEmpty());
		}
		TestFalse(TEXT("not busy after the failure"), Subsystem->IsBusy());

		// A Blueprint that does not exist fails before anything is sent
		FN2CTranslationRequest Missing = MakeRequest(EN2CTranslationSource::Overlay);
		Missing.BlueprintPath = TEXT("/Game/N2CTestMissing/BP_N2CSessionMissing.BP_N2CSessionMissing");
		const FGuid MissingId = Start(Missing);
		TestTrue(TEXT("the missing graph still gets a session"), MissingId.IsValid());
		TestEqual(TEXT("no request for it"), Sent.Num(), 1);
		if (TestTrue(TEXT("its session is known"), Subsystem->GetSessionInfo(MissingId, Info)))
		{
			TestEqual(TEXT("it failed"), Name(Info.State), Name(EN2CTranslationSessionState::Failed));
			TestFalse(TEXT("with a reason"), Info.ErrorMessage.IsEmpty());
		}
		TestFalse(TEXT("not busy after it"), Subsystem->IsBusy());
	});

	It(TEXT("cancels a session through its request handle: the result never lands and the session is Cancelled"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CSessionSpecState"));
		N2CTest::FScratchDir Output(TEXT("N2CSessionSpecOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);

		const FGuid Id = Start(MakeRequest(EN2CTranslationSource::Row));
		if (!TestEqual(TEXT("one request was sent"), Sent.Num(), 1))
		{
			return;
		}
		TestTrue(TEXT("Cancel stops it"), Subsystem->Cancel(Id));
		TestTrue(TEXT("the request's handle is cancelled"), Sent[0].Handle->IsCancelled());
		TestFalse(TEXT("not busy once cancelled"), Subsystem->IsBusy());
		TestFalse(TEXT("a second Cancel does nothing"), Subsystem->Cancel(Id));

		// A late completion (the handle refuses it; the session ignores it too)
		Sent[0].OnParsed.ExecuteIfBound(MakeResponse(), true);
		FN2CTranslationSessionInfo Info;
		if (TestTrue(TEXT("the session is known"), Subsystem->GetSessionInfo(Id, Info)))
		{
			TestEqual(TEXT("it stays Cancelled"), Name(Info.State), Name(EN2CTranslationSessionState::Cancelled));
			TestTrue(TEXT("nothing was written"), Info.RunFolder.IsEmpty());
		}
	});

	It(TEXT("refuses a second translation while one runs"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		AddExpectedMessage(TEXT("TRANSLATION_IN_PROGRESS"), EAutomationExpectedMessageFlags::Contains, 0);
		const FGuid First = Start(MakeRequest(EN2CTranslationSource::Overlay));
		FString Error;
		const FGuid Second = Start(MakeRequest(EN2CTranslationSource::Row), &Error);
		TestTrue(TEXT("the first started"), First.IsValid());
		TestFalse(TEXT("the second did not"), Second.IsValid());
		TestTrue(TEXT("and says why"), Error.StartsWith(TEXT("TRANSLATION_IN_PROGRESS")));
		TestEqual(TEXT("one request"), Sent.Num(), 1);
		Subsystem->Cancel(First);
	});

	It(TEXT("opens the Node to Code window on the result when no window is open (Open Result)"), [this]()
	{
		if (!Subsystem || !SlateReady())
		{
			return;
		}
		FScopedNoWindow NoWindow(Subsystem);
		N2CTest::FScratchDir StateDir(TEXT("N2CSessionSpecState"));
		N2CTest::FScratchDir Output(TEXT("N2CSessionSpecOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);

		const FGuid Id = Start(MakeRequest(EN2CTranslationSource::Overlay));
		Deliver(0, MakeResponse(), true);

		// The window, as its tab builds it
		TSharedPtr<SN2CMainWindow> Window;
		int32 Opened = 0;
		Subsystem->OpenWindowOverride = [&Window, &Opened]() { ++Opened; Window = SNew(SN2CMainWindow); };
		TestTrue(TEXT("the result is shown"), Subsystem->ShowResult(Id));
		TestEqual(TEXT("the window was opened once"), Opened, 1);
		TestTrue(TEXT("the window shows the translation"), Window.IsValid() && Window->IsTranslationViewerVisible());
		Window.Reset();
		TestFalse(TEXT("closing the window unregisters it"), Subsystem->HasPresenter());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
