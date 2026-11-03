// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestMaterialFactory.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "IMaterialEditor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Materials/MaterialFunction.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"

namespace N2CMaterialWorkingCopySpec
{
	const TCHAR* const BasicShape = TEXT("/Engine/BasicShapes/BasicShapeMaterial");
	const TCHAR* const DepthFade = TEXT("/Engine/Functions/Engine_MaterialFunctions01/Opacity/CameraDepthFade");

	FString FirstText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}

	/** Reads the focused Material Editor (no materialPath) or, with Path, the saved asset */
	FMcpToolCallResult Read(const FString& Path = FString())
	{
		const TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		if (!Path.IsEmpty())
		{
			Arguments->SetStringField(TEXT("materialPath"), Path);
		}
		return N2CTest::RunTool(TEXT("get-material-n2cjson"), Arguments);
	}

	/** "short=guid" for every node of every graph, in document order */
	TArray<FString> NodeIds(const TSharedPtr<FJsonObject>& Document)
	{
		TArray<FString> Ids;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!Document.IsValid() || !Document->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			return Ids;
		}
		for (const TSharedPtr<FJsonValue>& Graph : *Graphs)
		{
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (!Graph->AsObject()->TryGetArrayField(TEXT("nodes"), Nodes))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Node : *Nodes)
			{
				const TSharedPtr<FJsonObject>* IdsObject = nullptr;
				FString Short;
				FString Guid;
				if (Node->AsObject()->TryGetObjectField(TEXT("ids"), IdsObject))
				{
					(*IdsObject)->TryGetStringField(TEXT("short"), Short);
					(*IdsObject)->TryGetStringField(TEXT("guid"), Guid);
				}
				Ids.Add(Short + TEXT("=") + Guid);
			}
		}
		return Ids;
	}

	TSharedPtr<FJsonObject> TargetOf(const TSharedPtr<FJsonObject>& Document)
	{
		const TSharedPtr<FJsonObject>* Target = nullptr;
		return Document.IsValid() && Document->TryGetObjectField(TEXT("target"), Target) ? *Target : nullptr;
	}

	FString NameOf(const TSharedPtr<FJsonObject>& Document)
	{
		const TSharedPtr<FJsonObject>* Metadata = nullptr;
		FString Name;
		if (Document.IsValid() && Document->TryGetObjectField(TEXT("metadata"), Metadata))
		{
			(*Metadata)->TryGetStringField(TEXT("name"), Name);
		}
		return Name;
	}

	/** A log line flushed at once, so a session that stops inside a step shows which one */
	void Step(const TCHAR* What)
	{
		UE_LOG(LogTemp, Display, TEXT("MaterialWorkingCopy spec: %s"), What);
		GLog->Flush();
	}

	/** The asset tab of the Material Editor open on Asset */
	TSharedPtr<SDockTab> AssetTabOf(const UObject* Asset)
	{
		const TSharedPtr<IMaterialEditor> Editor = FN2CBlueprintEditorTracker::GetMaterialEditorFor(Asset);
		const TSharedPtr<FTabManager> TabManager = Editor.IsValid() ? Editor->GetTabManager() : nullptr;
		return TabManager.IsValid() ? TabManager->GetOwnerTab() : nullptr;
	}
}

/**
 * Card 0463: get-material-n2cjson with no materialPath reads the working copy of the Material Editor activated last
 * (FN2CBlueprintEditorTracker's last-activation rule, apart from the Blueprint focus): resolvedBy focus, workingCopy
 * true, the asset's own path, node ids equal to the saved asset's for unchanged expressions, and an unapplied edit
 * seen only there, never by a read by path. NO_FOCUSED_MATERIAL when no Material Editor is open. Material Editors
 * open headless only with -RenderOffScreen on UE 5.6 and 5.7 (gate 0458); build.ps1 -Test passes it (card 0509).
 */
BEGIN_DEFINE_SPEC(FN2CMaterialWorkingCopySpec, "NodeToCode.MCP.MaterialWorkingCopy", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CMaterialWorkingCopySpec)

void FN2CMaterialWorkingCopySpec::Define()
{
	using namespace N2CMaterialWorkingCopySpec;

	It(TEXT("answers NO_FOCUSED_MATERIAL when no Material Editor is open"), [this]()
	{
		if (FN2CBlueprintEditorTracker::GetOpenMaterialEditorAssets().Num() > 0)
		{
			AddInfo(TEXT("A Material Editor is open in this session: the no-editor case is not checked"));
			return;
		}
		TestNull(TEXT("no focused Material Editor"), FN2CBlueprintEditorTracker::Get().GetFocusedMaterialAsset());
		const FMcpToolCallResult Result = Read();
		const FString Text = FirstText(Result);
		TestTrue(TEXT("NO_FOCUSED_MATERIAL: ") + Text, Result.bIsError && Text.StartsWith(TEXT("NO_FOCUSED_MATERIAL")));
	});

	It(TEXT("reads the last activated Material Editor's working copy, with its unapplied edits, and never by path"), [this]()
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		UMaterial* Basic = LoadObject<UMaterial>(nullptr, *(FString(BasicShape) + TEXT(".BasicShapeMaterial")));
		// The second editor: a material built in code (two expressions), in its own package under /Game
		N2CTest::FTestMaterial Other(TEXT("M_N2CTest_FocusOther"));
		UMaterial* Grid = Other.GetMaterial();
		if (Grid)
		{
			UMaterialExpressionConstant* Half = Other.Add<UMaterialExpressionConstant>();
			UMaterialExpressionMultiply* Multiply = Other.Add<UMaterialExpressionMultiply>(nullptr, 200, 0);
			if (Half && Multiply)
			{
				Half->R = 0.5f;
				Other.Connect(Half, 0, Multiply, TEXT("A"));
			}
		}
		if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors) || !TestNotNull(TEXT("BasicShapeMaterial"), Basic)
			|| !TestNotNull(TEXT("the second material"), Grid))
		{
			return;
		}
		FN2CBlueprintEditorTracker& Tracker = FN2CBlueprintEditorTracker::Get();
		const TSharedPtr<FBlueprintEditor> BlueprintFocusBefore = Tracker.GetFocusedEditor();
		UMaterialExpressionConstant* Added = nullptr;
		UMaterial* Preview = nullptr;
		ON_SCOPE_EXIT
		{
			if (Preview && Added)
			{
				Preview->GetExpressionCollection().RemoveExpression(Added);
			}
			AssetEditors->CloseAllEditorsForAsset(Basic);
			AssetEditors->CloseAllEditorsForAsset(Grid);
			Step(TEXT("closed"));
		};

		// The saved asset first, with no editor open
		const TSharedPtr<FJsonObject> Saved = N2CTest::ResultJson(Read(BasicShape));
		if (!TestTrue(TEXT("BasicShapeMaterial reads by path"), Saved.IsValid()))
		{
			return;
		}

		Step(TEXT("open BasicShapeMaterial"));
		TestTrue(TEXT("BasicShapeMaterial opens"), AssetEditors->OpenEditorForAsset(Basic));
		Step(TEXT("open the second material"));
		TestTrue(TEXT("the second material opens"), AssetEditors->OpenEditorForAsset(Grid));
		Step(TEXT("both open"));
		TestTrue(TEXT("both are Material Editors"), FN2CBlueprintEditorTracker::GetMaterialEditorFor(Basic).IsValid()
			&& FN2CBlueprintEditorTracker::GetMaterialEditorFor(Grid).IsValid());
		TestTrue(TEXT("the one opened last is focused"), Tracker.GetFocusedMaterialAsset() == Grid);

		// Bringing BasicShapeMaterial's asset tab to the front focuses it; the Blueprint focus does not move
		const TSharedPtr<SDockTab> BasicTab = AssetTabOf(Basic);
		Step(TEXT("activate BasicShapeMaterial's tab"));
		if (TestTrue(TEXT("BasicShapeMaterial's asset tab"), BasicTab.IsValid()))
		{
			TestFalse(TEXT("a Material Editor's tab is no Blueprint editor"), Tracker.HandleTabActivated(BasicTab).IsValid());
			TestTrue(TEXT("FindMaterialAssetForTab"), FN2CBlueprintEditorTracker::FindMaterialAssetForTab(BasicTab) == Basic);
		}
		TestTrue(TEXT("the activated Material Editor is focused"), Tracker.GetFocusedMaterialAsset() == Basic);
		TestTrue(TEXT("the Blueprint focus is unchanged"), Tracker.GetFocusedEditor() == BlueprintFocusBefore);

		// Unchanged: the working copy's node ids are the saved asset's
		Step(TEXT("focus read"));
		const FMcpToolCallResult FocusResult = Read();
		TestTrue(TEXT("marked as untrusted"), N2CTest::IsMarkedUntrusted(FocusResult));
		const TSharedPtr<FJsonObject> Focused = N2CTest::ResultJson(FocusResult);
		const TSharedPtr<FJsonObject> Target = TargetOf(Focused);
		if (!TestTrue(TEXT("a focused read: ") + FirstText(FocusResult).Left(300), !FocusResult.bIsError && Target.IsValid()))
		{
			return;
		}
		TestEqual(TEXT("target.resolvedBy"), Target->GetStringField(TEXT("resolvedBy")), FString(TEXT("focus")));
		TestTrue(TEXT("target.workingCopy"), Target->GetBoolField(TEXT("workingCopy")));
		TestEqual(TEXT("target.assetPath is the asset's"), Target->GetStringField(TEXT("assetPath")), Basic->GetPathName());
		TestEqual(TEXT("target.kind"), Target->GetStringField(TEXT("kind")), FString(TEXT("material")));
		const FString WorkingCopyPath = Target->GetStringField(TEXT("workingCopyPath"));
		TestTrue(TEXT("target.workingCopyPath names the transient copy: ") + WorkingCopyPath,
			WorkingCopyPath.StartsWith(TEXT("/Engine/Transient.")) && WorkingCopyPath != Basic->GetPathName());
		TestEqual(TEXT("the document is named after the asset"), NameOf(Focused), NameOf(Saved));
		const TArray<FString> SavedIds = NodeIds(Saved);
		TestTrue(TEXT("node ids"), SavedIds.Num() > 0);
		TestEqual(TEXT("node ids match the saved asset's"), FString::Join(NodeIds(Focused), TEXT(",")), FString::Join(SavedIds, TEXT(",")));

		// An unapplied edit: an expression added to the working copy only
		const TSharedPtr<IMaterialEditor> Editor = FN2CBlueprintEditorTracker::GetMaterialEditorFor(Basic);
		Preview = Editor.IsValid() ? Cast<UMaterial>(FN2CBlueprintEditorTracker::WorkingCopyOf(*Editor)) : nullptr;
		if (!TestTrue(TEXT("the working copy is not the asset"), Preview && Preview != Basic))
		{
			return;
		}
		Added = NewObject<UMaterialExpressionConstant>(Preview, NAME_None, RF_Transactional);
		Added->R = 0.4635f;
		Added->Material = Preview;
		Added->UpdateMaterialExpressionGuid(true, false);
		Preview->GetExpressionCollection().AddExpression(Added);

		Step(TEXT("read after the edit"));
		const TSharedPtr<FJsonObject> Edited = N2CTest::ResultJson(Read());
		const TArray<FString> EditedIds = NodeIds(Edited);
		TestEqual(TEXT("the focused read sees the added expression"), EditedIds.Num(), SavedIds.Num() + 1);
		TestTrue(TEXT("with its guid"), FString::Join(EditedIds, TEXT(",")).Contains(Added->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens)));
		const TSharedPtr<FJsonObject> ByPath = N2CTest::ResultJson(Read(BasicShape));
		TestEqual(TEXT("a read by path does not see it"), FString::Join(NodeIds(ByPath), TEXT(",")), FString::Join(SavedIds, TEXT(",")));
		const TSharedPtr<FJsonObject> PathTarget = TargetOf(ByPath);
		TestTrue(TEXT("a read by path is the saved asset"), PathTarget.IsValid() && !PathTarget->GetBoolField(TEXT("workingCopy"))
			&& PathTarget->GetStringField(TEXT("resolvedBy")) == TEXT("path") && !PathTarget->HasField(TEXT("workingCopyPath")));

		// Closing the focused editor falls back to the other one still open
		Preview->GetExpressionCollection().RemoveExpression(Added);
		Added = nullptr;
		Step(TEXT("close BasicShapeMaterial"));
		AssetEditors->CloseAllEditorsForAsset(Basic);
		Preview = nullptr;
		TestTrue(TEXT("after closing it, the other Material Editor is focused"), Tracker.GetFocusedMaterialAsset() == Grid);
	});

	It(TEXT("reads a Material Function editor's working copy as the function"), [this]()
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		UMaterialFunction* Function = LoadObject<UMaterialFunction>(nullptr, *(FString(DepthFade) + TEXT(".CameraDepthFade")));
		if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors) || !TestNotNull(TEXT("CameraDepthFade"), Function))
		{
			return;
		}
		ON_SCOPE_EXIT { AssetEditors->CloseAllEditorsForAsset(Function); };

		const TSharedPtr<FJsonObject> Saved = N2CTest::ResultJson(Read(DepthFade));
		Step(TEXT("open CameraDepthFade"));
		TestTrue(TEXT("CameraDepthFade opens"), AssetEditors->OpenEditorForAsset(Function));
		TestTrue(TEXT("it is focused"), FN2CBlueprintEditorTracker::Get().GetFocusedMaterialAsset() == Function);
		const TSharedPtr<IMaterialEditor> Editor = FN2CBlueprintEditorTracker::GetMaterialEditorFor(Function);
		UObject* WorkingCopy = Editor.IsValid() ? FN2CBlueprintEditorTracker::WorkingCopyOf(*Editor) : nullptr;
		TestTrue(TEXT("the working copy is a duplicate function"), WorkingCopy && WorkingCopy != Function && WorkingCopy->IsA<UMaterialFunction>());

		const FMcpToolCallResult Result = Read();
		const TSharedPtr<FJsonObject> Focused = N2CTest::ResultJson(Result);
		const TSharedPtr<FJsonObject> Target = TargetOf(Focused);
		if (!TestTrue(TEXT("a focused read: ") + FirstText(Result).Left(300), !Result.bIsError && Target.IsValid()))
		{
			return;
		}
		TestEqual(TEXT("target.kind"), Target->GetStringField(TEXT("kind")), FString(TEXT("material-function")));
		TestEqual(TEXT("target.assetPath"), Target->GetStringField(TEXT("assetPath")), Function->GetPathName());
		TestTrue(TEXT("target.workingCopy"), Target->GetBoolField(TEXT("workingCopy")));
		TestEqual(TEXT("named after the function"), NameOf(Focused), NameOf(Saved));
		TestEqual(TEXT("node ids match the saved function's"), FString::Join(NodeIds(Focused), TEXT(",")), FString::Join(NodeIds(Saved), TEXT(",")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
