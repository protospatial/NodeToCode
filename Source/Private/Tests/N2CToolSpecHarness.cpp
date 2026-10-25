// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/N2CToolSpecHarness.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Components/ActorComponent.h"
#include "Engine/TimelineTemplate.h"
#include "K2Node_EditablePinBase.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

namespace N2CTest
{
	namespace ToolSpecPrivate
	{
		FString PinTypeText(const FEdGraphPinType& Type)
		{
			return FString::Printf(TEXT("%s/%s/%s/c%d/r%d/k%d"), *Type.PinCategory.ToString(), *Type.PinSubCategory.ToString(),
				Type.PinSubCategoryObject.IsValid() ? *Type.PinSubCategoryObject->GetPathName() : TEXT(""),
				static_cast<int32>(Type.ContainerType), Type.bIsReference ? 1 : 0, Type.bIsConst ? 1 : 0);
		}

		FString PinLabel(const UEdGraphPin* Pin)
		{
			if (!Pin)
			{
				return TEXT("<null>");
			}
			const UEdGraphNode* Owner = Pin->GetOwningNodeUnchecked();
			return FString::Printf(TEXT("%s.%s"), Owner ? *Owner->NodeGuid.ToString() : TEXT("<no node>"), *Pin->PinName.ToString());
		}

		TArray<UEdGraph*> GraphsOf(UBlueprint* Blueprint)
		{
			TArray<UEdGraph*> Graphs;
			if (Blueprint)
			{
				Blueprint->GetAllGraphs(Graphs);
			}
			return Graphs;
		}
	}

	FMcpToolCallResult RunTool(const FString& Name, const TSharedPtr<FJsonObject>& Args)
	{
		FN2CMcpToolManager& Manager = FN2CMcpToolManager::Get();
		bool bRegisteredHere = false;
		if (!Manager.IsToolRegistered(Name))
		{
			const TSharedPtr<IN2CMcpTool> Tool = FindRegisteredTool(Name);
			if (!Tool.IsValid())
			{
				return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("N2CTest: no registered tool '%s'"), *Name));
			}
			bRegisteredHere = Manager.RegisterTool(Tool);
		}
		FMcpToolCallResult Result = Manager.ExecuteTool(Name, Args.IsValid() ? Args : MakeShared<FJsonObject>());
		if (bRegisteredHere)
		{
			Manager.UnregisterTool(Name);
		}
		return Result;
	}

	FMcpToolCallResult RunToolUnmarked(const FString& Name, const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<IN2CMcpTool> Tool = FindRegisteredTool(Name);
		return Tool.IsValid() ? Tool->Execute(Args)
			: FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("N2CTest: no registered tool '%s'"), *Name));
	}

	TSharedPtr<FJsonObject> TargetArgs(const UBlueprint* Blueprint, const UEdGraph* Graph)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Args->SetStringField(TEXT("graphGuid"), Graph->GraphGuid.ToString());
		return Args;
	}

	TSharedPtr<FJsonValue> PinRef(const UEdGraphPin* Pin)
	{
		const TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
		Ref->SetStringField(TEXT("nodeGuid"), Pin->GetOwningNode()->NodeGuid.ToString());
		Ref->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
		Ref->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
		return MakeShared<FJsonValueObject>(Ref);
	}

	TSharedPtr<FJsonObject> ConnectArgs(const TArray<TPair<UEdGraphPin*, UEdGraphPin*>>& Links, const TSharedPtr<FJsonObject>& Base)
	{
		TArray<TSharedPtr<FJsonValue>> Connections;
		for (const TPair<UEdGraphPin*, UEdGraphPin*>& Link : Links)
		{
			const TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
			Connection->SetField(TEXT("from"), PinRef(Link.Key));
			Connection->SetField(TEXT("to"), PinRef(Link.Value));
			Connections.Add(MakeShared<FJsonValueObject>(Connection));
		}
		const TSharedPtr<FJsonObject> Args = Base.IsValid() ? Base : MakeShared<FJsonObject>();
		Args->SetArrayField(TEXT("connections"), Connections);
		return Args;
	}

	TSharedPtr<FJsonObject> ResultJson(const FMcpToolCallResult& Result)
	{
		FString Text;
		return Result.Content.Num() > 0 && Result.Content[0].IsValid() && Result.Content[0]->TryGetStringField(TEXT("text"), Text)
			? ParseJsonObject(Text) : nullptr;
	}

	namespace ToolSpecPrivate
	{
		TSharedPtr<FJsonObject> FirstObjectField(const FMcpToolCallResult& Result, const TCHAR* Field)
		{
			for (const TSharedPtr<FJsonObject>& Item : Result.Content)
			{
				FString Text;
				if (!Item.IsValid() || !Item->TryGetStringField(TEXT("text"), Text))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Parsed = ParseJsonObject(Text);
				const TSharedPtr<FJsonObject>* Found = nullptr;
				if (Parsed.IsValid() && Parsed->TryGetObjectField(Field, Found))
				{
					return *Found;
				}
			}
			return nullptr;
		}
	}

	TSharedPtr<FJsonObject> ResultTarget(const FMcpToolCallResult& Result)
	{
		return ToolSpecPrivate::FirstObjectField(Result, TEXT("target"));
	}

	TSharedPtr<FJsonObject> ResultCompile(const FMcpToolCallResult& Result)
	{
		return ToolSpecPrivate::FirstObjectField(Result, TEXT("compile"));
	}

	bool IsMarkedUntrusted(const FMcpToolCallResult& Result)
	{
		return ResultText(Result).TrimStart().StartsWith(FString::Printf(TEXT("{\"%s\""), FN2CMcpUntrustedContent::MarkerField));
	}

	bool ExpectResultTargets(FAutomationTestBase& Test, const FString& What, const FMcpToolCallResult& Result,
		const UBlueprint* Blueprint, const UEdGraph* Graph)
	{
		const TSharedPtr<FJsonObject> Target = ResultTarget(Result);
		if (!Test.TestTrue(What + TEXT(": the result names its target: ") + ResultText(Result).Left(300), Target.IsValid()))
		{
			return false;
		}
		bool bOk = Test.TestEqual(What + TEXT(": target.blueprintPath"), Target->GetStringField(TEXT("blueprintPath")), Blueprint->GetPathName());
		if (Graph)
		{
			bOk &= Test.TestEqual(What + TEXT(": target.graphGuid"), Target->GetStringField(TEXT("graphGuid")), Graph->GraphGuid.ToString());
		}
		const TSharedPtr<FJsonObject> Compile = ResultCompile(Result);
		FString CompiledPath;
		if (Compile.IsValid() && Compile->TryGetStringField(TEXT("blueprintPath"), CompiledPath))
		{
			bOk &= Test.TestEqual(What + TEXT(": compile.blueprintPath"), CompiledPath, Blueprint->GetPathName());
		}
		return bOk;
	}

	bool ExpectToolCompiled(FAutomationTestBase& Test, const FString& What, const FMcpToolCallResult& Result)
	{
		if (!Test.TestFalse(What + TEXT(" succeeds: ") + ResultText(Result).Left(400), Result.bIsError))
		{
			return false;
		}
		const TSharedPtr<FJsonObject> Compile = ResultCompile(Result);
		if (!Test.TestTrue(What + TEXT(": the result carries the compile"), Compile.IsValid()))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
		if (Compile->TryGetArrayField(TEXT("messages"), Messages))
		{
			for (const TSharedPtr<FJsonValue>& Message : *Messages)
			{
				const TSharedPtr<FJsonObject> Object = Message.IsValid() ? Message->AsObject() : nullptr;
				if (Object.IsValid())
				{
					Test.AddInfo(What + TEXT(": compiler: ") + Object->GetStringField(TEXT("message")));
				}
			}
		}
		const bool bSuccess = Test.TestTrue(What + TEXT(": compile success"), Compile->GetBoolField(TEXT("success")));
		return Test.TestEqual(What + TEXT(": compile errorCount"), static_cast<int32>(Compile->GetNumberField(TEXT("errorCount"))), 0) && bSuccess;
	}

	FString Snapshot(UBlueprint* Blueprint)
	{
		using namespace ToolSpecPrivate;
		TArray<FString> Lines;
		if (!Blueprint)
		{
			return FString();
		}
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			Lines.Add(FString::Printf(TEXT("variable %s %s flags=%llu"), *Variable.VarName.ToString(), *PinTypeText(Variable.VarType),
				static_cast<unsigned long long>(Variable.PropertyFlags)));
		}
		for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
		{
			Lines.Add(FString::Printf(TEXT("timeline %s"), Timeline ? *Timeline->GetName() : TEXT("<null>")));
		}
		for (const UActorComponent* Component : Blueprint->ComponentTemplates)
		{
			Lines.Add(FString::Printf(TEXT("component template %s"), Component ? *Component->GetName() : TEXT("<null>")));
		}
		for (const UEdGraph* Graph : GraphsOf(Blueprint))
		{
			Lines.Add(FString::Printf(TEXT("graph %s %s nodes=%d"), *Graph->GetName(), *Graph->GraphGuid.ToString(), Graph->Nodes.Num()));
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					Lines.Add(TEXT("  node <null>"));
					continue;
				}
				Lines.Add(FString::Printf(TEXT("  node %s %s %s"), *Node->NodeGuid.ToString(), *Node->GetClass()->GetName(), *Node->GetName()));
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin)
					{
						Lines.Add(TEXT("    pin <null>"));
						continue;
					}
					TArray<FString> Linked;
					for (const UEdGraphPin* Other : Pin->LinkedTo)
					{
						Linked.Add(PinLabel(Other));
					}
					Linked.Sort();
					Lines.Add(FString::Printf(TEXT("    pin %s %s %s %s default='%s'|%s|'%s' orphaned=%d hidden=%d links=[%s]"),
						*Pin->PinId.ToString(), *Pin->PinName.ToString(), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
						*PinTypeText(Pin->PinType), *Pin->DefaultValue, Pin->DefaultObject ? *Pin->DefaultObject->GetPathName() : TEXT(""),
						*Pin->DefaultTextValue.ToString(), Pin->bOrphanedPin ? 1 : 0, Pin->bHidden ? 1 : 0, *FString::Join(Linked, TEXT(", "))));
				}
				if (const UK2Node_EditablePinBase* Editable = Cast<UK2Node_EditablePinBase>(Node))
				{
					for (const TSharedPtr<FUserPinInfo>& Info : Editable->UserDefinedPins)
					{
						Lines.Add(Info.IsValid()
							? FString::Printf(TEXT("    user pin %s %s %s default='%s'"), *Info->PinName.ToString(),
								Info->DesiredPinDirection == EGPD_Input ? TEXT("in") : TEXT("out"), *PinTypeText(Info->PinType), *Info->PinDefaultValue)
							: FString(TEXT("    user pin <null>")));
					}
				}
			}
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FString FirstSnapshotDifference(const FString& Expected, const FString& Actual)
	{
		if (Expected == Actual)
		{
			return FString();
		}
		TArray<FString> A;
		TArray<FString> B;
		Expected.ParseIntoArrayLines(A, false);
		Actual.ParseIntoArrayLines(B, false);
		for (int32 Index = 0; Index < FMath::Max(A.Num(), B.Num()); ++Index)
		{
			const FString Left = A.IsValidIndex(Index) ? A[Index] : FString(TEXT("<end>"));
			const FString Right = B.IsValidIndex(Index) ? B[Index] : FString(TEXT("<end>"));
			if (Left != Right)
			{
				return FString::Printf(TEXT("line %d: expected [%s], got [%s]"), Index + 1, *Left.TrimStart(), *Right.TrimStart());
			}
		}
		return TEXT("the snapshots differ");
	}

	TSet<FString> AllLinks(UBlueprint* Blueprint)
	{
		TSet<FString> Found;
		for (const UEdGraph* Graph : ToolSpecPrivate::GraphsOf(Blueprint))
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				for (const UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
				{
					if (Pin && Pin->Direction == EGPD_Output)
					{
						for (const UEdGraphPin* Other : Pin->LinkedTo)
						{
							Found.Add(FString::Printf(TEXT("%s.%s -> %s.%s"), *Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
								*Other->GetOwningNode()->NodeGuid.ToString(), *Other->PinName.ToString()));
						}
					}
				}
			}
		}
		return Found;
	}

	TArray<FString> AsymmetricLinks(UBlueprint* Blueprint)
	{
		using namespace ToolSpecPrivate;
		TArray<FString> Problems;
		for (const UEdGraph* Graph : GraphsOf(Blueprint))
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				for (const UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
				{
					for (const UEdGraphPin* Other : Pin ? Pin->LinkedTo : TArray<UEdGraphPin*>())
					{
						if (!Other)
						{
							Problems.Add(FString::Printf(TEXT("%s links to a null pin"), *PinLabel(Pin)));
							continue;
						}
						const UEdGraphNode* OtherNode = Other->GetOwningNodeUnchecked();
						const UEdGraph* OtherGraph = OtherNode ? OtherNode->GetGraph() : nullptr;
						if (!OtherNode || !OtherGraph || !OtherGraph->Nodes.Contains(OtherNode))
						{
							Problems.Add(FString::Printf(TEXT("%s links to %s, whose node is not in a graph"), *PinLabel(Pin), *PinLabel(Other)));
						}
						else if (!Other->LinkedTo.Contains(Pin))
						{
							Problems.Add(FString::Printf(TEXT("%s links to %s, which does not link back"), *PinLabel(Pin), *PinLabel(Other)));
						}
					}
				}
			}
		}
		return Problems;
	}

	bool ExpectSymmetricLinks(FAutomationTestBase& Test, const FString& What, UBlueprint* Blueprint)
	{
		TArray<FString> Problems = AsymmetricLinks(Blueprint);
		if (Problems.Num() > 3)
		{
			Problems.SetNum(3);
			Problems.Add(TEXT("..."));
		}
		return Test.TestTrue(What + TEXT(": every pin link is symmetric") + (Problems.Num() ? TEXT(": ") + FString::Join(Problems, TEXT("; ")) : FString()),
			Problems.Num() == 0);
	}

	bool ExpectUndoRedoExact(FAutomationTestBase& Test, const FString& What, UBlueprint* Blueprint, const FString& Before)
	{
		if (!Test.TestTrue(What + TEXT(": an editor transaction buffer"), GEditor && GEditor->Trans))
		{
			return false;
		}
		const FString After = Snapshot(Blueprint);
		if (!Test.TestTrue(What + TEXT(": undo runs"), GEditor->UndoTransaction()))
		{
			return false;
		}
		const FString Undone = FirstSnapshotDifference(Before, Snapshot(Blueprint));
		bool bOk = Test.TestTrue(What + TEXT(": undo restores Nodes, Pins and UserDefinedPins exactly") + (Undone.IsEmpty() ? FString() : TEXT(" (") + Undone + TEXT(")")),
			Undone.IsEmpty());
		bOk &= ExpectSymmetricLinks(Test, What + TEXT(" after undo"), Blueprint);
		if (!Test.TestTrue(What + TEXT(": redo runs"), GEditor->RedoTransaction()))
		{
			return false;
		}
		const FString Redone = FirstSnapshotDifference(After, Snapshot(Blueprint));
		bOk &= Test.TestTrue(What + TEXT(": redo restores the edit exactly") + (Redone.IsEmpty() ? FString() : TEXT(" (") + Redone + TEXT(")")),
			Redone.IsEmpty());
		bOk &= ExpectSymmetricLinks(Test, What + TEXT(" after redo"), Blueprint);
		return bOk;
	}

	bool ExpectCompilesClean(FAutomationTestBase& Test, const FString& What, UBlueprint* Blueprint)
	{
		FN2CCompileDiagnostics Diagnostics;
		FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);
		for (const FN2CCompilerMessage& Message : Diagnostics.Messages)
		{
			Test.AddInfo(What + TEXT(": compiler: ") + Message.Message);
		}
		return Test.TestEqual(What + TEXT(": compile errors"), Diagnostics.ErrorCount, 0);
	}

	UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FString& GuidText)
	{
		FGuid Guid;
		if (!FGuid::Parse(GuidText, Guid))
		{
			return nullptr;
		}
		for (const UEdGraph* Graph : ToolSpecPrivate::GraphsOf(Blueprint))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == Guid)
				{
					return Node;
				}
			}
		}
		return nullptr;
	}

	bool HasUserPin(const UK2Node_EditablePinBase* Node, const FName Name)
	{
		return Node && Node->UserDefinedPins.ContainsByPredicate(
			[Name](const TSharedPtr<FUserPinInfo>& Info) { return Info.IsValid() && Info->PinName == Name; });
	}

	UEdGraphPin* ExecIn(UEdGraphNode* Node)
	{
		return Node ? Node->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
	}

	UEdGraphPin* ExecOut(UEdGraphNode* Node)
	{
		return Node ? Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output) : nullptr;
	}

	int32 UndoQueueLength()
	{
		return GEditor && GEditor->Trans ? GEditor->Trans->GetQueueLength() : 0;
	}

	FCompileCounter::FCompileCounter(UBlueprint* InBlueprint)
		: Blueprint(InBlueprint)
	{
		Handle = Blueprint->OnCompiled().AddLambda([this](UBlueprint*) { ++Count; });
	}

	FCompileCounter::~FCompileCounter()
	{
		Blueprint->OnCompiled().Remove(Handle);
	}

	FToolSpecBlueprint::FToolSpecBlueprint(const FString& BlueprintName, const FString& MountRoot, bool bInTrackDirty)
		: FTestBlueprint(BlueprintName, MountRoot)
		, bTrackDirty(bInTrackDirty)
	{
		if (bTrackDirty && GetPackage())
		{
			GetPackage()->ClearFlags(RF_Transient);
		}
	}

	FToolSpecBlueprint::~FToolSpecBlueprint()
	{
		CloseEditor();
		if (UPackage* OwnPackage = GetPackage())
		{
			OwnPackage->SetDirtyFlag(false);
			if (bTrackDirty)
			{
				OwnPackage->SetFlags(RF_Transient);
			}
		}
		// The undo buffer holds the test Blueprint's objects: drop them so the Blueprint can be collected
		if (GEditor && GEditor->Trans)
		{
			GEditor->ResetTransaction(NSLOCTEXT("NodeToCode", "N2CToolSpecReset", "NodeToCode tool specs"));
		}
	}

	FString FToolSpecBlueprint::Path() const
	{
		return GetBlueprint() ? GetBlueprint()->GetPathName() : FString();
	}

	bool FToolSpecBlueprint::IsDirty() const
	{
		return GetPackage() && GetPackage()->IsDirty();
	}

	void FToolSpecBlueprint::Settle()
	{
		if (GetBlueprint())
		{
			FKismetEditorUtilities::CompileBlueprint(GetBlueprint(), EBlueprintCompileOptions::SkipGarbageCollection);
		}
		if (GetPackage())
		{
			GetPackage()->SetDirtyFlag(false);
		}
	}

	bool FToolSpecBlueprint::OpenInEditor(FAutomationTestBase& Test, UEdGraph* Graph, bool bClean)
	{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
		Test.AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there, so this focus-bound case does not run"));
		return false;
#else
		TSharedPtr<IBlueprintEditor> Opened;
		FString OpenError;
		if (!Test.TestTrue(TEXT("Blueprint editor opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(GetBlueprint(), Opened, OpenError)))
		{
			return false;
		}
		bEditorOpen = true;
		if (Graph)
		{
			StaticCastSharedPtr<FBlueprintEditor>(Opened)->JumpToHyperlink(Graph, false);
		}
		if (bClean && GetPackage())
		{
			GetPackage()->SetDirtyFlag(false);
		}
		return true;
#endif
	}

	void FToolSpecBlueprint::CloseEditor()
	{
		if (bEditorOpen && GEditor && GetBlueprint())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(GetBlueprint());
		}
		bEditorOpen = false;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
