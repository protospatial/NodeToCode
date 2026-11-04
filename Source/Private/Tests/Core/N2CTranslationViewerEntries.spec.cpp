// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CUserSettings.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/N2CGraphStateSubsystem.h"
#include "Subsystems/N2CTranslationSubsystem.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

/**
 * The translation viewer shows every graph of a translation (card 0257). Before, the window passed only the root graph
 * to the viewer, so the nested graphs, structs and enums of a translation at Translation Depth 1 or more were only on
 * disk; and the code pane was editable but Copy copied the stored text and a tab switch overwrote the edit. Checks on a
 * headless viewer: a three-graph fixture manifest lists three entries, root first, labelled; a batch manifest gives the
 * one item's entries; a response lists its graphs with the asked-for one first; View Translation on a recorded graph
 * loads its whole run; the code is read-only until Edit, and an edit survives tab and entry switches and is what Copy
 * copies.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationViewerEntriesSpec, "NodeToCode.UI.TranslationViewer.Entries", N2C_TEST_FLAGS)

	TSharedPtr<SN2CTranslationViewer> Viewer;

	static void Write(const FString& Path, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/**
	 * A run folder like the archive writes: entries nested Helper (listed first), root EventGraph, struct FPoint; with
	 * ItemCount 2, a second item (another graph) with its own root entry
	 */
	static FString MakeRun(const FString& Folder, int32 ItemCount = 1)
	{
		Write(Folder / TEXT("EventGraph/EventGraph.cpp"), TEXT("void ABP::BeginPlay() { Helper(); }"));
		Write(Folder / TEXT("EventGraph/EventGraph.h"), TEXT("virtual void BeginPlay() override;"));
		Write(Folder / TEXT("EventGraph/EventGraph_Notes.txt"), TEXT("root notes"));
		Write(Folder / TEXT("Helper/Helper.cpp"), TEXT("void ABP::Helper() {}"));
		Write(Folder / TEXT("Helper/Helper_Notes.txt"), TEXT("helper notes"));
		Write(Folder / TEXT("FPoint/FPoint.h"), TEXT("USTRUCT() struct FPoint { int32 X; };"));
		FString Items = TEXT("{\"graphName\":\"EventGraph\",\"graphGuid\":\"11111111-1111-1111-1111-111111111111\",\"folder\":\"\"}");
		FString Entries =
			TEXT("{\"item\":0,\"graphName\":\"Helper\",\"graphType\":\"Function\",\"role\":\"nested\",\"folder\":\"Helper\",")
			TEXT("\"files\":{\"declaration\":\"\",\"implementation\":\"Helper/Helper.cpp\",\"notes\":\"Helper/Helper_Notes.txt\"}},")
			TEXT("{\"item\":0,\"graphName\":\"EventGraph\",\"graphType\":\"EventGraph\",\"role\":\"root\",\"folder\":\"EventGraph\",")
			TEXT("\"files\":{\"declaration\":\"EventGraph/EventGraph.h\",\"implementation\":\"EventGraph/EventGraph.cpp\",\"notes\":\"EventGraph/EventGraph_Notes.txt\"}},")
			TEXT("{\"item\":0,\"graphName\":\"FPoint\",\"graphType\":\"Struct\",\"role\":\"struct\",\"folder\":\"FPoint\",")
			TEXT("\"files\":{\"declaration\":\"FPoint/FPoint.h\",\"implementation\":\"\",\"notes\":\"\"}}");
		if (ItemCount > 1)
		{
			Write(Folder / TEXT("Other/Run/Run.cpp"), TEXT("void ABP::Run() {}"));
			Items += TEXT(",{\"graphName\":\"Run\",\"graphGuid\":\"22222222-2222-2222-2222-222222222222\",\"folder\":\"Other\"}");
			Entries += TEXT(",{\"item\":1,\"graphName\":\"Run\",\"graphType\":\"Function\",\"role\":\"root\",\"folder\":\"Other/Run\",")
				TEXT("\"files\":{\"declaration\":\"\",\"implementation\":\"Other/Run/Run.cpp\",\"notes\":\"\"}}");
		}
		const FString Manifest = FString::Printf(TEXT("{\"schemaVersion\":1,\"kind\":\"%s\",\"language\":\"Cpp\",\"items\":[%s],\"entries\":[%s]}"),
			ItemCount > 1 ? TEXT("batch") : TEXT("single"), *Items, *Entries);
		Write(Folder / TEXT("manifest.json"), Manifest);
		return Folder / TEXT("manifest.json");
	}

	TArray<FString> Labels() const
	{
		TArray<FString> Out;
		for (const FN2CViewerEntry& Entry : Viewer->GetEntries())
		{
			Out.Add(SN2CTranslationViewer::GetEntryLabel(Entry));
		}
		return Out;
	}

	FString EditorText() const
	{
		return Viewer->GetCodeEditor().IsValid() ? Viewer->GetCodeEditor()->GetText().ToString() : FString();
	}

END_DEFINE_SPEC(FN2CTranslationViewerEntriesSpec)

void FN2CTranslationViewerEntriesSpec::Define()
{
	BeforeEach([this]()
	{
		Viewer.Reset();
		if (FSlateApplication::IsInitialized())
		{
			FN2CCodeEditorStyle::Initialize();
			Viewer = SNew(SN2CTranslationViewer);
		}
	});

	AfterEach([this]() { Viewer.Reset(); });

	It(TEXT("lists the three entries of a fixture manifest, root first, with each one's tabs and notes"), [this]()
	{
		if (!Viewer.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		N2CTest::FScratchDir Run(TEXT("N2CViewerEntriesRun"));
		const FString Manifest = MakeRun(Run.Path);
		if (!TestTrue(TEXT("the manifest loads"), Viewer->LoadRunManifest(Manifest, TEXT("11111111-1111-1111-1111-111111111111"), TEXT("EventGraph"), FString()))) { return; }
		TestEqual(TEXT("three entries, root first"), Labels(), TArray<FString>({ TEXT("EventGraph"), TEXT("Helper (nested)"), TEXT("FPoint (struct)") }));
		TestEqual(TEXT("the root is shown"), Viewer->GetSelectedEntryIndex(), 0);
		TestEqual(TEXT("on its implementation"), EditorText(), FString(TEXT("void ABP::BeginPlay() { Helper(); }")));
		TestEqual(TEXT("the root's notes were read"), Viewer->GetEntries()[0].Translation.Code.ImplementationNotes, FString(TEXT("root notes")));

		Viewer->SelectEntry(2);
		TestEqual(TEXT("the struct opens on its declaration (it has no implementation)"), Viewer->GetActiveTab(), FString(TEXT("h")));
		TestEqual(TEXT("which is shown"), EditorText(), FString(TEXT("USTRUCT() struct FPoint { int32 X; };")));
		Viewer->SelectEntry(1);
		TestEqual(TEXT("the nested graph opens on its implementation"), Viewer->GetActiveTab(), FString(TEXT("cpp")));
		TestEqual(TEXT("its code"), EditorText(), FString(TEXT("void ABP::Helper() {}")));
	});

	It(TEXT("gives a batch run's entries of the one item asked for"), [this]()
	{
		if (!Viewer.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		N2CTest::FScratchDir Run(TEXT("N2CViewerEntriesBatch"));
		const FString Manifest = MakeRun(Run.Path, 2);
		if (!TestTrue(TEXT("the second item loads"), Viewer->LoadRunManifest(Manifest, TEXT("22222222-2222-2222-2222-222222222222"), TEXT("Run"), FString()))) { return; }
		TestEqual(TEXT("its one entry"), Labels(), TArray<FString>({ TEXT("Run") }));
		TestEqual(TEXT("with its code"), EditorText(), FString(TEXT("void ABP::Run() {}")));
		TestFalse(TEXT("an unknown graph does not load"), Viewer->LoadRunManifest(Manifest, TEXT("33333333-3333-3333-3333-333333333333"), TEXT("X"), FString()));
		TestEqual(TEXT("and leaves the viewer as it was"), Labels(), TArray<FString>({ TEXT("Run") }));
	});

	It(TEXT("lists a response's graphs with the asked-for one first"), [this]()
	{
		if (!Viewer.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		FN2CTranslationResponse Response;
		const TCHAR* Names[] = { TEXT("EColor"), TEXT("Helper"), TEXT("EventGraph") };
		const TCHAR* Types[] = { TEXT("Enum"), TEXT("Function"), TEXT("EventGraph") };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FN2CGraphTranslation& Graph = Response.Graphs.AddDefaulted_GetRef();
			Graph.GraphName = Names[Index];
			Graph.GraphType = Types[Index];
			Graph.Code.GraphImplementation = FString::Printf(TEXT("// %s"), Names[Index]);
		}
		Viewer->LoadResponse(Response, TEXT("EventGraph"), FString(), EN2CCodeLanguage::Cpp, 2);
		TestEqual(TEXT("root first, then in response order"), Labels(),
			TArray<FString>({ TEXT("EventGraph"), TEXT("EColor (enum)"), TEXT("Helper (nested)") }));
	});

	It(TEXT("loads the whole run when a recorded graph is viewed"), [this]()
	{
		if (!Viewer.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		// The capture written beside the run has no nodes (only the response matters here); its validator says so
		AddExpectedMessage(TEXT("No nodes found in any graph"), EAutomationExpectedMessageFlags::Contains, 0);
		AddExpectedMessage(TEXT("Blueprint validation failed"), EAutomationExpectedMessageFlags::Contains, 0);
		N2CTest::FScratchDir StateDir(TEXT("N2CViewerEntriesState"));
		N2CTest::FScratchDir Output(TEXT("N2CViewerEntriesOut"));
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(StateDir.Path));
		UN2CGraphStateSubsystem* State = UN2CGraphStateSubsystem::Get();
		if (!TestNotNull(TEXT("the graph state subsystem"), State)) { return; }
		State->SetManagerOverride(Manager.Get());
		const FString SavedOutput = GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path;
		GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = Output.Path;
		ON_SCOPE_EXIT
		{
			State->SetManagerOverride(nullptr);
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = SavedOutput;
		};

		FN2CTranslationResponse Response;
		const TCHAR* Names[] = { TEXT("EventGraph"), TEXT("Helper"), TEXT("FPoint") };
		const TCHAR* Types[] = { TEXT("EventGraph"), TEXT("Function"), TEXT("Struct") };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FN2CGraphTranslation& Graph = Response.Graphs.AddDefaulted_GetRef();
			Graph.GraphName = Names[Index];
			Graph.GraphType = Types[Index];
			Graph.Code.GraphDeclaration = FString::Printf(TEXT("// %s.h"), Names[Index]);
			Graph.Code.GraphImplementation = Index == 2 ? FString() : FString::Printf(TEXT("// %s.cpp"), Names[Index]);
		}
		FN2CBlueprint Capture;
		Capture.Metadata.Name = TEXT("BP_N2CViewerEntries");
		Capture.Metadata.BlueprintClass = TEXT("BP_N2CViewerEntries_C");
		Capture.Graphs.AddDefaulted_GetRef().Name = TEXT("EventGraph");
		FN2CTagInfo Tag;
		Tag.GraphName = TEXT("EventGraph");
		Tag.GraphGuid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Tag.BlueprintPath = TEXT("/Game/N2CTest/BP_N2CViewerEntries.BP_N2CViewerEntries");
		FN2CTranslationRunInfo RunInfo = UN2CTranslationSubsystem::MakeSingleRunInfo(Capture);
		RunInfo.Language = EN2CCodeLanguage::Cpp;
		if (!TestTrue(TEXT("the run was written and recorded"), UN2CTranslationSubsystem::SaveSingleTranslation(Tag, Response, Capture, &RunInfo))) { return; }

		if (!TestTrue(TEXT("View Translation loads it"), Viewer->LoadTranslation(Tag))) { return; }
		TestEqual(TEXT("every graph of the run"), Labels(), TArray<FString>({ TEXT("EventGraph"), TEXT("Helper (nested)"), TEXT("FPoint (struct)") }));
		TestEqual(TEXT("the struct's declaration was read from its file"), Viewer->GetEntries().Num() == 3
			? Viewer->GetEntries()[2].Translation.Code.GraphDeclaration.TrimStartAndEnd() : FString(), FString(TEXT("// FPoint.h")));
	});

	It(TEXT("is read-only until Edit; then an edit survives tab and entry switches and is what Copy copies"), [this]()
	{
		if (!Viewer.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		N2CTest::FScratchDir Run(TEXT("N2CViewerEntriesEdit"));
		if (!TestTrue(TEXT("the manifest loads"), Viewer->LoadRunManifest(MakeRun(Run.Path), TEXT("11111111-1111-1111-1111-111111111111"), TEXT("EventGraph"), FString()))) { return; }
		TSharedPtr<SN2CCodeEditor> Editor = Viewer->GetCodeEditor();
		if (!TestTrue(TEXT("the code editor exists"), Editor.IsValid())) { return; }
		TestTrue(TEXT("read-only by default"), Editor->IsReadOnly());

		Viewer->SetEditing(true);
		TestFalse(TEXT("editable with Edit on"), Editor->IsReadOnly());
		const FString Edited = TEXT("void ABP::BeginPlay() { /* edited */ }");
		Editor->SetText(FText::FromString(Edited));
		TestEqual(TEXT("Copy copies the edited text"), Viewer->GetCopyText(), Edited);

		Viewer->SelectTab(TEXT("h"));
		TestEqual(TEXT("the .h tab shows the declaration"), EditorText(), FString(TEXT("virtual void BeginPlay() override;")));
		Viewer->SelectTab(TEXT("json"));
		TestTrue(TEXT("the JSON tab stays read-only"), Editor->IsReadOnly());
		Viewer->SelectTab(TEXT("cpp"));
		TestEqual(TEXT("back on the implementation, the edit is kept"), EditorText(), Edited);
		TestFalse(TEXT("and editable again"), Editor->IsReadOnly());

		Viewer->SelectEntry(1);
		TestEqual(TEXT("another entry shows its own code"), EditorText(), FString(TEXT("void ABP::Helper() {}")));
		Viewer->SelectEntry(0);
		TestEqual(TEXT("the first entry's edit is kept"), EditorText(), Edited);

		Viewer->SetEditing(false);
		TestTrue(TEXT("read-only again"), Editor->IsReadOnly());
		TestEqual(TEXT("the edit is still shown"), EditorText(), Edited);
		TestEqual(TEXT("and Copy copies it"), Viewer->GetCopyText(), Edited);

		Viewer->LoadRunManifest(MakeRun(Run.Path), TEXT("11111111-1111-1111-1111-111111111111"), TEXT("EventGraph"), FString());
		TestEqual(TEXT("a new load drops the working copies"), EditorText(), FString(TEXT("void ABP::BeginPlay() { Helper(); }")));
		TestFalse(TEXT("and Edit"), Viewer->IsEditing());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
