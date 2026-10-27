// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CNodeCollector.h"
#include "Core/N2CCapture.h"
#include "Core/N2CCaptureContext.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/EngineTypes.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Regex.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GenericToText.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MapForEach.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_SetForEach.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetTextLibrary.h"
#include "Kismet2/StructureEditorUtils.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpGetFocusedBlueprintTool.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Models/N2CJsonContract.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/EditorStyleSettings.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Tests/Capture/N2CTestNodeClasses.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/N2CStableNames.h"
#include "Utils/Validators/N2CBlueprintValidator.h"

/**
 * Golden captures of graph shapes: each case builds a Blueprint graph in code (N2CTest::FTestBlueprint, a transient
 * Actor Blueprint in a /Game/N2CTest_<guid> package that is never saved, so capture treats it and its functions,
 * macros, structs and enums as user content, as it would a project's), runs the capture the editor runs
 * (FN2CNodeCollector::CollectNodesFromGraph, N2C::CaptureNodes, FN2CSerializer::ToJson) and
 * compares the N2C JSON (version N2CJsonContract::Version) with Goldens/Capture/<case>.json. The transient package path
 * and any GUID are replaced with <PACKAGE> and <GUID> first. Every case also survives FromJson -> ToJson unchanged.
 *
 * The goldens record today's output, known defects included: each golden lists the defects it shows under
 * "known_defects" with the finding and the E10 card that changes it, so those cards show their output changes
 * deliberately. A card that changes capture output regenerates the goldens in the same commit (N2C_UPDATE_GOLDENS=1
 * in the editor's environment, -N2CUpdateGoldens on its command line, or .\build.ps1 -Test -UpdateGoldens) and its
 * commit message explains the golden diff. <case>.<engine>.json (for example Branch.5.8.json), when present, is used
 * on that engine instead of <case>.json where the engine really differs (README "Capture goldens").
 *
 * A case may set Max Translation Depth (UN2CSettings::TranslationDepth) for its capture; the setting is put back
 * afterwards and the golden records the depth it ran with.
 *
 * "Isolation" checks that captures share no state (card 0133): a capture started in the middle of another, and a
 * capture made after another, leave each result equal to its own golden.
 *
 * "Identity" checks the capture's IDs (card 0134): a pasted node's pins get IDs of their own and the capture validates,
 * a function that calls itself is captured once, same-named functions of two Blueprints are both captured with their
 * owner, an unchanged graph captured twice gets the same IDs, and the ID-to-GUID maps (and get-focused-blueprint's
 * result) have one entry per node and per pin.
 *
 * "DataFlows" checks the lossless data edges (card 0135, PR #7): in every case each connected data input is the target of
 * exactly one flows.data entry, and a fan-out (directly or through a knot) lists every target. "V1Reader" checks that
 * the N2C JSON 1.x documents in Fixtures/Capture/V1 (earlier 2.0 goldens and a 1.2.3 save) read into the v2 model.
 *
 * "ExecFlows" checks the pin-to-pin exec edges (card 0136): in every case each exec link is one "N3.P5->N4.P1" entry
 * from an exec output pin to an exec input pin, and Branch, Sequence, ForEachLoop, Switch, Gate and Timeline name the
 * pin on each end; the validator checks both pins, and node-only entries (1.x) still read.
 *
 * "Liveness" checks the nodes Blueprint never runs (card 0137, issue #13): ghost default events are not captured,
 * disabled nodes are marked and exec passes through them as the compiler does, pure nodes nothing live reads are
 * "unused" and impure nodes no entry reaches are "unreachable"; no other case marks a node.
 *
 * "StableNames" checks the identifiers (card 0138, gap-5): every node has its K2Node "class" and no "name", a title or
 * label only when it says more than the identifier, member names as declared (Temperature_C, Convert_C), member_parent
 * the declaring Blueprint, exec pins named execute and then; and every case captures the same JSON with the editor in
 * Japanese, and the same identifiers with the friendly-names preference flipped (only the hints may change).
 *
 * "Discovery" and the cases ConstructionScript, MacroGraph_Depth1, NestedGraphs_Depth0/1, PluginContent_Depth1 and
 * CommentBoxes check graph kinds and nested graphs (card 0235): graph_type comes from the schema (Construction,
 * Macro, Animation), a collapsed graph is captured at every depth and its Composite node names it, a function and a
 * user struct in plugin content are followed, engine content never is, comment boxes list the nodes inside them, and
 * a null entry in a graph's Nodes array is skipped.
 *
 * "PinTypes" and the cases MapPins and BitmaskPin check pin types (card 0238, translation-core-13 and -31): a map pin is
 * typed by its key with its value in "value_type" and "value_sub_type", a struct or enum used only as a map value is
 * listed, Make Map is named key then value, an enum pin is Enum (not Byte) with its enum as sub_type, a bitmask int is
 * Bitmask and a Target pin Self; the subcategory is checked before the category, and the value fields round-trip.
 */
BEGIN_DEFINE_SPEC(FN2CCaptureGoldenSpec, "NodeToCode.Capture.Golden", N2C_TEST_FLAGS)

	struct FKnownDefect
	{
		FString Finding;
		FString Card;
		FString Effect;
	};

	/** One graph shape: Build makes the graph in the given Blueprint and returns the graph to capture */
	struct FCaptureCase
	{
		FString Name;
		FString Description;
		TArray<FKnownDefect> KnownDefects;
		TFunction<UEdGraph*(N2CTest::FTestBlueprint&)> Build;
		/** Max Translation Depth for the capture; unset leaves the setting as it is */
		TOptional<int32> TranslationDepth;
		/** Error log lines today's capture writes for this case (known defects); each must appear */
		TArray<FString> ExpectedErrors;
	};

	/** The capture text, normalised; empty with errors added when the graph or the capture failed */
	struct FCaptured
	{
		FString Json;
		TSharedPtr<FJsonObject> Capture;
		/** The capture's own result and the test package path, to serialize it again later */
		FN2CBlueprint Blueprint;
		FString PackagePath;
		/** The whole capture result (Blueprint and its ID-to-GUID maps) */
		FN2CCapture Result;
	};

	TArray<FCaptureCase> Cases;

	static FString GoldenDir()
	{
		return N2CTest::GoldensDir(TEXT("Capture"));
	}

	static FString EngineVersion()
	{
		return FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	}

	/** <case>.<engine>.json when it exists (a real engine difference), else <case>.json */
	static FString GoldenPath(const FString& CaseName)
	{
		const FString PerEngine = GoldenDir() / FString::Printf(TEXT("%s.%s.json"), *CaseName, *EngineVersion());
		return IFileManager::Get().FileExists(*PerEngine) ? PerEngine : GoldenDir() / CaseName + TEXT(".json");
	}

	static bool UpdateGoldens()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_UPDATE_GOLDENS")) == TEXT("1")
			|| FParse::Param(FCommandLine::Get(), TEXT("N2CUpdateGoldens"));
	}

	static FString PrettyJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Text.Replace(TEXT("\r\n"), TEXT("\n")) + TEXT("\n");
	}

	/** Replaces capture group 1 of every match of Pattern with Token (the whole match when there is no group) */
	static FString ReplaceMatches(const FString& Text, const TCHAR* Pattern, const TCHAR* Token)
	{
		const FRegexPattern Compiled(Pattern);
		FRegexMatcher Matcher(Compiled, Text);
		FString Result;
		int32 Last = 0;
		while (Matcher.FindNext())
		{
			const int32 Begin = Matcher.GetCaptureGroupBeginning(1) >= 0 ? Matcher.GetCaptureGroupBeginning(1) : Matcher.GetMatchBeginning();
			const int32 End = Matcher.GetCaptureGroupEnding(1) >= 0 ? Matcher.GetCaptureGroupEnding(1) : Matcher.GetMatchEnding();
			Result += Text.Mid(Last, Begin - Last) + Token;
			Last = End;
		}
		return Result + Text.Mid(Last);
	}

	/**
	 * Replaces what changes from run to run with stable tokens: the case's transient package path (<PACKAGE>) and any
	 * GUID (<GUID>). Nothing else may change between sessions (the UObject index FN2CVariableProcessor once put in a
	 * member_name is gone, card 0234, so a golden shows it if it comes back)
	 */
	static FString Normalise(const FString& Text, const FString& PackagePath)
	{
		const FString Out = PackagePath.IsEmpty() ? Text : Text.Replace(*PackagePath, TEXT("<PACKAGE>"), ESearchCase::CaseSensitive);
		return ReplaceMatches(Out, TEXT("[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}"), TEXT("<GUID>"));
	}

	/** Captures Case's graph; DuringCapture, when set, runs once right after the capture's first node */
	FCaptured Capture(const FCaptureCase& Case, TFunction<void()> DuringCapture = nullptr)
	{
		FCaptured Captured;
		N2CTest::FTestBlueprint Blueprint(FString::Printf(TEXT("BP_N2CGolden_%s"), *Case.Name), TEXT("/Game"));
		UEdGraph* Graph = Blueprint.GetBlueprint() ? Case.Build(Blueprint) : nullptr;
		for (const FString& Error : Blueprint.Errors())
		{
			AddError(FString::Printf(TEXT("%s: building the graph: %s"), *Case.Name, *Error));
		}
		if (!Graph || Blueprint.Errors().Num() > 0)
		{
			AddError(FString::Printf(TEXT("%s: the test graph could not be built"), *Case.Name));
			return Captured;
		}

		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		const int32 SavedDepth = Settings->TranslationDepth;
		ON_SCOPE_EXIT { Settings->TranslationDepth = SavedDepth; };
		if (Case.TranslationDepth.IsSet())
		{
			Settings->TranslationDepth = Case.TranslationDepth.GetValue();
		}
		for (const FString& Expected : Case.ExpectedErrors)
		{
			AddExpectedError(Expected, EAutomationExpectedErrorFlags::Contains, 0);
		}

		TArray<UK2Node*> Nodes;
		if (!TestTrue(Case.Name + TEXT(": CollectNodesFromGraph succeeds"), FN2CNodeCollector::Get().CollectNodesFromGraph(Graph, Nodes)))
		{
			return Captured;
		}
		if (DuringCapture)
		{
			FN2CCaptureContext::SetNodeCapturedHookForTests(MoveTemp(DuringCapture));
		}
		const TValueOrError<FN2CCapture, FString> Result = N2C::CaptureNodes(Nodes);
		FN2CCaptureContext::SetNodeCapturedHookForTests(nullptr);
		if (!TestTrue(Case.Name + TEXT(": CaptureNodes succeeds"), Result.HasValue()))
		{
			return Captured;
		}
		Captured.Blueprint = Result.GetValue().Blueprint;
		Captured.Result = Result.GetValue();
		Captured.PackagePath = Blueprint.GetPackagePath();
		Serialize(Case, Captured);
		return Captured;
	}

	/** Fills Captured's JSON from its Blueprint: ToJson, normalised and parsed */
	void Serialize(const FCaptureCase& Case, FCaptured& Captured)
	{
		const FString Raw = FN2CSerializer::ToJson(Captured.Blueprint, EN2CJsonStyle::Pretty);
		const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(Normalise(Raw, Captured.PackagePath));
		Captured.Capture.Reset();
		Captured.Json.Reset();
		if (TestTrue(Case.Name + TEXT(": ToJson output parses"), Parsed.IsValid()))
		{
			Captured.Capture = Parsed;
			Captured.Json = PrettyJson(Parsed);
		}
	}

	/** The golden file for a case: its description and known defects (from the case table) and the capture */
	static TSharedPtr<FJsonObject> GoldenObject(const FCaptureCase& Case, const TSharedPtr<FJsonObject>& Capture)
	{
		TSharedPtr<FJsonObject> Golden = MakeShared<FJsonObject>();
		Golden->SetStringField(TEXT("case"), Case.Name);
		Golden->SetStringField(TEXT("description"), Case.Description);
		if (Case.TranslationDepth.IsSet())
		{
			Golden->SetNumberField(TEXT("translation_depth"), Case.TranslationDepth.GetValue());
		}
		TArray<TSharedPtr<FJsonValue>> Defects;
		for (const FKnownDefect& Defect : Case.KnownDefects)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("finding"), Defect.Finding);
			Entry->SetStringField(TEXT("card"), Defect.Card);
			Entry->SetStringField(TEXT("effect"), Defect.Effect);
			Defects.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Golden->SetArrayField(TEXT("known_defects"), Defects);
		Golden->SetObjectField(TEXT("capture"), Capture);
		return Golden;
	}

	/** Reports the first differing line of two texts, with the line number */
	void ReportDifference(const FString& What, const FString& Expected, const FString& Actual)
	{
		TArray<FString> ExpectedLines;
		TArray<FString> ActualLines;
		Expected.ParseIntoArray(ExpectedLines, TEXT("\n"), false);
		Actual.ParseIntoArray(ActualLines, TEXT("\n"), false);
		const int32 Count = FMath::Max(ExpectedLines.Num(), ActualLines.Num());
		for (int32 Line = 0; Line < Count; ++Line)
		{
			const FString E = ExpectedLines.IsValidIndex(Line) ? ExpectedLines[Line].TrimStartAndEnd() : TEXT("(end of file)");
			const FString A = ActualLines.IsValidIndex(Line) ? ActualLines[Line].TrimStartAndEnd() : TEXT("(end of file)");
			if (E != A)
			{
				AddError(FString::Printf(TEXT("%s differs at line %d: expected `%s`, got `%s`"), *What, Line + 1, *E, *A));
				return;
			}
		}
		AddError(What + TEXT(" differs (whitespace only)"));
	}

	/** Keeps the actual output next to the scratch logs, for a diff against the golden */
	static FString SaveActual(const FString& CaseName, const FString& Suffix, const FString& Text)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/N2CCaptureGolden")
			/ CaseName + Suffix + TEXT(".json"));
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return Path;
	}

	void RunGolden(const FCaptureCase& Case)
	{
		const FCaptured Captured = Capture(Case);
		if (!Captured.Capture.IsValid())
		{
			return;
		}
		if (UpdateGoldens())
		{
			const FString Path = GoldenPath(Case.Name);
			TestTrue(TEXT("golden written: ") + Path, FFileHelper::SaveStringToFile(PrettyJson(GoldenObject(Case, Captured.Capture)),
				*Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			return;
		}
		ExpectGolden(Case, Captured, FString());
	}

	/** Compares a capture of Case with Case's golden (never writes it); Label tells apart several captures in one test */
	void ExpectGolden(const FCaptureCase& Case, const FCaptured& Captured, const FString& Label)
	{
		if (!TestTrue(Case.Name + Label + TEXT(": captured"), Captured.Capture.IsValid()))
		{
			return;
		}
		const FString Expected = PrettyJson(GoldenObject(Case, Captured.Capture));
		const FString Path = GoldenPath(Case.Name);
		FString GoldenText;
		if (!FFileHelper::LoadFileToString(GoldenText, *Path))
		{
			AddError(FString::Printf(TEXT("No golden at %s; run with N2C_UPDATE_GOLDENS=1 to write it"), *Path));
			return;
		}
		GoldenText.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		if (GoldenText != Expected)
		{
			const FString ActualPath = SaveActual(Case.Name + Label, TEXT(".actual"), Expected);
			ReportDifference(FString::Printf(TEXT("%s%s capture (golden %s, actual %s)"), *Case.Name, *Label, *Path, *ActualPath),
				GoldenText, Expected);
		}
	}

	/** The capture after FromJson then ToJson again */
	FString RoundTrip(const FCaptureCase& Case, const FCaptured& Captured)
	{
		FN2CBlueprint Reread;
		const FString Text = PrettyJson(Captured.Capture);
		if (!TestTrue(Case.Name + TEXT(": FromJson reads the ToJson output"), FN2CSerializer::FromJson(Text, Reread)))
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> Again = N2CTest::ParseJsonObject(FN2CSerializer::ToJson(Reread, EN2CJsonStyle::Pretty));
		return Again.IsValid() ? PrettyJson(Again) : FString();
	}

	void RunRoundTrip(const FCaptureCase& Case)
	{
		const FCaptured Captured = Capture(Case);
		if (!Captured.Capture.IsValid())
		{
			return;
		}
		const FString Again = RoundTrip(Case, Captured);
		if (Again != Captured.Json)
		{
			ReportDifference(Case.Name + TEXT(" ToJson -> FromJson -> ToJson"), Captured.Json, Again);
		}
	}

	/**
	 * flows.data is lossless: every target is a connected input pin of the graph and appears once, every connected data
	 * input (a pin with a "type") is a target, and every key is an output pin
	 */
	void ExpectLosslessDataFlows(const FString& What, const TSharedPtr<FJsonObject>& Capture)
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : Capture->GetArrayField(TEXT("graphs")))
		{
			const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
			const FString GraphWhat = What + TEXT(" ") + Graph->GetStringField(TEXT("name"));
			TSet<FString> ConnectedInputs;
			TSet<FString> Outputs;
			for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
			{
				const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
				const FString NodeID = Node->GetStringField(TEXT("id"));
				for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(TEXT("input_pins")))
				{
					const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
					bool bConnected = false;
					if (Pin->HasField(TEXT("type")) && Pin->TryGetBoolField(TEXT("connected"), bConnected) && bConnected)
					{
						ConnectedInputs.Add(NodeID + TEXT(".") + Pin->GetStringField(TEXT("id")));
					}
				}
				for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(TEXT("output_pins")))
				{
					Outputs.Add(NodeID + TEXT(".") + PinValue->AsObject()->GetStringField(TEXT("id")));
				}
			}

			TSet<FString> Targets;
			const TSharedPtr<FJsonObject> Data = Graph->GetObjectField(TEXT("flows"))->GetObjectField(N2CJsonContract::DataField);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Data->Values)
			{
				TestTrue(GraphWhat + TEXT(": data key ") + Entry.Key + TEXT(" is an output pin"), Outputs.Contains(Entry.Key));
				TArray<FString> EntryTargets;
				if (Entry.Value->Type == EJson::String)
				{
					EntryTargets.Add(Entry.Value->AsString());
				}
				else if (TestTrue(GraphWhat + TEXT(": data ") + Entry.Key + TEXT(" is a string or an array"), Entry.Value->Type == EJson::Array))
				{
					TestTrue(GraphWhat + TEXT(": an array value has several targets ") + Entry.Key, Entry.Value->AsArray().Num() > 1);
					for (const TSharedPtr<FJsonValue>& Target : Entry.Value->AsArray())
					{
						EntryTargets.Add(Target->AsString());
					}
				}
				for (const FString& Target : EntryTargets)
				{
					bool bSeen = false;
					Targets.Add(Target, &bSeen);
					TestFalse(GraphWhat + TEXT(": ") + Target + TEXT(" is the target of one edge"), bSeen);
					TestTrue(GraphWhat + TEXT(": ") + Target + TEXT(" is a connected data input"), ConnectedInputs.Contains(Target));
				}
			}
			for (const FString& Input : ConnectedInputs)
			{
				TestTrue(GraphWhat + TEXT(": connected input ") + Input + TEXT(" has its source in flows.data"), Targets.Contains(Input));
			}
		}
	}

	/** True when NodeKey is the node's id, member_name, class or title (card 0138: nodes have no "name") */
	static bool NodeMatches(const TSharedPtr<FJsonObject>& Node, const FString& NodeKey)
	{
		for (const TCHAR* Field : { TEXT("id"), TEXT("member_name"), N2CJsonContract::NodeClassField, N2CJsonContract::NodeTitleField })
		{
			FString Value;
			if (Node->TryGetStringField(Field, Value) && Value == NodeKey)
			{
				return true;
			}
		}
		return false;
	}

	/** The data targets of the output pin named OutputPin on the node NodeName (NodeMatches) in the capture's first graph */
	TArray<FString> DataTargetsOf(const TSharedPtr<FJsonObject>& Capture, const FString& NodeName, const FString& OutputPin)
	{
		TArray<FString> Result;
		const TSharedPtr<FJsonObject> Graph = Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
		FString SourceRef;
		for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (!NodeMatches(Node, NodeName))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(TEXT("output_pins")))
			{
				if (PinValue->AsObject()->GetStringField(TEXT("name")) == OutputPin)
				{
					SourceRef = Node->GetStringField(TEXT("id")) + TEXT(".") + PinValue->AsObject()->GetStringField(TEXT("id"));
				}
			}
		}
		if (!TestFalse(FString::Printf(TEXT("%s.%s is captured"), *NodeName, *OutputPin), SourceRef.IsEmpty()))
		{
			return Result;
		}
		const TSharedPtr<FJsonValue> Value = Graph->GetObjectField(TEXT("flows"))->GetObjectField(N2CJsonContract::DataField)->TryGetField(SourceRef);
		if (Value.IsValid() && Value->Type == EJson::String)
		{
			Result.Add(Value->AsString());
		}
		else if (Value.IsValid() && Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Target : Value->AsArray())
			{
				Result.Add(Target->AsString());
			}
		}
		return Result;
	}

	/** The pin ref ("N3.P7") of the input pin named InputPin on the node named NodeName in the capture's first graph */
	FString InputRef(const TSharedPtr<FJsonObject>& Capture, const FString& NodeName, const FString& InputPin)
	{
		const TSharedPtr<FJsonObject> Graph = Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
		for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (!NodeMatches(Node, NodeName))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(TEXT("input_pins")))
			{
				if (PinValue->AsObject()->GetStringField(TEXT("name")) == InputPin)
				{
					return Node->GetStringField(TEXT("id")) + TEXT(".") + PinValue->AsObject()->GetStringField(TEXT("id"));
				}
			}
		}
		AddError(FString::Printf(TEXT("%s.%s is not captured"), *NodeName, *InputPin));
		return FString();
	}

	static FString V1Dir()
	{
		return N2CTest::FixturesDir(TEXT("Capture/V1"));
	}

	/**
	 * A v1 document as ToJson writes it after FromJson: the current version, and each node's "name" (its display
	 * title in 1.x) written as "title" in the same place (card 0138); pin names stay as the document has them
	 */
	static FString AsCurrentVersion(const FString& Text)
	{
		const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(Text);
		if (!Object.IsValid())
		{
			return FString();
		}
		Object->SetStringField(N2CJsonContract::VersionField, N2CJsonContract::Version);
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (Object->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& Graph : *Graphs)
			{
				TArray<TSharedPtr<FJsonValue>> Nodes;
				for (const TSharedPtr<FJsonValue>& NodeValue : Graph->AsObject()->GetArrayField(TEXT("nodes")))
				{
					TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : NodeValue->AsObject()->Values)
					{
						Node->SetField(Field.Key == N2CJsonContract::NodeNameFieldV1 ? FString(N2CJsonContract::NodeTitleField) : Field.Key, Field.Value);
					}
					Nodes.Add(MakeShared<FJsonValueObject>(Node));
				}
				Graph->AsObject()->SetArrayField(TEXT("nodes"), Nodes);
			}
		}
		return PrettyJson(Object);
	}

	/** A pin name compared loosely: no spaces or underscores, lower case ("then_0" and "Then 0" are "then0") */
	static FString LoosePinName(const FString& Name)
	{
		return Name.Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT("")).ToLower();
	}

	/**
	 * The pin ref ("N3.P7") of an exec pin (a pin without "type") in the capture's first graph: on the first node whose
	 * member_name or name is NodeKey, the output (or input) exec pin named PinName (compared with LoosePinName; "" for
	 * the blank exec pins of calls and events)
	 */
	FString ExecPinRef(const TSharedPtr<FJsonObject>& Capture, const FString& NodeKey, const FString& PinName, bool bOutput)
	{
		const TSharedPtr<FJsonObject> Graph = Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
		for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (!NodeMatches(Node, NodeKey))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(bOutput ? TEXT("output_pins") : TEXT("input_pins")))
			{
				const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
				if (!Pin->HasField(TEXT("type")) && LoosePinName(Pin->GetStringField(TEXT("name"))) == LoosePinName(PinName))
				{
					return Node->GetStringField(TEXT("id")) + TEXT(".") + Pin->GetStringField(TEXT("id"));
				}
			}
			break;
		}
		AddError(FString::Printf(TEXT("no exec %s pin '%s' on node '%s'"), bOutput ? TEXT("output") : TEXT("input"), *PinName, *NodeKey));
		return FString();
	}

	/** One exec link by names: source node (member_name or name) and its output pin, target node and its input pin */
	struct FExecLink
	{
		FString SourceNode;
		FString SourcePin;
		FString TargetNode;
		FString TargetPin;
	};

	/**
	 * Captures the named case and checks that its first graph's flows.execution is exactly Links (in any order), each
	 * written "N3.P5->N4.P1" from the exec output pin to the exec input pin
	 */
	void ExpectExecLinks(const TCHAR* CaseName, const TArray<FExecLink>& Links)
	{
		const FCaptureCase* Case = FindCase(CaseName);
		if (!TestNotNull(CaseName, Case))
		{
			return;
		}
		const FCaptured Captured = Capture(*Case);
		if (!Captured.Capture.IsValid())
		{
			return;
		}
		TArray<FString> Expected;
		for (const FExecLink& Link : Links)
		{
			Expected.Add(ExecPinRef(Captured.Capture, Link.SourceNode, Link.SourcePin, true) + N2CJsonContract::ExecEdgeSeparator
				+ ExecPinRef(Captured.Capture, Link.TargetNode, Link.TargetPin, false));
		}
		TArray<FString> Actual;
		const TSharedPtr<FJsonObject> Graph = Captured.Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
		for (const TSharedPtr<FJsonValue>& Flow : Graph->GetObjectField(TEXT("flows"))->GetArrayField(N2CJsonContract::ExecutionField))
		{
			Actual.Add(Flow->AsString());
		}
		Expected.Sort();
		Actual.Sort();
		TestEqual(FString(CaseName) + TEXT(": flows.execution"), FString::Join(Actual, TEXT(", ")), FString::Join(Expected, TEXT(", ")));
		TestTrue(FString(CaseName) + TEXT(": the capture validates"), Captured.Blueprint.IsValid());
	}

	/**
	 * flows.execution is pin to pin: every entry is "N#.P#->N#.P#" from an exec output pin to an exec input pin of the
	 * graph, no entry appears twice, every connected exec output is the source of an entry and every connected exec
	 * input the target of one
	 */
	void ExpectPinQualifiedExecFlows(const FString& What, const TSharedPtr<FJsonObject>& Capture)
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : Capture->GetArrayField(TEXT("graphs")))
		{
			const TSharedPtr<FJsonObject> Graph = GraphValue->AsObject();
			const FString GraphWhat = What + TEXT(" ") + Graph->GetStringField(TEXT("name"));
			TSet<FString> ExecOutputs;
			TSet<FString> ExecInputs;
			TSet<FString> ConnectedOutputs;
			TSet<FString> ConnectedInputs;
			// Card 0137: a disabled node has no exec entries (exec passes around it), an unreachable node's exec input
			// may have had only a disabled source, and a link into a disabled node exec does not pass through is dropped
			bool bStopsAtDisabledNode = false;
			for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
			{
				const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
				const FString NodeID = Node->GetStringField(TEXT("id"));
				if (NodeFlag(Node, N2CJsonContract::DisabledField))
				{
					int32 ExecIn = 0;
					int32 ExecOut = 0;
					for (const bool bOutput : { false, true })
					{
						for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(bOutput ? TEXT("output_pins") : TEXT("input_pins")))
						{
							(bOutput ? ExecOut : ExecIn) += PinValue->AsObject()->HasField(TEXT("type")) ? 0 : 1;
						}
					}
					bStopsAtDisabledNode |= !(ExecIn == 1 && ExecOut == 1);
					continue;
				}
				const bool bUnreachable = NodeFlag(Node, N2CJsonContract::UnreachableField);
				for (const bool bOutput : { false, true })
				{
					for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(bOutput ? TEXT("output_pins") : TEXT("input_pins")))
					{
						const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
						if (Pin->HasField(TEXT("type")))
						{
							continue;
						}
						const FString Ref = NodeID + TEXT(".") + Pin->GetStringField(TEXT("id"));
						(bOutput ? ExecOutputs : ExecInputs).Add(Ref);
						bool bConnected = false;
						if (Pin->TryGetBoolField(TEXT("connected"), bConnected) && bConnected && (bOutput || !bUnreachable))
						{
							(bOutput ? ConnectedOutputs : ConnectedInputs).Add(Ref);
						}
					}
				}
			}

			TSet<FString> Entries;
			TSet<FString> Sources;
			TSet<FString> Targets;
			for (const TSharedPtr<FJsonValue>& Flow : Graph->GetObjectField(TEXT("flows"))->GetArrayField(N2CJsonContract::ExecutionField))
			{
				const FString Entry = Flow->AsString();
				bool bSeen = false;
				Entries.Add(Entry, &bSeen);
				TestFalse(GraphWhat + TEXT(": exec entry ") + Entry + TEXT(" is recorded once"), bSeen);
				FString Source;
				FString Target;
				if (!TestTrue(GraphWhat + TEXT(": exec entry ") + Entry + TEXT(" is one link"), Entry.Split(N2CJsonContract::ExecEdgeSeparator, &Source, &Target)
					&& !Target.Contains(N2CJsonContract::ExecEdgeSeparator)))
				{
					continue;
				}
				TestTrue(GraphWhat + TEXT(": ") + Entry + TEXT(" starts at an exec output pin"), ExecOutputs.Contains(Source));
				TestTrue(GraphWhat + TEXT(": ") + Entry + TEXT(" ends at an exec input pin"), ExecInputs.Contains(Target));
				Sources.Add(Source);
				Targets.Add(Target);
			}
			for (const FString& Output : ConnectedOutputs)
			{
				TestTrue(GraphWhat + TEXT(": connected exec output ") + Output + TEXT(" is the source of an exec entry"),
					Sources.Contains(Output) || bStopsAtDisabledNode);
			}
			for (const FString& Input : ConnectedInputs)
			{
				TestTrue(GraphWhat + TEXT(": connected exec input ") + Input + TEXT(" is the target of an exec entry"), Targets.Contains(Input));
			}
		}
	}

	/** A one-graph document: N1 (outputs P1 exec, P2 Integer) and N2 (inputs P3 exec, P4 Integer; output P5 exec) */
	static FString ExecFlowDocument(const FString& Version, const FString& ExecEntry)
	{
		return FString::Printf(TEXT("{\"version\":\"%s\",\"metadata\":{\"name\":\"BP\",\"blueprint_type\":\"Normal\",")
			TEXT("\"blueprint_class\":\"BP\"},\"graphs\":[{\"name\":\"G\",\"graph_type\":\"Function\",\"nodes\":[")
			TEXT("{\"id\":\"N1\",\"type\":\"FunctionEntry\",\"class\":\"K2Node_FunctionEntry\",\"title\":\"G\",\"input_pins\":[],\"output_pins\":[")
			TEXT("{\"id\":\"P1\",\"name\":\"then\",\"connected\":true},{\"id\":\"P2\",\"name\":\"Value\",\"type\":\"Integer\"}]},")
			TEXT("{\"id\":\"N2\",\"type\":\"CallFunction\",\"class\":\"K2Node_CallFunction\",\"member_parent\":\"KismetSystemLibrary\",")
			TEXT("\"member_name\":\"CollectGarbage\",\"input_pins\":[")
			TEXT("{\"id\":\"P3\",\"name\":\"execute\",\"connected\":true},{\"id\":\"P4\",\"name\":\"Count\",\"type\":\"Integer\"}],")
			TEXT("\"output_pins\":[{\"id\":\"P5\",\"name\":\"then\"}]}],")
			TEXT("\"flows\":{\"execution\":[\"%s\"],\"data\":{}}}],\"structs\":[],\"enums\":[]}"), *Version, *ExecEntry);
	}

	/** A node's boolean flag ("pure", "unused", ...): true only when present and true */
	static bool NodeFlag(const TSharedPtr<FJsonObject>& Node, const TCHAR* Field)
	{
		bool bValue = false;
		return Node->TryGetBoolField(Field, bValue) && bValue;
	}

	/** The pin named PinName in Node's PinsField ("input_pins" or "output_pins"); null and an error when none */
	TSharedPtr<FJsonObject> FindPinObject(const TSharedPtr<FJsonObject>& Node, const TCHAR* PinsField, const FString& PinName)
	{
		if (Node.IsValid())
		{
			for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(PinsField))
			{
				if (PinValue->AsObject()->GetStringField(N2CJsonContract::PinNameField) == PinName)
				{
					return PinValue->AsObject();
				}
			}
		}
		AddError(FString::Printf(TEXT("no pin '%s' in %s"), *PinName, PinsField));
		return nullptr;
	}

	/** A pin's string field, empty when it is absent */
	static FString PinField(const TSharedPtr<FJsonObject>& Pin, const TCHAR* Field)
	{
		FString Value;
		if (Pin.IsValid())
		{
			Pin->TryGetStringField(Field, Value);
		}
		return Value;
	}

	/** Checks a pin's type, sub_type, value_type and value_sub_type (an empty expectation means the field is absent) */
	void ExpectPinTypes(const FString& What, const TSharedPtr<FJsonObject>& Pin, const FString& Type, const FString& SubType,
		const FString& ValueType = FString(), const FString& ValueSubType = FString())
	{
		if (!Pin.IsValid())
		{
			return;
		}
		TestEqual(What + TEXT(" type"), PinField(Pin, TEXT("type")), Type);
		TestEqual(What + TEXT(" sub_type"), PinField(Pin, TEXT("sub_type")), SubType);
		TestEqual(What + TEXT(" value_type"), PinField(Pin, N2CJsonContract::PinValueTypeField), ValueType);
		TestEqual(What + TEXT(" value_sub_type"), PinField(Pin, N2CJsonContract::PinValueSubTypeField), ValueSubType);
	}

	/** The "name" of each entry of the capture's top-level array Field ("structs" or "enums") */
	static TArray<FString> TopLevelNames(const TSharedPtr<FJsonObject>& Capture, const TCHAR* Field)
	{
		TArray<FString> Names;
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (Capture.IsValid() && Capture->TryGetArrayField(Field, Entries))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Entries)
			{
				Names.Add(Entry->AsObject()->GetStringField(TEXT("name")));
			}
		}
		return Names;
	}

	/** A one-graph document whose node N1 has one map output pin P1 (key Name); PinExtra is appended inside the pin */
	static FString MapPinDocument(const FString& PinExtra)
	{
		return FString::Printf(TEXT("{\"version\":\"%s\",\"metadata\":{\"name\":\"BP\",\"blueprint_type\":\"Normal\",")
			TEXT("\"blueprint_class\":\"BP\"},\"graphs\":[{\"name\":\"G\",\"graph_type\":\"Function\",\"nodes\":[")
			TEXT("{\"id\":\"N1\",\"type\":\"VariableGet\",\"class\":\"K2Node_VariableGet\",\"member_parent\":\"BP\",")
			TEXT("\"member_name\":\"Counts\",\"pure\":true,\"input_pins\":[],\"output_pins\":[")
			TEXT("{\"id\":\"P1\",\"name\":\"Counts\",\"type\":\"Name\",\"is_map\":true%s}]}],")
			TEXT("\"flows\":{\"execution\":[],\"data\":{}}}],\"structs\":[],\"enums\":[]}"), N2CJsonContract::Version, *PinExtra);
	}

	/** The node of the capture's first graph whose member_name (or else name) is Key; null and an error when none */
	TSharedPtr<FJsonObject> FindNode(const TSharedPtr<FJsonObject>& Capture, const FString& Key)
	{
		const TSharedPtr<FJsonObject> Graph = Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
		for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (NodeMatches(Node, Key))
			{
				return Node;
			}
		}
		AddError(FString::Printf(TEXT("no node '%s' in the first graph"), *Key));
		return nullptr;
	}

	/** The liveness flags a node carries, joined ("unused", "unused,disabled", "" for none) */
	static FString LivenessOf(const TSharedPtr<FJsonObject>& Node)
	{
		TArray<FString> Flags;
		for (const TCHAR* Field : { N2CJsonContract::UnusedField, N2CJsonContract::UnreachableField, N2CJsonContract::DisabledField })
		{
			if (Node.IsValid() && NodeFlag(Node, Field))
			{
				Flags.Add(Field);
			}
		}
		return FString::Join(Flags, TEXT(","));
	}

	/** Expects each named node (member_name or name) of the capture's first graph to carry exactly these flags */
	void ExpectLiveness(const FString& What, const TSharedPtr<FJsonObject>& Capture, const TArray<TPair<FString, FString>>& Expected)
	{
		for (const TPair<FString, FString>& Entry : Expected)
		{
			TestEqual(What + TEXT(": liveness of ") + Entry.Key, LivenessOf(FindNode(Capture, Entry.Key)), Entry.Value);
		}
	}

	/** A capture's JSON without the display hints (node "title", pin "label"), pretty printed */
	static FString WithoutHints(const FString& Json)
	{
		const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(Json);
		if (!Object.IsValid())
		{
			return Json;
		}
		for (const TSharedPtr<FJsonValue>& Graph : Object->GetArrayField(TEXT("graphs")))
		{
			for (const TSharedPtr<FJsonValue>& Node : Graph->AsObject()->GetArrayField(TEXT("nodes")))
			{
				Node->AsObject()->RemoveField(N2CJsonContract::NodeTitleField);
				for (const TCHAR* Pins : { TEXT("input_pins"), TEXT("output_pins") })
				{
					for (const TSharedPtr<FJsonValue>& Pin : Node->AsObject()->GetArrayField(Pins))
					{
						Pin->AsObject()->RemoveField(N2CJsonContract::PinLabelField);
					}
				}
			}
		}
		return PrettyJson(Object);
	}

	/** Captures a case by name; an invalid capture (with an error) when the case or the capture fails */
	FCaptured CaptureNamed(const TCHAR* CaseName)
	{
		const FCaptureCase* Case = FindCase(CaseName);
		if (!TestNotNull(CaseName, Case))
		{
			return FCaptured();
		}
		return Capture(*Case);
	}

	void AddCases();

	const FCaptureCase* FindCase(const TCHAR* Name) const
	{
		return Cases.FindByPredicate([Name](const FCaptureCase& Case) { return Case.Name == Name; });
	}

	/**
	 * Checks a capture's IDs and maps: node and pin IDs unique across the capture, Blueprint valid, NodeGuids one entry
	 * per node, Pins one entry per pin naming that pin's node, and no (NodeGuid, PinGuid) pair twice
	 */
	void ExpectConsistentIDs(const FString& What, const FN2CCapture& Capture)
	{
		TestTrue(What + TEXT(": the captured Blueprint validates"), Capture.Blueprint.IsValid());
		TSet<FString> NodeIDs;
		TSet<FString> PinIDs;
		TSet<TPair<FGuid, FGuid>> PinGuidPairs;
		int32 NodeCount = 0;
		int32 PinCount = 0;
		for (const FN2CGraph& Graph : Capture.Blueprint.Graphs)
		{
			for (const FN2CNodeDefinition& Node : Graph.Nodes)
			{
				++NodeCount;
				bool bSeen = false;
				NodeIDs.Add(Node.ID, &bSeen);
				TestFalse(What + TEXT(": node ID ") + Node.ID + TEXT(" is used once"), bSeen);
				TestTrue(What + TEXT(": NodeGuids has ") + Node.ID, Capture.NodeGuids.Contains(Node.ID));
				for (const TArray<FN2CPinDefinition>* Pins : { &Node.InputPins, &Node.OutputPins })
				{
					for (const FN2CPinDefinition& Pin : *Pins)
					{
						++PinCount;
						PinIDs.Add(Pin.ID, &bSeen);
						TestFalse(What + TEXT(": pin ID ") + Pin.ID + TEXT(" is used once"), bSeen);
						const FN2CCapturedPin* Entry = Capture.Pins.Find(Pin.ID);
						if (TestNotNull(What + TEXT(": Pins has ") + Pin.ID, Entry))
						{
							TestEqual(What + TEXT(": the node of ") + Pin.ID, Entry->NodeID, Node.ID);
							TestEqual(What + TEXT(": the node GUID of ") + Pin.ID, Entry->NodeGuid.ToString(), Capture.NodeGuids.FindRef(Node.ID).ToString());
							PinGuidPairs.Add(TPair<FGuid, FGuid>(Entry->NodeGuid, Entry->PinGuid), &bSeen);
							TestFalse(What + TEXT(": the (node GUID, pin GUID) of ") + Pin.ID + TEXT(" is unique"), bSeen);
						}
					}
				}
			}
		}
		TestEqual(What + TEXT(": one Pins entry per captured pin"), Capture.Pins.Num(), PinCount);
		TestEqual(What + TEXT(": one NodeGuids entry per captured node"), Capture.NodeGuids.Num(), NodeCount);
		TestEqual(What + TEXT(": one GraphGuids entry per graph"), Capture.GraphGuids.Num(), Capture.Blueprint.Graphs.Num());
	}

END_DEFINE_SPEC(FN2CCaptureGoldenSpec)

namespace N2CCaptureGolden
{
	using N2CTest::FTestBlueprint;

	/** A map pin type: Key's category, subcategory and object describe the key, PinValueType the value (as the editor) */
	FEdGraphPinType MapPinType(const FEdGraphPinType& Key, const FEdGraphPinType& Value)
	{
		FEdGraphPinType Type = Key;
		Type.ContainerType = EPinContainerType::Map;
		Type.PinValueType = FEdGraphTerminalType::FromPinType(Value);
		return Type;
	}

	FEdGraphPinType NamePinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Name;
		return Type;
	}

	UK2Node_CallFunction* AddMath(FTestBlueprint& BP, UEdGraph* Graph, const FName Function)
	{
		return BP.AddCall(Graph, UKismetMathLibrary::StaticClass(), Function);
	}

	/**
	 * A node of class T made as FGraphNodeCreator makes it (called through UEdGraphNode, since some engine nodes make
	 * AllocateDefaultPins private); Setup, when set, runs before the node's pins are made
	 */
	template <typename T>
	T* AddNode(UEdGraph* Graph, TFunction<void(T*)> Setup = nullptr)
	{
		T* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Node, /*bUserAction*/ false, /*bSelectNewNode*/ false);
		if (Setup)
		{
			Setup(Node);
		}
		UEdGraphNode* GraphNode = Node;
		GraphNode->CreateNewGuid();
		GraphNode->PostPlacedNewNode();
		if (GraphNode->Pins.Num() == 0)
		{
			GraphNode->AllocateDefaultPins();
		}
		return Node;
	}

	/** Exec sinks with few pins: CollectGarbage (exec only) and ControlScreensaver (exec and one bool) */
	UK2Node_CallFunction* AddCollectGarbage(FTestBlueprint& BP, UEdGraph* Graph)
	{
		return BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
	}

	UK2Node_CallFunction* AddControlScreensaver(FTestBlueprint& BP, UEdGraph* Graph)
	{
		return BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, ControlScreensaver));
	}

	/** The pin name of a user struct member on Make/Break nodes (<DisplayName>_<n>_<GUID>) */
	FName MemberPinName(UUserDefinedStruct* Struct, const FString& DisplayName)
	{
		if (Struct)
		{
			for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
			{
				if (Var.FriendlyName == DisplayName)
				{
					return Var.VarName;
				}
			}
		}
		return FName(*DisplayName);
	}

	/**
	 * Root(Count) calls Child and then itself (Count from its input); Child calls Grandchild; Grandchild calls Root.
	 * Captured from Root, so with working depth limits depth 1 adds Child and depth 2 adds Grandchild as well.
	 */
	UEdGraph* BuildRecursion(FTestBlueprint& BP)
	{
		UEdGraph* Root = BP.AddFunction(TEXT("Root"));
		UEdGraph* Child = BP.AddFunction(TEXT("Child"));
		UEdGraph* Grandchild = BP.AddFunction(TEXT("Grandchild"));
		BP.AddFunctionInput(Root, TEXT("Count"), FTestBlueprint::IntPinType());
		UK2Node_CallFunction* CallChild = BP.AddSelfCall(Root, TEXT("Child"));
		UK2Node_CallFunction* CallRoot = BP.AddSelfCall(Root, TEXT("Root"));
		UK2Node_CallFunction* CallGrandchild = BP.AddSelfCall(Child, TEXT("Grandchild"));
		UK2Node_CallFunction* CallRootAgain = BP.AddSelfCall(Grandchild, TEXT("Root"));
		UK2Node_FunctionEntry* RootEntry = BP.FunctionEntry(Root);
		BP.Link(RootEntry, UEdGraphSchema_K2::PN_Then, CallChild, UEdGraphSchema_K2::PN_Execute);
		BP.Link(CallChild, UEdGraphSchema_K2::PN_Then, CallRoot, UEdGraphSchema_K2::PN_Execute);
		BP.Link(RootEntry, TEXT("Count"), CallRoot, TEXT("Count"));
		BP.Link(BP.FunctionEntry(Child), UEdGraphSchema_K2::PN_Then, CallGrandchild, UEdGraphSchema_K2::PN_Execute);
		BP.Link(BP.FunctionEntry(Grandchild), UEdGraphSchema_K2::PN_Then, CallRootAgain, UEdGraphSchema_K2::PN_Execute);
		return Root;
	}

	/** A calls B, B calls C, C calls D, each from its entry; captured from A */
	UEdGraph* BuildChain(FTestBlueprint& BP)
	{
		const FName Names[] = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") };
		TArray<UEdGraph*> Graphs;
		for (const FName Name : Names)
		{
			Graphs.Add(BP.AddFunction(Name));
		}
		for (int32 Index = 0; Index + 1 < Graphs.Num(); ++Index)
		{
			UK2Node_CallFunction* Call = BP.AddSelfCall(Graphs[Index], Names[Index + 1]);
			BP.Link(BP.FunctionEntry(Graphs[Index]), UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute);
		}
		return Graphs[0];
	}

	/**
	 * Root calls Near and then Far. Far -> Mid -> Shared (Shared at depth 3 that way); Near -> Shared (depth 2);
	 * Shared -> Leaf -> Beyond. Far is queued last, so a depth-first walk reaches Shared through Far at depth 3 first
	 * and drops Leaf at Max Translation Depth 3; a shallowest-first walk reaches Shared at depth 2 and keeps Leaf
	 * (depth 3), while Beyond (depth 4) stays out.
	 */
	UEdGraph* BuildShortestPath(FTestBlueprint& BP)
	{
		TMap<FName, UEdGraph*> Graphs;
		for (const TCHAR* Name : { TEXT("Root"), TEXT("Near"), TEXT("Far"), TEXT("Mid"), TEXT("Shared"), TEXT("Leaf"),
			TEXT("Beyond") })
		{
			Graphs.Add(Name, BP.AddFunction(Name));
		}
		// The caller runs its calls in order from its entry
		auto Calls = [&BP, &Graphs](const TCHAR* Caller, const TArray<FName>& Callees)
		{
			UEdGraph* Graph = Graphs.FindRef(Caller);
			UEdGraphNode* Previous = BP.FunctionEntry(Graph);
			for (const FName Callee : Callees)
			{
				UK2Node_CallFunction* Call = BP.AddSelfCall(Graph, Callee);
				BP.Link(Previous, UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute);
				Previous = Call;
			}
		};
		Calls(TEXT("Root"), { TEXT("Near"), TEXT("Far") });
		Calls(TEXT("Near"), { TEXT("Shared") });
		Calls(TEXT("Far"), { TEXT("Mid") });
		Calls(TEXT("Mid"), { TEXT("Shared") });
		Calls(TEXT("Shared"), { TEXT("Leaf") });
		Calls(TEXT("Leaf"), { TEXT("Beyond") });
		return Graphs.FindRef(TEXT("Root"));
	}

	/**
	 * Run calls Helper, then Init; Helper calls BP_..._Other's Init. Graphs of one depth are taken most recently queued
	 * first, so the own Init is captured before Helper queues the other Init: a name check would drop the second.
	 */
	UEdGraph* BuildSameNameOwners(FTestBlueprint& BP)
	{
		FTestBlueprint* Other = BP.AddSiblingBlueprint(TEXT("BP_N2CGolden_SameNameOwners_Other"));
		if (!Other)
		{
			return nullptr;
		}
		UEdGraph* OtherInit = Other->AddFunction(TEXT("Init"));
		UK2Node_CallFunction* OtherCollect = AddCollectGarbage(*Other, OtherInit);
		Other->Link(Other->FunctionEntry(OtherInit), UEdGraphSchema_K2::PN_Then, OtherCollect, UEdGraphSchema_K2::PN_Execute);

		UEdGraph* Run = BP.AddFunction(TEXT("Run"));
		UEdGraph* Helper = BP.AddFunction(TEXT("Helper"));
		UEdGraph* Init = BP.AddFunction(TEXT("Init"));
		UK2Node_CallFunction* OwnScreensaver = AddControlScreensaver(BP, Init);
		BP.Link(BP.FunctionEntry(Init), UEdGraphSchema_K2::PN_Then, OwnScreensaver, UEdGraphSchema_K2::PN_Execute);

		// A call from Helper to the other Blueprint's Init (through that Blueprint's skeleton class)
		UK2Node_CallFunction* CallOtherInit = Other->AddSelfCall(Helper, TEXT("Init"));
		BP.Link(BP.FunctionEntry(Helper), UEdGraphSchema_K2::PN_Then, CallOtherInit, UEdGraphSchema_K2::PN_Execute);

		UK2Node_CallFunction* CallHelper = BP.AddSelfCall(Run, TEXT("Helper"));
		UK2Node_CallFunction* CallInit = BP.AddSelfCall(Run, TEXT("Init"));
		BP.Link(BP.FunctionEntry(Run), UEdGraphSchema_K2::PN_Then, CallHelper, UEdGraphSchema_K2::PN_Execute);
		BP.Link(CallHelper, UEdGraphSchema_K2::PN_Then, CallInit, UEdGraphSchema_K2::PN_Execute);
		return Run;
	}

	/**
	 * Nested(): the entry runs a collapsed graph GoldenCollapsed(In -> CollectGarbage -> Out), whose Out runs an
	 * instance of the Blueprint's own macro LocalSteps (In -> ControlScreensaver -> Out)
	 */
	UEdGraph* BuildNested(FTestBlueprint& BP)
	{
		const FEdGraphPinType Exec = FTestBlueprint::ExecPinType();
		UEdGraph* Graph = BP.AddFunction(TEXT("Nested"));

		UK2Node_Composite* Collapsed = BP.AddCollapsedGraph(Graph, TEXT("GoldenCollapsed"));
		UEdGraph* Bound = Collapsed ? Collapsed->BoundGraph.Get() : nullptr;
		UK2Node_Tunnel* CollapsedIn = BP.TunnelEntry(Bound);
		UK2Node_Tunnel* CollapsedOut = BP.TunnelExit(Bound);
		BP.AddTunnelPin(CollapsedIn, TEXT("In"), Exec);
		BP.AddTunnelPin(CollapsedOut, TEXT("Out"), Exec);
		UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Bound);
		BP.Link(CollapsedIn, TEXT("In"), Collect, UEdGraphSchema_K2::PN_Execute);
		BP.Link(Collect, UEdGraphSchema_K2::PN_Then, CollapsedOut, TEXT("Out"));

		UEdGraph* Macro = BP.AddMacro(TEXT("LocalSteps"));
		UK2Node_Tunnel* MacroIn = BP.TunnelEntry(Macro);
		UK2Node_Tunnel* MacroOut = BP.TunnelExit(Macro);
		BP.AddTunnelPin(MacroIn, TEXT("In"), Exec);
		BP.AddTunnelPin(MacroOut, TEXT("Out"), Exec);
		UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Macro);
		BP.Link(MacroIn, TEXT("In"), Screensaver, UEdGraphSchema_K2::PN_Execute);
		BP.Link(Screensaver, UEdGraphSchema_K2::PN_Then, MacroOut, TEXT("Out"));
		UK2Node_MacroInstance* Steps = BP.AddMacroInstance(Graph, Macro);

		BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Collapsed, TEXT("In"));
		BP.Link(Collapsed, TEXT("Out"), Steps, TEXT("In"));
		return Graph;
	}
}

void FN2CCaptureGoldenSpec::AddCases()
{
	using namespace N2CCaptureGolden;

	const FKnownDefect CustomEventNone{ TEXT("translation-core-16"), TEXT("0239"),
		TEXT("Custom events get member_name 'None' (the event processor's custom-event branch is unreachable)") };
	const FKnownDefect SwitchName{ TEXT("translation-core-32"), TEXT("0239"),
		TEXT("Switch nodes are named 'Switch', losing the switched type") };
	const FKnownDefect RepeatMissingName{ TEXT("translation-core-24"), TEXT("0236"),
		TEXT("A user struct or enum met a second time comes back empty and the validator logs ERROR 'Missing name' "
			"(expected errors in this case)") };

	Cases.Add({ TEXT("FanOutData"),
		TEXT("Function FanOut(Value): Value feeds Add.A, Add.B and Multiply.A; Add feeds Multiply.B; Multiply is the result."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("FanOut"));
			BP.AddFunctionInput(Graph, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* Add = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
			UK2Node_CallFunction* Multiply = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_IntInt));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Result"), FTestBlueprint::IntPinType());
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Value"), Add, TEXT("A"));
			BP.Link(Entry, TEXT("Value"), Add, TEXT("B"));
			BP.Link(Entry, TEXT("Value"), Multiply, TEXT("A"));
			BP.Link(Add, UEdGraphSchema_K2::PN_ReturnValue, Multiply, TEXT("B"));
			BP.Link(Multiply, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Result"));
			return Graph;
		} });

	Cases.Add({ TEXT("Knots"),
		TEXT("Function Knots(Value): the exec line runs through a knot; Value runs through two knots and the second fans "
			"out to Add.A and Add.B; Add is the result."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("Knots"));
			BP.AddFunctionInput(Graph, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_Knot* ExecKnot = BP.AddKnot(Graph);
			UK2Node_Knot* First = BP.AddKnot(Graph);
			UK2Node_Knot* Second = BP.AddKnot(Graph);
			UK2Node_CallFunction* Add = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Result"), FTestBlueprint::IntPinType());
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, ExecKnot, TEXT("InputPin"));
			BP.Link(ExecKnot, TEXT("OutputPin"), Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Value"), First, TEXT("InputPin"));
			BP.Link(First, TEXT("OutputPin"), Second, TEXT("InputPin"));
			BP.Link(Second, TEXT("OutputPin"), Add, TEXT("A"));
			BP.Link(Second, TEXT("OutputPin"), Add, TEXT("B"));
			BP.Link(Add, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Result"));
			return Graph;
		} });

	// Card 0135: the fan-out of the card's "Done looks like" (PR #7, issue #13)
	Cases.Add({ TEXT("FanOutActorLocation"),
		TEXT("Function ReportLocation(): the entry runs Print String, then SetActorLocation. GetActorLocation's Return "
			"Value feeds both To String (Vector), whose result is Print String's In String, and SetActorLocation's New "
			"Location."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("ReportLocation"));
			UK2Node_CallFunction* GetLocation = BP.AddCall(Graph, AActor::StaticClass(), GET_FUNCTION_NAME_CHECKED(AActor, K2_GetActorLocation));
			UK2Node_CallFunction* ToText = BP.AddCall(Graph, UKismetStringLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_VectorToString));
			UK2Node_CallFunction* Print = BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
			UK2Node_CallFunction* SetLocation = BP.AddCall(Graph, AActor::StaticClass(), GET_FUNCTION_NAME_CHECKED(AActor, K2_SetActorLocation));
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Print, UEdGraphSchema_K2::PN_Then, SetLocation, UEdGraphSchema_K2::PN_Execute);
			BP.Link(GetLocation, UEdGraphSchema_K2::PN_ReturnValue, ToText, TEXT("InVec"));
			BP.Link(ToText, UEdGraphSchema_K2::PN_ReturnValue, Print, TEXT("InString"));
			BP.Link(GetLocation, UEdGraphSchema_K2::PN_ReturnValue, SetLocation, TEXT("NewLocation"));
			return Graph;
		} });

	Cases.Add({ TEXT("Branch"),
		TEXT("Event graph: custom event OnBranch runs a Branch on Less(A, B); True runs CollectGarbage, False runs "
			"ControlScreensaver."),
		{ CustomEventNone },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.EventGraph();
			UK2Node_CustomEvent* Event = BP.AddCustomEvent(Graph, TEXT("OnBranch"));
			UK2Node_CallFunction* Less = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Less_IntInt));
			UK2Node_IfThenElse* Branch = BP.AddBranch(Graph);
			UK2Node_CallFunction* OnTrue = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* OnFalse = AddControlScreensaver(BP, Graph);
			BP.Link(Event, UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Less, UEdGraphSchema_K2::PN_ReturnValue, Branch, UEdGraphSchema_K2::PN_Condition);
			BP.Link(Branch, UEdGraphSchema_K2::PN_Then, OnTrue, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Branch, UEdGraphSchema_K2::PN_Else, OnFalse, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("SpawnedVariableNodes"),
		TEXT("Function Spawned with local Counter (int) and member Score (int), its nodes made by the MCP spawn path "
			"(card 0155, UBlueprintVariableNodeSpawner): Entry runs Set Score (from Get Counter), then Set Counter (from "
			"Get Score); a comment encloses the four variable nodes."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UBlueprint* Blueprint = BP.GetBlueprint();
			BP.AddMemberVariable(TEXT("Score"), FTestBlueprint::IntPinType());
			UEdGraph* Graph = BP.AddFunction(TEXT("Spawned"));
			FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, TEXT("Counter"), FTestBlueprint::IntPinType());
			const FBPVariableDescription* Counter = FN2CMcpVariableUtils::FindLocalVariable(BP.FunctionEntry(Graph), TEXT("Counter"));
			FString Error;
			const FProperty* Score = FN2CMcpVariableUtils::FindMemberVariableProperty(Blueprint, TEXT("Score"), /*bForSet*/ true, Error);
			if (!Counter || !Score)
			{
				return nullptr;
			}
			UK2Node_Variable* SetScore = FN2CMcpNodeSpawn::SpawnMemberVariableNode(Blueprint, Graph, Score, true, FVector2D(300, 0), Error);
			UK2Node_Variable* GetCounter = FN2CMcpNodeSpawn::SpawnLocalVariableNode(Blueprint, Graph, *Counter, false, FVector2D(100, 150), Error);
			UK2Node_Variable* SetCounter = FN2CMcpNodeSpawn::SpawnLocalVariableNode(Blueprint, Graph, *Counter, true, FVector2D(600, 0), Error);
			UK2Node_Variable* GetScore = FN2CMcpNodeSpawn::SpawnMemberVariableNode(Blueprint, Graph, Score, false, FVector2D(400, 150), Error);
			if (!SetScore || !GetCounter || !SetCounter || !GetScore)
			{
				return nullptr;
			}
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, SetScore, UEdGraphSchema_K2::PN_Execute);
			BP.Link(GetCounter, TEXT("Counter"), SetScore, TEXT("Score"));
			BP.Link(SetScore, UEdGraphSchema_K2::PN_Then, SetCounter, UEdGraphSchema_K2::PN_Execute);
			BP.Link(GetScore, TEXT("Score"), SetCounter, TEXT("Counter"));
			FN2CMcpNodeSpawn::SpawnCommentNode(Graph, FSlateRect(50, -50, 800, 250), TEXT("Spawned nodes"), FLinearColor::White, 18, true,
				{ SetScore, GetCounter, SetCounter, GetScore });
			return Graph;
		} });

	Cases.Add({ TEXT("FunctionPinEdits"),
		TEXT("Max Translation Depth 1. Function Caller(In) calls MyFunc(Value) -> Result with In as Value; MyFunc then gets "
			"input Speed (real) and return value bOk through the MCP signature path (card 0156, "
			"FN2CMcpFunctionPinUtils::AddFunctionPin), and Caller wires the call's new bOk into ControlScreensaver run "
			"after it. Captured from Caller."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UBlueprint* Blueprint = BP.GetBlueprint();
			UEdGraph* MyFunc = BP.AddFunction(TEXT("MyFunc"));
			BP.AddFunctionInput(MyFunc, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_FunctionResult* Return = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(BP.FunctionEntry(MyFunc));
			if (!Return)
			{
				return nullptr;
			}
			Return->CreateUserDefinedPin(TEXT("Result"), FTestBlueprint::IntPinType(), EGPD_Input, /*bUseUniqueName*/ false);
			BP.Link(BP.FunctionEntry(MyFunc), TEXT("Value"), Return, TEXT("Result"));

			UEdGraph* Caller = BP.AddFunction(TEXT("Caller"));
			BP.AddFunctionInput(Caller, TEXT("In"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* Call = BP.AddSelfCall(Caller, TEXT("MyFunc"));
			BP.Link(BP.FunctionEntry(Caller), UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute);
			BP.Link(BP.FunctionEntry(Caller), TEXT("In"), Call, TEXT("Value"));

			FN2CFunctionPinEdit Edit;
			if (!FN2CMcpFunctionPinUtils::AddFunctionPin(Blueprint, MyFunc, /*bReturnValue*/ false, TEXT("Speed"),
					FTestBlueprint::DoublePinType(), FString(), FString(), FText::FromString(TEXT("Add Speed (test)")), Edit)
				|| !FN2CMcpFunctionPinUtils::AddFunctionPin(Blueprint, MyFunc, /*bReturnValue*/ true, TEXT("bOk"),
					FTestBlueprint::BoolPinType(), FString(), FString(), FText::FromString(TEXT("Add bOk (test)")), Edit))
			{
				return nullptr;
			}
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Caller);
			BP.Link(Call, UEdGraphSchema_K2::PN_Then, Screensaver, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Call, TEXT("bOk"), Screensaver, TEXT("bAllowScreenSaver"));
			return Caller;
		} });
	Cases.Last().TranslationDepth = 1;

	Cases.Add({ TEXT("SwitchOnInt"),
		TEXT("Function SwitchInt(Selection): Switch on Int with cases 0 and 1 and Default; 0 and Default both run "
			"CollectGarbage, 1 runs ControlScreensaver."),
		{ SwitchName },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("SwitchInt"));
			BP.AddFunctionInput(Graph, TEXT("Selection"), FTestBlueprint::IntPinType());
			UK2Node_SwitchInteger* Switch = BP.AddSwitchOnInt(Graph, 2);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Switch, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Selection"), Switch, TEXT("Selection"));
			BP.Link(Switch, TEXT("0"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Switch, TEXT("1"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Switch, TEXT("Default"), Collect, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("SwitchOnEnum"),
		TEXT("Function SwitchEnum(Mobility: EComponentMobility): Switch on the engine enum; Static runs CollectGarbage, "
			"Movable runs ControlScreensaver, Stationary is unconnected."),
		{ SwitchName },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEnum* Mobility = StaticEnum<EComponentMobility::Type>();
			UEdGraph* Graph = BP.AddFunction(TEXT("SwitchEnum"));
			BP.AddFunctionInput(Graph, TEXT("Mobility"), FTestBlueprint::EnumPinType(Mobility));
			UK2Node_SwitchEnum* Switch = BP.AddSwitchOnEnum(Graph, Mobility);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Switch, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Mobility"), Switch, TEXT("Selection"));
			BP.Link(Switch, TEXT("Static"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Switch, TEXT("Movable"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("MakeBreakStruct"),
		TEXT("Function Structs(Value): Make Vector (X from Value) feeds Break Vector; Break X and Y feed Add (int to "
			"double through the Make); Add is the result."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UScriptStruct* Vector = TBaseStructure<FVector>::Get();
			UEdGraph* Graph = BP.AddFunction(TEXT("Structs"));
			FEdGraphPinType Real;
			Real.PinCategory = UEdGraphSchema_K2::PC_Real;
			Real.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			BP.AddFunctionInput(Graph, TEXT("Value"), Real);
			UK2Node_MakeStruct* Make = BP.AddMakeStruct(Graph, Vector);
			UK2Node_BreakStruct* Break = BP.AddBreakStruct(Graph, Vector);
			UK2Node_CallFunction* Add = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_DoubleDouble));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Result"), Real);
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Value"), Make, TEXT("X"));
			BP.Link(BP.Pin(Make, Vector->GetFName(), EGPD_Output), BP.Pin(Break, Vector->GetFName(), EGPD_Input));
			BP.Link(Break, TEXT("X"), Add, TEXT("A"));
			BP.Link(Break, TEXT("Y"), Add, TEXT("B"));
			BP.Link(Add, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Result"));
			return Graph;
		} });

	Cases.Add({ TEXT("GateMultiExec"),
		TEXT("Event graph: custom events OnEnter, OnOpen and OnClose run a Gate (StandardMacros) through its Enter, "
			"Open and Close inputs; Exit runs CollectGarbage."),
		{ CustomEventNone },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.EventGraph();
			UK2Node_CustomEvent* OnEnter = BP.AddCustomEvent(Graph, TEXT("OnEnter"));
			UK2Node_CustomEvent* OnOpen = BP.AddCustomEvent(Graph, TEXT("OnOpen"));
			UK2Node_CustomEvent* OnClose = BP.AddCustomEvent(Graph, TEXT("OnClose"));
			UK2Node_MacroInstance* Gate = BP.AddStandardMacro(Graph, TEXT("Gate"));
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			BP.Link(OnEnter, UEdGraphSchema_K2::PN_Then, Gate, TEXT("Enter"));
			BP.Link(OnOpen, UEdGraphSchema_K2::PN_Then, Gate, TEXT("Open"));
			BP.Link(OnClose, UEdGraphSchema_K2::PN_Then, Gate, TEXT("Close"));
			BP.Link(Gate, TEXT("Exit"), Collect, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("TimelineMultiExec"),
		TEXT("Event graph: custom events OnPlay and OnReverse run a Timeline (empty template GoldenTimeline) through "
			"Play and Reverse; Update runs CollectGarbage, Finished runs ControlScreensaver."),
		{ CustomEventNone },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.EventGraph();
			UK2Node_CustomEvent* OnPlay = BP.AddCustomEvent(Graph, TEXT("OnPlay"));
			UK2Node_CustomEvent* OnReverse = BP.AddCustomEvent(Graph, TEXT("OnReverse"));
			UK2Node_Timeline* Timeline = BP.AddTimeline(Graph, TEXT("GoldenTimeline"));
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			BP.Link(OnPlay, UEdGraphSchema_K2::PN_Then, Timeline, TEXT("Play"));
			BP.Link(OnReverse, UEdGraphSchema_K2::PN_Then, Timeline, TEXT("Reverse"));
			BP.Link(Timeline, TEXT("Update"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Timeline, TEXT("Finished"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	// Card 0136: exec flows name the pin on each end, so outputs into one node and inputs of one node stay apart
	Cases.Add({ TEXT("BranchSameTarget"),
		TEXT("Function BranchSame(Condition): Branch on Condition; True and False both run CollectGarbage."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("BranchSame"));
			BP.AddFunctionInput(Graph, TEXT("Condition"), FTestBlueprint::BoolPinType());
			UK2Node_IfThenElse* Branch = BP.AddBranch(Graph);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Condition"), Branch, UEdGraphSchema_K2::PN_Condition);
			BP.Link(Branch, UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Branch, UEdGraphSchema_K2::PN_Else, Collect, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("Sequence"),
		TEXT("Function RunSequence(): a Sequence; then_0 runs CollectGarbage, then_1 runs ControlScreensaver."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("RunSequence"));
			UK2Node_ExecutionSequence* Sequence = BP.AddSequence(Graph);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Sequence, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Sequence, TEXT("then_0"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Sequence, TEXT("then_1"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("ForEachLoop"),
		TEXT("Function Loop(Values: int array): ForEachLoop (StandardMacros) over Values; Loop Body runs CollectGarbage, "
			"Completed runs ControlScreensaver."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			FEdGraphPinType IntArray = FTestBlueprint::IntPinType();
			IntArray.ContainerType = EPinContainerType::Array;
			UEdGraph* Graph = BP.AddFunction(TEXT("Loop"));
			BP.AddFunctionInput(Graph, TEXT("Values"), IntArray);
			UK2Node_MacroInstance* Loop = BP.AddStandardMacro(Graph, TEXT("ForEachLoop"));
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Loop, TEXT("Exec"));
			BP.Link(Entry, TEXT("Values"), Loop, TEXT("Array"));
			BP.Link(Loop, TEXT("LoopBody"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Loop, TEXT("Completed"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	// Card 0127: user types, recursion and depth, copy-paste, nested graphs, Enhanced Input

	Cases.Add({ TEXT("UserStruct"),
		TEXT("Function UseItem(Item: S_GoldenItem): S_GoldenItem is a user struct {Owner: Actor object, Count: int, "
			"Level: user enum E_GoldenLevel {Low, High}}. Break S_GoldenItem feeds Count to the Count result and "
			"Owner, Count and Level to Make S_GoldenItem, which is the Copy result."),
		{ { TEXT("translation-core-8"), TEXT("0236"),
			TEXT("S_GoldenItem is missing from 'structs': its object member Owner gets no type name, the validator rejects "
				"the struct (ERROR 'Member Owner requires a type name') and it is dropped without a warning") },
		  RepeatMissingName },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UUserDefinedEnum* Level = BP.AddUserEnum(TEXT("E_GoldenLevel"), { TEXT("Low"), TEXT("High") });
			UUserDefinedStruct* Item = BP.AddUserStruct(TEXT("S_GoldenItem"), {
				{ TEXT("Owner"), FTestBlueprint::ObjectPinType(AActor::StaticClass()) },
				{ TEXT("Count"), FTestBlueprint::IntPinType() },
				{ TEXT("Level"), FTestBlueprint::EnumPinType(Level) } });
			if (!Item)
			{
				return nullptr;
			}
			UEdGraph* Graph = BP.AddFunction(TEXT("UseItem"));
			BP.AddFunctionInput(Graph, TEXT("Item"), FTestBlueprint::StructPinType(Item));
			UK2Node_BreakStruct* Break = BP.AddBreakStruct(Graph, Item);
			UK2Node_MakeStruct* Make = BP.AddMakeStruct(Graph, Item);
			BP.AddFunctionOutput(Graph, TEXT("Count"), FTestBlueprint::IntPinType());
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Copy"), FTestBlueprint::StructPinType(Item));
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(BP.Pin(Entry, TEXT("Item"), EGPD_Output), BP.Pin(Break, Item->GetFName(), EGPD_Input));
			for (const TCHAR* Member : { TEXT("Owner"), TEXT("Count"), TEXT("Level") })
			{
				const FName PinName = MemberPinName(Item, Member);
				BP.Link(Break, PinName, Make, PinName);
			}
			BP.Link(Break, MemberPinName(Item, TEXT("Count")), Result, TEXT("Count"));
			BP.Link(BP.Pin(Make, Item->GetFName(), EGPD_Output), BP.Pin(Result, TEXT("Copy"), EGPD_Input));
			return Graph;
		} });

	Cases.Last().ExpectedErrors = { TEXT("Member Owner requires a type name"), TEXT("Missing name") };

	Cases.Add({ TEXT("UserEnum"),
		TEXT("Function SwitchLimit(Limit: E_GoldenLimit): E_GoldenLimit is a user enum {Min, Max, MaxHealth}. Switch "
			"on E_GoldenLimit: Min runs CollectGarbage, Max runs ControlScreensaver, MaxHealth is unconnected."),
		{ { TEXT("translation-core-11"), TEXT("0236"),
			TEXT("Enum values whose display name contains 'max' in any case are dropped: E_GoldenLimit lists Min only, "
				"while the Switch still has Max and Max Health outputs") },
		  RepeatMissingName, SwitchName },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UUserDefinedEnum* Limit = BP.AddUserEnum(TEXT("E_GoldenLimit"), { TEXT("Min"), TEXT("Max"), TEXT("MaxHealth") });
			if (!Limit)
			{
				return nullptr;
			}
			UEdGraph* Graph = BP.AddFunction(TEXT("SwitchLimit"));
			BP.AddFunctionInput(Graph, TEXT("Limit"), FTestBlueprint::EnumPinType(Limit));
			UK2Node_SwitchEnum* Switch = BP.AddSwitchOnEnum(Graph, Limit);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Switch, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Limit"), Switch, TEXT("Selection"));
			BP.Link(Switch, TEXT("NewEnumerator0"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Switch, TEXT("NewEnumerator1"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });
	Cases.Last().ExpectedErrors = { TEXT("Missing name") };

	// Card 0238: map value types, enum and bitmask pins (translation-core-13, translation-core-31)

	Cases.Add({ TEXT("MapPins"),
		TEXT("Function Maps with members Counts (Name to int), Items (Name to user struct S_GoldenMapItem {Count: int}) "
			"and Weapons (Name to user enum E_GoldenWeaponType {Sword, Bow}); S_GoldenMapItem and E_GoldenWeaponType are "
			"used only as map values. Get Counts, Get Items and Get Weapons are the results of the same names; a Make Map "
			"(Name to int) is the Made result."),
		{ RepeatMissingName },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UUserDefinedEnum* Weapon = BP.AddUserEnum(TEXT("E_GoldenWeaponType"), { TEXT("Sword"), TEXT("Bow") });
			UUserDefinedStruct* Item = BP.AddUserStruct(TEXT("S_GoldenMapItem"), { { TEXT("Count"), FTestBlueprint::IntPinType() } });
			if (!Weapon || !Item
				|| !BP.AddMemberVariable(TEXT("Counts"), MapPinType(NamePinType(), FTestBlueprint::IntPinType()))
				|| !BP.AddMemberVariable(TEXT("Items"), MapPinType(NamePinType(), FTestBlueprint::StructPinType(Item)))
				|| !BP.AddMemberVariable(TEXT("Weapons"), MapPinType(NamePinType(), FTestBlueprint::EnumPinType(Weapon))))
			{
				return nullptr;
			}
			UEdGraph* Graph = BP.AddFunction(TEXT("Maps"));
			BP.AddFunctionOutput(Graph, TEXT("Counts"), MapPinType(NamePinType(), FTestBlueprint::IntPinType()));
			BP.AddFunctionOutput(Graph, TEXT("Items"), MapPinType(NamePinType(), FTestBlueprint::StructPinType(Item)));
			BP.AddFunctionOutput(Graph, TEXT("Weapons"), MapPinType(NamePinType(), FTestBlueprint::EnumPinType(Weapon)));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Made"),
				MapPinType(NamePinType(), FTestBlueprint::IntPinType()));
			for (const TCHAR* Member : { TEXT("Counts"), TEXT("Items"), TEXT("Weapons") })
			{
				BP.Link(BP.AddVariableGet(Graph, Member), Member, Result, Member);
			}
			FGraphNodeCreator<UK2Node_MakeMap> Creator(*Graph);
			UK2Node_MakeMap* MakeMap = Creator.CreateNode();
			Creator.Finalize();
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(MakeMap->GetOutputPin(), BP.Pin(Result, TEXT("Made"), EGPD_Input));
			return Graph;
		} });
	Cases.Last().ExpectedErrors = { TEXT("Missing name") };

	Cases.Add({ TEXT("BitmaskPin"),
		TEXT("Function Bits with member Flags (int, marked Bitmask in its details): Get Flags is the Value result."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UBlueprint* Blueprint = BP.GetBlueprint();
			if (!BP.AddMemberVariable(TEXT("Flags"), FTestBlueprint::IntPinType()))
			{
				return nullptr;
			}
			// As the variable's Bitmask checkbox does (FBlueprintVarActionDetails::OnBitmaskChanged)
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, TEXT("Flags"), nullptr, FBlueprintMetadata::MD_Bitmask, TEXT(""));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			UEdGraph* Graph = BP.AddFunction(TEXT("Bits"));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_VariableGet* GetFlags = BP.AddVariableGet(Graph, TEXT("Flags"));
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(GetFlags, TEXT("Flags"), Result, TEXT("Value"));
			return Graph;
		} });

	for (const int32 Depth : { 1, 2 })
	{
		FCaptureCase& Recursion = Cases.Add_GetRef({ FString::Printf(TEXT("Recursion_Depth%d"), Depth),
			FString::Printf(TEXT("Max Translation Depth %d. Function Root(Count) calls Child, then itself with Count; Child "
				"calls Grandchild; Grandchild calls Root. Captured from Root."), Depth),
			{},
			&BuildRecursion });
		Recursion.TranslationDepth = Depth;
	}

	// Max Translation Depth counts calls from the captured graph (translation-core-3, card 0130)
	for (const int32 Depth : { 1, 2 })
	{
		FCaptureCase& Chain = Cases.Add_GetRef({ FString::Printf(TEXT("CallChain_Depth%d"), Depth),
			FString::Printf(TEXT("Max Translation Depth %d. Function A calls B, B calls C, C calls D. Captured from A, so "
				"depth 1 adds B and depth 2 adds B and C; D is never captured."), Depth),
			{},
			&BuildChain });
		Chain.TranslationDepth = Depth;
	}

	Cases.Add({ TEXT("ShortestPath_Depth3"),
		TEXT("Max Translation Depth 3. Root calls Near, then Far. Near calls Shared; Far calls Mid, Mid calls Shared; "
			"Shared calls Leaf, Leaf calls Beyond. Captured from Root. Shared is 2 calls away through Near (3 through "
			"Far), so Leaf (3) is captured and Beyond (4) is not."),
		{},
		&BuildShortestPath });
	Cases.Last().TranslationDepth = 3;

	Cases.Add({ TEXT("CopyPaste"),
		TEXT("Function CopyPaste(Enabled): the entry runs ControlScreensaver (bAllowScreenSaver from Enabled), which "
			"runs a copy-pasted duplicate of itself (pasted as the editor does: new node GUID, pin IDs kept), also fed "
			"from Enabled."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("CopyPaste"));
			BP.AddFunctionInput(Graph, TEXT("Enabled"), FTestBlueprint::BoolPinType());
			UK2Node_CallFunction* Original = AddControlScreensaver(BP, Graph);
			const TArray<UEdGraphNode*> Pasted = BP.CopyPaste(Graph, { Original });
			if (Pasted.Num() != 1)
			{
				return nullptr;
			}
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Original, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Original, UEdGraphSchema_K2::PN_Then, Pasted[0], UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Enabled"), Original, TEXT("bAllowScreenSaver"));
			BP.Link(Entry, TEXT("Enabled"), Pasted[0], TEXT("bAllowScreenSaver"));
			return Graph;
		} });

	// Card 0134: graphs are told apart by identity, not by name
	Cases.Add({ TEXT("SameNameOwners"),
		TEXT("Max Translation Depth 2. Function Run calls Helper, then Init (both its own). Helper calls Init of a second "
			"Blueprint in the same package, BP_N2CGolden_SameNameOwners_Other. Captured from Run: both Init graphs "
			"appear, the other Blueprint's with owner_blueprint."),
		{},
		&BuildSameNameOwners });
	Cases.Last().TranslationDepth = 2;

	// Card 0235: a collapsed graph is part of its graph at any depth, and its node names it (imp-translation-core-5, -4);
	// a macro is a Macro graph whatever its name (translation-core-20)
	for (const int32 Depth : { 0, 1 })
	{
		FCaptureCase& Nested = Cases.Add_GetRef({ FString::Printf(TEXT("NestedGraphs_Depth%d"), Depth),
			FString::Printf(TEXT("Max Translation Depth %d. Function Nested(): the entry runs a collapsed graph "
				"GoldenCollapsed (In -> CollectGarbage -> Out), whose Out runs the Blueprint's own macro LocalSteps "
				"(In -> ControlScreensaver -> Out). The collapsed graph is captured at every depth and its Composite node "
				"names it in member_name; the macro graph (a Macro graph, though its name has no 'macro') only from depth 1."),
				Depth),
			{},
			&BuildNested });
		Nested.TranslationDepth = Depth;
	}

	Cases.Add({ TEXT("ConstructionScript"),
		TEXT("The Actor Blueprint's construction script (UserConstructionScript, a function graph with an entry node): "
			"the entry runs CollectGarbage. Its graph_type is Construction."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = FBlueprintEditorUtils::FindUserConstructionScript(BP.GetBlueprint());
			UK2Node_FunctionEntry* Entry = Graph ? BP.FunctionEntry(Graph) : nullptr;
			if (!Entry)
			{
				return nullptr;
			}
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("MacroGraph_Depth1"),
		TEXT("Max Translation Depth 1. Function ApplyHit(): the entry runs an instance of the Blueprint's own macro "
			"DoDamage (In -> ControlScreensaver -> Out), whose Out runs CollectGarbage. DoDamage is a Macro graph."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			const FEdGraphPinType Exec = FTestBlueprint::ExecPinType();
			UEdGraph* Macro = BP.AddMacro(TEXT("DoDamage"));
			UK2Node_Tunnel* MacroIn = BP.TunnelEntry(Macro);
			UK2Node_Tunnel* MacroOut = BP.TunnelExit(Macro);
			BP.AddTunnelPin(MacroIn, TEXT("In"), Exec);
			BP.AddTunnelPin(MacroOut, TEXT("Out"), Exec);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Macro);
			BP.Link(MacroIn, TEXT("In"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Screensaver, UEdGraphSchema_K2::PN_Then, MacroOut, TEXT("Out"));

			UEdGraph* Graph = BP.AddFunction(TEXT("ApplyHit"));
			UK2Node_MacroInstance* Damage = BP.AddMacroInstance(Graph, Macro);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Damage, TEXT("In"));
			BP.Link(Damage, TEXT("Out"), Collect, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });
	Cases.Last().TranslationDepth = 1;

	// Plugin content is the user's too (translation-core-21): the plugin's own /NodeToCode mount stands in for a content
	// or GameFeature plugin (/ShooterCore/...)
	Cases.Add({ TEXT("PluginContent_Depth1"),
		TEXT("Max Translation Depth 1. BP_N2CGolden_PluginHelper lives under the NodeToCode plugin's mount "
			"(/NodeToCode/N2CTest_<guid>, standing in for a content or GameFeature plugin), with the user struct "
			"S_GoldenPluginItem {Count: int} and function Helper(Item: S_GoldenPluginItem), whose entry runs "
			"CollectGarbage. Function CallPlugin() in /Game: the entry runs Helper with Item from Make S_GoldenPluginItem. "
			"Helper is captured with its owner_blueprint and S_GoldenPluginItem is in 'structs'."),
		{ RepeatMissingName },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			FTestBlueprint* Plugin = BP.AddBlueprintAt(TEXT("BP_N2CGolden_PluginHelper"), TEXT("/NodeToCode"));
			UUserDefinedStruct* Item = Plugin ? Plugin->AddUserStruct(TEXT("S_GoldenPluginItem"),
				{ { TEXT("Count"), FTestBlueprint::IntPinType() } }) : nullptr;
			if (!Item)
			{
				return nullptr;
			}
			UEdGraph* Helper = Plugin->AddFunction(TEXT("Helper"));
			Plugin->AddFunctionInput(Helper, TEXT("Item"), FTestBlueprint::StructPinType(Item));
			UK2Node_CallFunction* Collect = AddCollectGarbage(*Plugin, Helper);
			Plugin->Link(Plugin->FunctionEntry(Helper), UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);

			UEdGraph* Graph = BP.AddFunction(TEXT("CallPlugin"));
			UK2Node_CallFunction* CallHelper = Plugin->AddSelfCall(Graph, TEXT("Helper"));
			UK2Node_MakeStruct* Make = BP.AddMakeStruct(Graph, Item);
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, CallHelper, UEdGraphSchema_K2::PN_Execute);
			BP.Link(BP.Pin(Make, Item->GetFName(), EGPD_Output), BP.Pin(CallHelper, TEXT("Item"), EGPD_Input));
			return Graph;
		} });
	Cases.Last().TranslationDepth = 1;
	Cases.Last().ExpectedErrors = { TEXT("Missing name") };

	Cases.Add({ TEXT("CommentBoxes"),
		TEXT("Function Commented(): the entry (at 0,0) runs CollectGarbage (300,0), then ControlScreensaver (600,0). The "
			"comment 'Clean up, then allow the screensaver' spans (250,-50)-(900,200) around the two calls; the comment "
			"'Nothing in here' at (0,400)-(300,500) holds no node."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("Commented"));
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			if (!Entry || !Collect || !Screensaver)
			{
				return nullptr;
			}
			Entry->NodePosX = 0;
			Entry->NodePosY = 0;
			Collect->NodePosX = 300;
			Collect->NodePosY = 0;
			Screensaver->NodePosX = 600;
			Screensaver->NodePosY = 0;
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Collect, UEdGraphSchema_K2::PN_Then, Screensaver, UEdGraphSchema_K2::PN_Execute);
			FN2CMcpNodeSpawn::SpawnCommentNode(Graph, FSlateRect(250, -50, 900, 200), TEXT("Clean up, then allow the screensaver"),
				FLinearColor::White, 18, true, { Collect, Screensaver });
			FN2CMcpNodeSpawn::SpawnCommentNode(Graph, FSlateRect(0, 400, 300, 500), TEXT("Nothing in here"),
				FLinearColor::White, 18, true, {});
			return Graph;
		} });

	Cases.Add({ TEXT("EnhancedInputEvent"),
		TEXT("Event graph: the Enhanced Input action event for a transient UInputAction IA_GoldenJump (Boolean); "
			"Triggered runs CollectGarbage, Completed runs ControlScreensaver."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.EventGraph();
			UEdGraphNode* Action = BP.AddEnhancedInputAction(Graph, TEXT("IA_GoldenJump"));
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			BP.Link(Action, TEXT("Triggered"), Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Action, TEXT("Completed"), Screensaver, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	// Card 0234: node types by the nearest mapped ancestor (translation-core-4, -15, -17, -27)
	Cases.Add({ TEXT("NodeTypes"),
		TEXT("Function NodeTypes with local Counter (int): Entry runs Set members in Vector, then Set Counter (from Get "
			"Counter), then a K2Node_N2CTestUnmapped (a UK2Node subclass with no mapping), then a "
			"K2Node_N2CTestCallFunction (a K2Node_CallFunction subclass with no mapping of its own) calling CollectGarbage."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UBlueprint* Blueprint = BP.GetBlueprint();
			UEdGraph* Graph = BP.AddFunction(TEXT("NodeTypes"));
			FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, TEXT("Counter"), FTestBlueprint::IntPinType());
			const FBPVariableDescription* Counter = FN2CMcpVariableUtils::FindLocalVariable(BP.FunctionEntry(Graph), TEXT("Counter"));
			UFunction* CollectGarbage = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
			if (!Counter || !CollectGarbage)
			{
				return nullptr;
			}
			FString Error;
			UK2Node_SetFieldsInStruct* SetMembers = AddNode<UK2Node_SetFieldsInStruct>(Graph,
				[](UK2Node_SetFieldsInStruct* Node) { Node->StructType = TBaseStructure<FVector>::Get(); });
			UK2Node_Variable* GetCounter = FN2CMcpNodeSpawn::SpawnLocalVariableNode(Blueprint, Graph, *Counter, false, FVector2D(100, 150), Error);
			UK2Node_Variable* SetCounter = FN2CMcpNodeSpawn::SpawnLocalVariableNode(Blueprint, Graph, *Counter, true, FVector2D(600, 0), Error);
			UK2Node_N2CTestUnmapped* Unmapped = AddNode<UK2Node_N2CTestUnmapped>(Graph);
			UK2Node_N2CTestCallFunction* Call = AddNode<UK2Node_N2CTestCallFunction>(Graph,
				[CollectGarbage](UK2Node_N2CTestCallFunction* Node) { Node->SetFromFunction(CollectGarbage); });
			if (!GetCounter || !SetCounter)
			{
				return nullptr;
			}
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, SetMembers, UEdGraphSchema_K2::PN_Execute);
			BP.Link(SetMembers, UEdGraphSchema_K2::PN_Then, SetCounter, UEdGraphSchema_K2::PN_Execute);
			BP.Link(GetCounter, TEXT("Counter"), SetCounter, TEXT("Counter"));
			BP.Link(SetCounter, UEdGraphSchema_K2::PN_Then, Unmapped, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Unmapped, UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	// Card 0137: nodes Blueprint never runs (translation-core-18, translation-core-19, issue #13)
	Cases.Add({ TEXT("Issue13"),
		TEXT("Function Issue13Label(Value), the graph of issue #13: To String (Integer) stands in for its Number Format. "
			"Its Return Value feeds both To Text (String), whose result is the Return node's Result, and the {effect} "
			"argument of a Format Text whose Result nothing reads, so Format Text is unused."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("Issue13Label"));
			BP.AddFunctionInput(Graph, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* NumberFormat = BP.AddCall(Graph, UKismetStringLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_IntToString));
			UK2Node_CallFunction* ToText = BP.AddCall(Graph, UKismetTextLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetTextLibrary, Conv_StringToText));
			UK2Node_FormatText* Format = BP.AddFormatText(Graph, TEXT("{effect}"));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Result"), FTestBlueprint::TextPinType());
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Value"), NumberFormat, TEXT("InInt"));
			BP.Link(NumberFormat, UEdGraphSchema_K2::PN_ReturnValue, ToText, TEXT("InString"));
			BP.Link(NumberFormat, UEdGraphSchema_K2::PN_ReturnValue, Format, TEXT("effect"));
			BP.Link(ToText, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Result"));
			return Graph;
		} });

	Cases.Add({ TEXT("GhostEvents"),
		TEXT("Event graph of a new Actor Blueprint: its default BeginPlay is wired to CollectGarbage, which makes it a "
			"real event; the untouched default ActorBeginOverlap and Tick stay ghost nodes and are not captured."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.EventGraph();
			UK2Node_Event* BeginPlay = BP.FindEvent(Graph, TEXT("ReceiveBeginPlay"));
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			BP.Link(BeginPlay, UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	Cases.Add({ TEXT("DisabledNodes"),
		TEXT("Function Disabled(Value): the entry runs CollectGarbage, then a Print String the user disabled (its In "
			"String from To String (Integer) of Value), then ControlScreensaver set to Development Only, then a disabled "
			"Branch whose True runs ResetGamepadAssignments. Exec passes through the disabled Print String (one exec "
			"input, one exec output) to ControlScreensaver; the disabled Branch ends the chain, so "
			"ResetGamepadAssignments is unreachable and To String, read only by the disabled Print String, is unused."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.AddFunction(TEXT("Disabled"));
			BP.AddFunctionInput(Graph, TEXT("Value"), FTestBlueprint::IntPinType());
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* ToString = BP.AddCall(Graph, UKismetStringLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_IntToString));
			UK2Node_CallFunction* Print = BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			UK2Node_IfThenElse* Branch = BP.AddBranch(Graph);
			UK2Node_CallFunction* Reset = BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, ResetGamepadAssignments));
			UK2Node_FunctionEntry* Entry = BP.FunctionEntry(Graph);
			BP.Link(Entry, UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Collect, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Print, UEdGraphSchema_K2::PN_Then, Screensaver, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Screensaver, UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Branch, UEdGraphSchema_K2::PN_Then, Reset, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Entry, TEXT("Value"), ToString, TEXT("InInt"));
			BP.Link(ToString, UEdGraphSchema_K2::PN_ReturnValue, Print, TEXT("InString"));
			if (Print && Screensaver && Branch)
			{
				// As the editor's Disable (Do Not Compile) and Enable Compile > Development Only commands set them
				Print->SetEnabledState(ENodeEnabledState::Disabled);
				Branch->SetEnabledState(ENodeEnabledState::Disabled);
				Screensaver->SetEnabledState(ENodeEnabledState::DevelopmentOnly);
			}
			return Graph;
		} });

	Cases.Add({ TEXT("UnreachableChain"),
		TEXT("Event graph: custom event OnRun runs CollectGarbage. Apart from it, ControlScreensaver (its Allow Screen "
			"Saver from Not of a literal) runs ResetGamepadAssignments with no event before them: both are unreachable "
			"and Not, read only by them, is unused. The default ghost events are not captured."),
		{ CustomEventNone },
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			UEdGraph* Graph = BP.EventGraph();
			UK2Node_CustomEvent* Event = BP.AddCustomEvent(Graph, TEXT("OnRun"));
			UK2Node_CallFunction* Collect = AddCollectGarbage(BP, Graph);
			UK2Node_CallFunction* Not = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool));
			UK2Node_CallFunction* Screensaver = AddControlScreensaver(BP, Graph);
			UK2Node_CallFunction* Reset = BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, ResetGamepadAssignments));
			BP.Link(Event, UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Not, UEdGraphSchema_K2::PN_ReturnValue, Screensaver, TEXT("bAllowScreenSaver"));
			BP.Link(Screensaver, UEdGraphSchema_K2::PN_Then, Reset, UEdGraphSchema_K2::PN_Execute);
			return Graph;
		} });

	// Card 0138: stable identifiers (gap-5, translation-core-22, translation-core-28)
	Cases.Add({ TEXT("StableNames"),
		TEXT("Function UseNames(): the entry runs Convert_C (a function of this Blueprint), then the result, which "
			"returns Health + Temperature_C. Health is a variable of BP_N2CGolden_Enemy read through this Blueprint's "
			"Enemy reference, Temperature_C a variable of this Blueprint. Names ending in _C keep it, member_parent is "
			"the declaring Blueprint (no type string), exec pins are named execute and then."),
		{},
		[](FTestBlueprint& BP) -> UEdGraph*
		{
			FTestBlueprint* Enemy = BP.AddSiblingBlueprint(TEXT("BP_N2CGolden_Enemy"));
			if (!Enemy || !Enemy->AddMemberVariable(TEXT("Health"), FTestBlueprint::DoublePinType()))
			{
				return nullptr;
			}
			BP.AddMemberVariable(TEXT("Enemy"), FTestBlueprint::ObjectPinType(Enemy->GetBlueprint()->GeneratedClass));
			BP.AddMemberVariable(TEXT("Temperature_C"), FTestBlueprint::DoublePinType());
			BP.AddFunction(TEXT("Convert_C"));
			UEdGraph* Graph = BP.AddFunction(TEXT("UseNames"));
			UK2Node_FunctionResult* Result = BP.AddFunctionOutput(Graph, TEXT("Result"), FTestBlueprint::DoublePinType());
			UK2Node_CallFunction* Convert = BP.AddSelfCall(Graph, TEXT("Convert_C"));
			UK2Node_VariableGet* GetEnemy = BP.AddVariableGet(Graph, TEXT("Enemy"));
			UK2Node_VariableGet* GetHealth = BP.AddVariableGet(Graph, TEXT("Health"), Enemy);
			UK2Node_VariableGet* GetTemperature = BP.AddVariableGet(Graph, TEXT("Temperature_C"));
			UK2Node_CallFunction* Add = AddMath(BP, Graph, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_DoubleDouble));
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Convert, UEdGraphSchema_K2::PN_Execute);
			BP.Link(Convert, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute);
			BP.Link(GetEnemy, TEXT("Enemy"), GetHealth, UEdGraphSchema_K2::PN_Self);
			BP.Link(GetHealth, TEXT("Health"), Add, TEXT("A"));
			BP.Link(GetTemperature, TEXT("Temperature_C"), Add, TEXT("B"));
			BP.Link(Add, UEdGraphSchema_K2::PN_ReturnValue, Result, TEXT("Result"));
			return Graph;
		} });
}

void FN2CCaptureGoldenSpec::Define()
{
	Cases.Reset();
	AddCases();
	
	for (const FCaptureCase& Case : Cases)
	{
		Describe(Case.Name, [this, Case]()
		{
			It(TEXT("captures the golden N2C JSON"), [this, Case]() { RunGolden(Case); });

			// translation-core-25 (card 0135): FromJson reads every pin, struct and enum ToJson writes
			It(TEXT("survives ToJson -> FromJson -> ToJson unchanged"), [this, Case]() { RunRoundTrip(Case); });
		});
	}

	// Card 0234: the node type is the nearest mapped ancestor's (translation-core-4, -15, -17, -27, arch-11)
	Describe(TEXT("NodeTypes"), [this]()
	{
		It(TEXT("resolves each node class to its nearest mapped ancestor, and a class with none to Unknown"), [this]()
		{
			FN2CNodeTypeRegistry& Registry = FN2CNodeTypeRegistry::Get();
			const auto TypeName = [](const EN2CNodeType Type)
			{
				return StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(Type));
			};
			const auto Expect = [this, &Registry, &TypeName](const TCHAR* What, UClass* Class, const EN2CNodeType Expected)
			{
				if (TestNotNull(FString(What) + TEXT(" class"), Class))
				{
					const UK2Node* Node = CastChecked<UK2Node>(NewObject<UObject>(GetTransientPackage(), Class));
					TestEqual(What, TypeName(Registry.GetNodeType(Node)), TypeName(Expected));
				}
			};
			Expect(TEXT("BreakStruct (a StructMemberGet, a StructOperation, a Variable)"), UK2Node_BreakStruct::StaticClass(), EN2CNodeType::BreakStruct);
			Expect(TEXT("SetFieldsInStruct (a MakeStruct)"), UK2Node_SetFieldsInStruct::StaticClass(), EN2CNodeType::SetFieldsInStruct);
			Expect(TEXT("MakeStruct"), UK2Node_MakeStruct::StaticClass(), EN2CNodeType::MakeStruct);
			Expect(TEXT("For Each (Map)"), UK2Node_MapForEach::StaticClass(), EN2CNodeType::ForEachLoop);
			Expect(TEXT("For Each (Set)"), UK2Node_SetForEach::StaticClass(), EN2CNodeType::ForEachLoop);
			Expect(TEXT("To Text (GenericToText)"), UK2Node_GenericToText::StaticClass(), EN2CNodeType::GenericToText);
			Expect(TEXT("Enhanced Input action event"),
				FindObject<UClass>(nullptr, TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction")), EN2CNodeType::InputAction);
			Expect(TEXT("Get Input Action Value"),
				FindObject<UClass>(nullptr, TEXT("/Script/InputBlueprintNodes.K2Node_GetInputActionValue")), EN2CNodeType::GetInputAxisValue);
			Expect(TEXT("a UK2Node_CallFunction subclass with no mapping of its own"), UK2Node_N2CTestCallFunction::StaticClass(),
				EN2CNodeType::CallFunction);
			Expect(TEXT("a UK2Node subclass with no mapped ancestor"), UK2Node_N2CTestUnmapped::StaticClass(), EN2CNodeType::Unknown);
			TestEqual(TEXT("a null node"), TypeName(Registry.GetNodeType(nullptr)), TypeName(EN2CNodeType::Unknown));
		});

		It(TEXT("captures Break Struct as BreakStruct named by its struct, the same in every session"), [this]()
		{
			for (const TCHAR* CaseName : { TEXT("MakeBreakStruct"), TEXT("UserStruct") })
			{
				const FCaptured Captured = CaptureNamed(CaseName);
				const TSharedPtr<FJsonObject> Break = Captured.Capture.IsValid() ? FindNode(Captured.Capture, TEXT("K2Node_BreakStruct")) : nullptr;
				if (!Break.IsValid())
				{
					continue;
				}
				const FString What = FString(CaseName) + TEXT(": Break");
				TestEqual(What + TEXT(" type"), Break->GetStringField(TEXT("type")), FString(TEXT("BreakStruct")));
				const FString MemberName = Break->GetStringField(TEXT("member_name"));
				TestEqual(What + TEXT(" member_name is its struct"), MemberName,
					FString(FCString::Strcmp(CaseName, TEXT("UserStruct")) == 0 ? TEXT("S_GoldenItem") : TEXT("Vector")));
				TestFalse(What + TEXT(" member_name has no object index"), MemberName.StartsWith(TEXT("Var_")));
				FString Title;
				Break->TryGetStringField(TEXT("title"), Title);
				TestNotEqual(What + TEXT(" title"), Title, FString(TEXT("None")));
			}
		});

		It(TEXT("captures Set members in a struct as SetFieldsInStruct, not titled Make"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("NodeTypes"));
			const TSharedPtr<FJsonObject> SetMembers = Captured.Capture.IsValid() ? FindNode(Captured.Capture, TEXT("K2Node_SetFieldsInStruct")) : nullptr;
			if (!SetMembers.IsValid())
			{
				return;
			}
			TestEqual(TEXT("type"), SetMembers->GetStringField(TEXT("type")), FString(TEXT("SetFieldsInStruct")));
			TestEqual(TEXT("member_name"), SetMembers->GetStringField(TEXT("member_name")), FString(TEXT("Vector")));
			FString Title;
			SetMembers->TryGetStringField(TEXT("title"), Title);
			TestFalse(TEXT("the title does not say Make: ") + Title, Title.StartsWith(TEXT("Make")));
		});

		It(TEXT("captures the Enhanced Input action event as an input action event named by its action"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("EnhancedInputEvent"));
			const TSharedPtr<FJsonObject> Action = Captured.Capture.IsValid() ? FindNode(Captured.Capture, TEXT("K2Node_EnhancedInputAction")) : nullptr;
			if (!Action.IsValid())
			{
				return;
			}
			TestEqual(TEXT("type"), Action->GetStringField(TEXT("type")), FString(TEXT("InputAction")));
			TestEqual(TEXT("member_name"), Action->GetStringField(TEXT("member_name")), FString(TEXT("IA_GoldenJump")));
		});

		// Not a golden case: the loops' key and value pin labels are display text the engine makes with
		// FText::FromString, so their capture differs in Japanese (StableNames would fail on an engine quirk)
		It(TEXT("captures For Each (Map) and For Each (Set) as loops and To Text as GenericToText"), [this]()
		{
			const FCaptureCase Loops{ TEXT("NodeTypesLoops"),
				TEXT("Event graph: Custom event Go runs For Each (Map), whose Completed runs For Each (Set); an unwired To Text."),
				{},
				[](N2CTest::FTestBlueprint& BP) -> UEdGraph*
				{
					using namespace N2CCaptureGolden;
					UEdGraph* Graph = BP.EventGraph();
					UK2Node_CustomEvent* Go = BP.AddCustomEvent(Graph, TEXT("Go"));
					UK2Node_MapForEach* MapLoop = AddNode<UK2Node_MapForEach>(Graph);
					UK2Node_SetForEach* SetLoop = AddNode<UK2Node_SetForEach>(Graph);
					AddNode<UK2Node_GenericToText>(Graph);
					BP.Link(Go, UEdGraphSchema_K2::PN_Then, MapLoop, UEdGraphSchema_K2::PN_Execute);
					BP.Link(MapLoop, TEXT("CompletedPin"), SetLoop, UEdGraphSchema_K2::PN_Execute);
					return Graph;
				} };
			const FCaptured Captured = Capture(Loops);
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			for (const TCHAR* Class : { TEXT("K2Node_MapForEach"), TEXT("K2Node_SetForEach") })
			{
				if (const TSharedPtr<FJsonObject> Loop = FindNode(Captured.Capture, Class))
				{
					TestEqual(FString(Class) + TEXT(" type"), Loop->GetStringField(TEXT("type")), FString(TEXT("ForEachLoop")));
				}
			}
			if (const TSharedPtr<FJsonObject> ToText = FindNode(Captured.Capture, TEXT("K2Node_GenericToText")))
			{
				TestEqual(TEXT("To Text's type"), ToText->GetStringField(TEXT("type")), FString(TEXT("GenericToText")));
			}
		});

		It(TEXT("gives a local variable's Get and Set different types"), [this]()
		{
			for (const TCHAR* CaseName : { TEXT("NodeTypes"), TEXT("SpawnedVariableNodes") })
			{
				const FCaptured Captured = CaptureNamed(CaseName);
				if (!Captured.Capture.IsValid())
				{
					continue;
				}
				TArray<FString> CounterTypes;
				for (const TSharedPtr<FJsonValue>& NodeValue : Captured.Capture->GetArrayField(TEXT("graphs"))[0]->AsObject()->GetArrayField(TEXT("nodes")))
				{
					const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
					FString MemberName;
					if (Node->TryGetStringField(TEXT("member_name"), MemberName) && MemberName == TEXT("Counter"))
					{
						CounterTypes.Add(Node->GetStringField(TEXT("type")));
					}
				}
				CounterTypes.Sort();
				TestEqual(FString(CaseName) + TEXT(": the types of Get Counter and Set Counter"), FString::Join(CounterTypes, TEXT(", ")),
					FString(TEXT("LocalVariableGet, LocalVariableSet")));
			}
		});

		It(TEXT("captures a node class with no mapping as Unknown with its class, and a CallFunction subclass as CallFunction"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("NodeTypes"));
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			if (const TSharedPtr<FJsonObject> Unmapped = FindNode(Captured.Capture, TEXT("K2Node_N2CTestUnmapped")))
			{
				TestEqual(TEXT("the unmapped node's type"), Unmapped->GetStringField(TEXT("type")), FString(TEXT("Unknown")));
			}
			if (const TSharedPtr<FJsonObject> Call = FindNode(Captured.Capture, TEXT("K2Node_N2CTestCallFunction")))
			{
				TestEqual(TEXT("the CallFunction subclass's type"), Call->GetStringField(TEXT("type")), FString(TEXT("CallFunction")));
				TestEqual(TEXT("the CallFunction subclass's member_name"), Call->GetStringField(TEXT("member_name")), FString(TEXT("CollectGarbage")));
			}
		});
	});

	// Card 0135: data edges are lossless (translation-core-1, PR #7, issue #13)
	// Card 0235: graph types from the schema, nested-graph discovery (translation-core-20, -21); golden cases
	// ConstructionScript, MacroGraph_Depth1, NestedGraphs_Depth0/1, PluginContent_Depth1 and CommentBoxes
	// Card 0238: pin types from the K2 schema's subcategories, enums and map value types (translation-core-13, -31)
	Describe(TEXT("PinTypes"), [this]()
	{
		It(TEXT("types a map pin by its key and gives its value type"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("MapPins"));
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			ExpectPinTypes(TEXT("TMap<FName, int32> Counts:"), FindPinObject(FindNode(Captured.Capture, TEXT("Counts")), TEXT("output_pins"),
				TEXT("Counts")), TEXT("Name"), FString(), TEXT("Integer"));
			ExpectPinTypes(TEXT("TMap<FName, S_GoldenMapItem> Items:"), FindPinObject(FindNode(Captured.Capture, TEXT("Items")),
				TEXT("output_pins"), TEXT("Items")), TEXT("Name"), FString(), TEXT("Struct"), TEXT("S_GoldenMapItem"));
			ExpectPinTypes(TEXT("TMap<FName, E_GoldenWeaponType> Weapons:"), FindPinObject(FindNode(Captured.Capture, TEXT("Weapons")),
				TEXT("output_pins"), TEXT("Weapons")), TEXT("Name"), FString(), TEXT("Enum"), TEXT("E_GoldenWeaponType"));
		});

		It(TEXT("lists a struct and an enum used only as map values"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("MapPins"));
			TestTrue(TEXT("structs lists S_GoldenMapItem"), TopLevelNames(Captured.Capture, TEXT("structs")).Contains(TEXT("S_GoldenMapItem")));
			TestTrue(TEXT("enums lists E_GoldenWeaponType"), TopLevelNames(Captured.Capture, TEXT("enums")).Contains(TEXT("E_GoldenWeaponType")));
		});

		It(TEXT("names a Make Map by its key type, then its value type"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("MapPins"));
			const TSharedPtr<FJsonObject> MakeMap = Captured.Capture.IsValid() ? FindNode(Captured.Capture, TEXT("K2Node_MakeMap")) : nullptr;
			if (MakeMap.IsValid())
			{
				TestEqual(TEXT("member_name"), MakeMap->GetStringField(TEXT("member_name")), FString(TEXT("Name to Integer")));
				ExpectPinTypes(TEXT("Make Map's output:"), FindPinObject(MakeMap, TEXT("output_pins"), TEXT("Map")), TEXT("Name"), FString(),
					TEXT("Integer"));
			}
		});

		It(TEXT("types an enum pin Enum with its enum as sub_type, for a user and an engine enum"), [this]()
		{
			const FCaptured User = CaptureNamed(TEXT("UserEnum"));
			if (User.Capture.IsValid())
			{
				ExpectPinTypes(TEXT("Switch on E_GoldenLimit's Selection:"), FindPinObject(FindNode(User.Capture, TEXT("K2Node_SwitchEnum")),
					TEXT("input_pins"), TEXT("Selection")), TEXT("Enum"), TEXT("E_GoldenLimit"));
				ExpectPinTypes(TEXT("the Limit parameter:"), FindPinObject(FindNode(User.Capture, TEXT("K2Node_FunctionEntry")),
					TEXT("output_pins"), TEXT("Limit")), TEXT("Enum"), TEXT("E_GoldenLimit"));
			}
			const FCaptured Engine = CaptureNamed(TEXT("SwitchOnEnum"));
			if (Engine.Capture.IsValid())
			{
				ExpectPinTypes(TEXT("Switch on EComponentMobility's Selection:"), FindPinObject(FindNode(Engine.Capture, TEXT("K2Node_SwitchEnum")),
					TEXT("input_pins"), TEXT("Selection")), TEXT("Enum"), TEXT("EComponentMobility"));
			}
		});

		It(TEXT("types a bitmask int pin Bitmask, and a plain int Integer"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("BitmaskPin"));
			if (Captured.Capture.IsValid())
			{
				ExpectPinTypes(TEXT("Get Flags:"), FindPinObject(FindNode(Captured.Capture, TEXT("Flags")), TEXT("output_pins"), TEXT("Flags")),
					TEXT("Bitmask"), TEXT("bitmask"));
				ExpectPinTypes(TEXT("the Value result:"), FindPinObject(FindNode(Captured.Capture, TEXT("K2Node_FunctionResult")),
					TEXT("input_pins"), TEXT("Value")), TEXT("Integer"), FString());
			}
		});

		It(TEXT("types a call's Target pin Self"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("CallChain_Depth1"));
			if (Captured.Capture.IsValid())
			{
				ExpectPinTypes(TEXT("the call to B's Target:"), FindPinObject(FindNode(Captured.Capture, TEXT("B")), TEXT("input_pins"),
					UEdGraphSchema_K2::PN_Self.ToString()), TEXT("Self"), TEXT("self"));
			}
		});

		It(TEXT("checks a type's subcategory before its category"), [this]()
		{
			using N2CStableNames::PinType;
			UEnum* Mobility = StaticEnum<EComponentMobility::Type>();
			const FName None;
			const auto Name = [](EN2CPinType Type) { return StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Type)); };
			TestEqual(TEXT("byte with an enum"), Name(PinType(UEdGraphSchema_K2::PC_Byte, None, Mobility)), Name(EN2CPinType::Enum));
			TestEqual(TEXT("byte"), Name(PinType(UEdGraphSchema_K2::PC_Byte, None, nullptr)), Name(EN2CPinType::Byte));
			TestEqual(TEXT("enum"), Name(PinType(UEdGraphSchema_K2::PC_Enum, None, Mobility)), Name(EN2CPinType::Enum));
			TestEqual(TEXT("bitmask int"), Name(PinType(UEdGraphSchema_K2::PC_Int, UEdGraphSchema_K2::PSC_Bitmask, nullptr)), Name(EN2CPinType::Bitmask));
			TestEqual(TEXT("bitmask byte with its flags enum"), Name(PinType(UEdGraphSchema_K2::PC_Byte, UEdGraphSchema_K2::PSC_Bitmask, Mobility)),
				Name(EN2CPinType::Bitmask));
			TestEqual(TEXT("self object"), Name(PinType(UEdGraphSchema_K2::PC_Object, UEdGraphSchema_K2::PSC_Self, nullptr)), Name(EN2CPinType::Self));
			TestEqual(TEXT("self class"), Name(PinType(UEdGraphSchema_K2::PC_Class, UEdGraphSchema_K2::PSC_Self, nullptr)), Name(EN2CPinType::Self));
			TestEqual(TEXT("index wildcard"), Name(PinType(UEdGraphSchema_K2::PC_Wildcard, UEdGraphSchema_K2::PSC_Index, nullptr)), Name(EN2CPinType::Index));
			TestEqual(TEXT("object"), Name(PinType(UEdGraphSchema_K2::PC_Object, None, AActor::StaticClass())), Name(EN2CPinType::Object));
			TestEqual(TEXT("real"), Name(PinType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double, nullptr)), Name(EN2CPinType::Real));
			TestEqual(TEXT("an unknown category"), Name(PinType(TEXT("no_such_category"), None, nullptr)), Name(EN2CPinType::Wildcard));
		});

		It(TEXT("reads value_type and value_sub_type and writes them back the same"), [this]()
		{
			const FString Text = MapPinDocument(TEXT(",\"value_type\":\"Struct\",\"value_sub_type\":\"S_Item\""));
			FN2CBlueprint Blueprint;
			TArray<FN2CJsonReadWarning> Warnings;
			if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint, Warnings))
				&& TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1) && TestEqual(TEXT("nodes"), Blueprint.Graphs[0].Nodes.Num(), 1))
			{
				const FN2CPinDefinition& Pin = Blueprint.Graphs[0].Nodes[0].OutputPins[0];
				TestTrue(TEXT("ValueType is Struct"), Pin.ValueType == EN2CPinType::Struct);
				TestEqual(TEXT("ValueSubType"), Pin.ValueSubType, FString(TEXT("S_Item")));
				TestEqual(TEXT("no warnings"), Warnings.Num(), 0);
				TestEqual(TEXT("ToJson writes it back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);
			}
		});

		It(TEXT("reads a map pin without a value type (written before card 0238) and writes none"), [this]()
		{
			const FString Text = MapPinDocument(FString());
			FN2CBlueprint Blueprint;
			if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)) && TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1))
			{
				TestFalse(TEXT("no value type"), Blueprint.Graphs[0].Nodes[0].OutputPins[0].HasValueType());
				TestEqual(TEXT("ToJson writes it back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);
			}
		});

		It(TEXT("keeps a value_type it does not know, with a warning"), [this]()
		{
			const FString Text = MapPinDocument(TEXT(",\"value_type\":\"Hologram\""));
			FN2CBlueprint Blueprint;
			TArray<FN2CJsonReadWarning> Warnings;
			if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint, Warnings)) && TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1))
			{
				TestEqual(TEXT("one warning"), Warnings.Num(), 1);
				if (Warnings.Num() == 1)
				{
					TestEqual(TEXT("its code"), Warnings[0].Code, FString(N2CJsonContract::ReadWarning::UnknownPinType));
				}
				TestEqual(TEXT("ToJson writes it back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);
			}
		});
	});

	Describe(TEXT("Discovery"), [this]()
	{
		It(TEXT("captures a graph whose Nodes array holds a null entry as the main graph and as a nested and a listed one"), [this]()
		{
			using namespace N2CCaptureGolden;
			N2CTest::FTestBlueprint BP(TEXT("BP_N2CGolden_NullNode"), TEXT("/Game"));
			UEdGraph* Graph = BP.GetBlueprint() ? BP.AddFunction(TEXT("WithNull")) : nullptr;
			UEdGraph* Caller = Graph ? BP.AddFunction(TEXT("CallsWithNull")) : nullptr;
			if (!TestNotNull(TEXT("the test graphs"), Caller))
			{
				return;
			}
			BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, AddCollectGarbage(BP, Graph), UEdGraphSchema_K2::PN_Execute);
			BP.Link(BP.FunctionEntry(Caller), UEdGraphSchema_K2::PN_Then, BP.AddSelfCall(Caller, TEXT("WithNull")), UEdGraphSchema_K2::PN_Execute);
			if (!TestEqual(TEXT("building the graphs"), BP.Errors().Num(), 0))
			{
				return;
			}

			// As a node class that failed to load leaves it; first, so any loop over the nodes meets it before the rest
			Graph->Nodes.Insert(nullptr, 0);
			ON_SCOPE_EXIT { Graph->Nodes.Remove(nullptr); };

			TArray<UK2Node*> Nodes;
			FN2CNodeCollector::Get().CollectNodesFromGraph(Graph, Nodes);
			FN2CCaptureOptions Options;
			Options.Depth = 0;
			const TValueOrError<FN2CCapture, FString> Main = N2C::CaptureNodes(Nodes, Options);
			if (TestTrue(TEXT("the graph is captured"), Main.HasValue()))
			{
				const FN2CBlueprint& Captured = Main.GetValue().Blueprint;
				TestEqual(TEXT("one graph"), Captured.Graphs.Num(), 1);
				TestEqual(TEXT("its two nodes"), Captured.Graphs.Num() > 0 ? Captured.Graphs[0].Nodes.Num() : 0, 2);
				TestTrue(TEXT("a Function graph"), Captured.Graphs.Num() > 0 && Captured.Graphs[0].GraphType == EN2CGraphType::Function);
			}

			Options.Depth = 1;
			const TValueOrError<FN2CCapture, FString> Nested = N2C::CaptureGraph(*Caller, Options);
			if (TestTrue(TEXT("the caller is captured"), Nested.HasValue()))
			{
				const FN2CBlueprint& Captured = Nested.GetValue().Blueprint;
				TestEqual(TEXT("the caller and the graph it calls"), Captured.Graphs.Num(), 2);
				TestEqual(TEXT("the nested graph's two nodes"), Captured.Graphs.Num() > 1 ? Captured.Graphs[1].Nodes.Num() : 0, 2);
			}

			const TValueOrError<FN2CCapture, FString> Listed = N2C::CaptureGraphs({ Graph }, Options);
			TestTrue(TEXT("the graph is captured from a list"), Listed.HasValue()
				&& Listed.GetValue().Blueprint.Graphs.Num() == 1 && Listed.GetValue().Blueprint.Graphs[0].Nodes.Num() == 2);
		});

		It(TEXT("types an animation graph Animation by its schema"), [this]()
		{
			using namespace N2CCaptureGolden;
			FModuleManager::Get().LoadModule(TEXT("AnimGraph"));
			UClass* SchemaClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraph.AnimationGraphSchema"));
			N2CTest::FTestBlueprint BP(TEXT("BP_N2CGolden_AnimSchema"), TEXT("/Game"));
			if (!TestNotNull(TEXT("UAnimationGraphSchema"), SchemaClass) || !TestNotNull(TEXT("the Blueprint"), BP.GetBlueprint()))
			{
				return;
			}
			// Named as an Animation Blueprint names it; the name has no "animation" in it
			UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(BP.GetBlueprint(), TEXT("AnimGraph"), UEdGraph::StaticClass(), SchemaClass);
			if (!TestNotNull(TEXT("the anim graph"), Graph) || !TestNotNull(TEXT("a node in it"), AddCollectGarbage(BP, Graph)))
			{
				return;
			}
			FN2CCaptureOptions Options;
			Options.Depth = 0;
			const TValueOrError<FN2CCapture, FString> Result = N2C::CaptureGraph(*Graph, Options);
			if (TestTrue(TEXT("the anim graph is captured"), Result.HasValue()) && TestEqual(TEXT("one graph"), Result.GetValue().Blueprint.Graphs.Num(), 1))
			{
				TestEqual(TEXT("graph_type"), StaticEnum<EN2CGraphType>()->GetNameStringByValue(
					static_cast<int64>(Result.GetValue().Blueprint.Graphs[0].GraphType)), FString(TEXT("Animation")));
			}
		});

		It(TEXT("never follows engine content: a StandardMacros ForEachLoop adds no graph at depth 2"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("ForEachLoop"));
			if (!TestNotNull(TEXT("the ForEachLoop case"), Case))
			{
				return;
			}
			FCaptureCase Deep = *Case;
			Deep.TranslationDepth = 2;
			const FCaptured Captured = Capture(Deep);
			TestEqual(TEXT("graphs captured"), Captured.Blueprint.Graphs.Num(), 1);
		});
	});

	Describe(TEXT("DataFlows"), [this]()
	{
		It(TEXT("lists every connected data input as the target of exactly one edge in every case"), [this]()
		{
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (Captured.Capture.IsValid())
				{
					ExpectLosslessDataFlows(Case.Name, Captured.Capture);
				}
			}
		});

		It(TEXT("gives an output feeding two inputs one entry listing both"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("FanOutActorLocation"));
			if (!TestNotNull(TEXT("FanOutActorLocation case"), Case))
			{
				return;
			}
			const FCaptured Captured = Capture(*Case);
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			const TArray<FString> Targets = DataTargetsOf(Captured.Capture, TEXT("K2_GetActorLocation"), TEXT("ReturnValue"));
			TestEqual(TEXT("GetActorLocation.ReturnValue targets"), FString::Join(Targets, TEXT(",")),
				FString::Join(TArray<FString>{ InputRef(Captured.Capture, TEXT("Conv_VectorToString"), TEXT("InVec")),
					InputRef(Captured.Capture, TEXT("K2_SetActorLocation"), TEXT("NewLocation")) }, TEXT(",")));
			TestEqual(TEXT("the N2C JSON version"), Captured.Capture->GetStringField(N2CJsonContract::VersionField),
				FString(N2CJsonContract::Version));
		});

		It(TEXT("follows every branch of a knot's fan-out"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("Knots"));
			if (!TestNotNull(TEXT("Knots case"), Case))
			{
				return;
			}
			const FCaptured Captured = Capture(*Case);
			if (Captured.Capture.IsValid())
			{
				// Value runs through two knots into Add.A and Add.B
				const TSharedPtr<FJsonObject> Graph = Captured.Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
				const TArray<TSharedPtr<FJsonValue>>& Nodes = Graph->GetArrayField(TEXT("nodes"));
				const FString Entry = Nodes[0]->AsObject()->GetStringField(TEXT("id"));
				const FString AddName = TEXT("Add_IntInt");
				TestEqual(TEXT("Value's targets"), FString::Join(DataTargetsOf(Captured.Capture, Entry, TEXT("Value")), TEXT(",")),
					InputRef(Captured.Capture, AddName, TEXT("A")) + TEXT(",") + InputRef(Captured.Capture, AddName, TEXT("B")));
			}
		});
	});

	// Card 0136: exec flows are recorded pin to pin (translation-core-2)
	Describe(TEXT("ExecFlows"), [this]()
	{
		It(TEXT("records every exec link once, from its exec output pin to its exec input pin, in every case"), [this]()
		{
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (Captured.Capture.IsValid())
				{
					ExpectPinQualifiedExecFlows(Case.Name, Captured.Capture);
					TestTrue(Case.Name + TEXT(": the capture validates"), Captured.Blueprint.IsValid());
				}
			}
		});

		It(TEXT("tells a Branch's True and False apart"), [this]()
		{
			ExpectExecLinks(TEXT("Branch"), {
				{ TEXT("OnBranch"), TEXT("then"), TEXT("Branch"), TEXT("execute") },
				{ TEXT("Branch"), TEXT("then"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("Branch"), TEXT("else"), TEXT("ControlScreensaver"), TEXT("execute") } });
		});

		It(TEXT("records True and False into one node as two entries"), [this]()
		{
			ExpectExecLinks(TEXT("BranchSameTarget"), {
				{ TEXT("BranchSame"), TEXT("then"), TEXT("Branch"), TEXT("execute") },
				{ TEXT("Branch"), TEXT("then"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("Branch"), TEXT("else"), TEXT("CollectGarbage"), TEXT("execute") } });
		});

		It(TEXT("tells a Gate's Enter, Open and Close inputs apart"), [this]()
		{
			ExpectExecLinks(TEXT("GateMultiExec"), {
				{ TEXT("OnEnter"), TEXT("then"), TEXT("Gate"), TEXT("Enter") },
				{ TEXT("OnOpen"), TEXT("then"), TEXT("Gate"), TEXT("Open") },
				{ TEXT("OnClose"), TEXT("then"), TEXT("Gate"), TEXT("Close") },
				{ TEXT("Gate"), TEXT("Exit"), TEXT("CollectGarbage"), TEXT("execute") } });
		});

		It(TEXT("tells a Sequence's then_0 and then_1 apart"), [this]()
		{
			ExpectExecLinks(TEXT("Sequence"), {
				{ TEXT("RunSequence"), TEXT("then"), TEXT("Sequence"), TEXT("execute") },
				{ TEXT("Sequence"), TEXT("then_0"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("Sequence"), TEXT("then_1"), TEXT("ControlScreensaver"), TEXT("execute") } });
		});

		It(TEXT("tells a ForEachLoop's Loop Body and Completed apart"), [this]()
		{
			ExpectExecLinks(TEXT("ForEachLoop"), {
				{ TEXT("Loop"), TEXT("then"), TEXT("ForEachLoop"), TEXT("Exec") },
				{ TEXT("ForEachLoop"), TEXT("LoopBody"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("ForEachLoop"), TEXT("Completed"), TEXT("ControlScreensaver"), TEXT("execute") } });
		});

		It(TEXT("records each wired case of a Switch on Int, two of them into one node"), [this]()
		{
			ExpectExecLinks(TEXT("SwitchOnInt"), {
				{ TEXT("SwitchInt"), TEXT("then"), TEXT("Switch"), TEXT("execute") },
				{ TEXT("Switch"), TEXT("0"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("Switch"), TEXT("1"), TEXT("ControlScreensaver"), TEXT("execute") },
				{ TEXT("Switch"), TEXT("Default"), TEXT("CollectGarbage"), TEXT("execute") } });
		});

		It(TEXT("tells a Timeline's Play and Reverse inputs apart"), [this]()
		{
			ExpectExecLinks(TEXT("TimelineMultiExec"), {
				{ TEXT("OnPlay"), TEXT("then"), TEXT("GoldenTimeline"), TEXT("Play") },
				{ TEXT("OnReverse"), TEXT("then"), TEXT("GoldenTimeline"), TEXT("Reverse") },
				{ TEXT("GoldenTimeline"), TEXT("Update"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("GoldenTimeline"), TEXT("Finished"), TEXT("ControlScreensaver"), TEXT("execute") } });
		});

		It(TEXT("validates an exec entry's pins: an exec output to an exec input of the named nodes"), [this]()
		{
			AddExpectedError(TEXT("xecution flow"), EAutomationExpectedErrorFlags::Contains, 0);
			struct FEntryCase
			{
				const TCHAR* Entry;
				bool bValid;
			};
			const FEntryCase EntryCases[] = {
				{ TEXT("N1.P1->N2.P3"), true },
				{ TEXT("N1->N2"), true },               // 1.x (and 2.0 builds before card 0136): node only
				{ TEXT("N1->N2->N2"), true },           // a 1.x chain
				{ TEXT("N1.P2->N2.P3"), false },        // P2 is a data pin
				{ TEXT("N2.P3->N1.P1"), false },        // input to output
				{ TEXT("N1.P1->N2.P5"), false },        // P5 is an output
				{ TEXT("N1.P1->N2.P9"), false },        // no such pin
				{ TEXT("N1.P1->N3.P3"), false },        // no such node
				{ TEXT("N1.P1->N2"), false },           // a pin ref and a node ID mixed
				{ TEXT("N1.P1->N2.P3->N2.P5"), false }, // a pin-qualified entry is one link
				{ TEXT("N1.->N2.P3"), false },
				{ TEXT("N1"), false } };
			for (const FEntryCase& EntryCase : EntryCases)
			{
				FN2CBlueprint Blueprint;
				if (!TestTrue(FString(TEXT("FromJson reads ")) + EntryCase.Entry,
					FN2CSerializer::FromJson(ExecFlowDocument(N2CJsonContract::Version, EntryCase.Entry), Blueprint))
					|| !TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1))
				{
					continue;
				}
				FString Error;
				TestEqual(FString(TEXT("the validator on ")) + EntryCase.Entry,
					FN2CBlueprintValidator().ValidateFlowReferences(Blueprint.Graphs[0], Error), EntryCase.bValid);
			}
		});
	});

	// Card 0135: N2C JSON 1.x (1.2.x and earlier 2.0 builds) reads into the v2 model
	// Card 0137: nodes Blueprint never runs (translation-core-18, translation-core-19, issue #13)
	Describe(TEXT("Liveness"), [this]()
	{
		It(TEXT("keeps both edges of issue #13's fan-out and marks the Format Text nobody reads unused"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("Issue13"));
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			const TSharedPtr<FJsonObject> NumberFormat = FindNode(Captured.Capture, TEXT("Conv_IntToString"));
			const TSharedPtr<FJsonObject> ToText = FindNode(Captured.Capture, TEXT("Conv_StringToText"));
			const TSharedPtr<FJsonObject> Format = FindNode(Captured.Capture, TEXT("Format Text"));
			if (!NumberFormat.IsValid() || !ToText.IsValid() || !Format.IsValid())
			{
				return;
			}
			TestEqual(TEXT("Number Format's Return Value feeds To Text and Format Text"),
				FString::Join(DataTargetsOf(Captured.Capture, TEXT("Conv_IntToString"), TEXT("ReturnValue")), TEXT(",")),
				InputRef(Captured.Capture, TEXT("Conv_StringToText"), TEXT("InString")) + TEXT(",")
					+ InputRef(Captured.Capture, TEXT("Format Text"), TEXT("effect")));
			ExpectLiveness(TEXT("Issue13"), Captured.Capture, {
				{ TEXT("Conv_IntToString"), TEXT("") },
				{ TEXT("Conv_StringToText"), TEXT("") },
				{ TEXT("Format Text"), TEXT("unused") } });
		});

		It(TEXT("captures a wired default event and drops the ghost ones"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("GhostEvents"));
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			TArray<FString> Members;
			for (const TSharedPtr<FJsonValue>& Node : Captured.Capture->GetArrayField(TEXT("graphs"))[0]->AsObject()->GetArrayField(TEXT("nodes")))
			{
				FString Member;
				Node->AsObject()->TryGetStringField(TEXT("member_name"), Member);
				Members.Add(Member);
			}
			TestEqual(TEXT("the captured nodes"), FString::Join(Members, TEXT(",")), FString(TEXT("ReceiveBeginPlay,CollectGarbage")));
			ExpectLiveness(TEXT("GhostEvents"), Captured.Capture, { { TEXT("ReceiveBeginPlay"), TEXT("") } });
		});

		It(TEXT("captures nothing from a new Actor Blueprint's untouched event graph"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CGolden_Untouched"), TEXT("/Game"));
			TArray<UK2Node*> Nodes;
			if (!TestTrue(TEXT("CollectNodesFromGraph succeeds"), Blueprint.EventGraph() != nullptr
				&& FN2CNodeCollector::Get().CollectNodesFromGraph(Blueprint.EventGraph(), Nodes)))
			{
				return;
			}
			TestEqual(TEXT("the collector still lists the three ghost events (find-nodes-in-graph sees them)"), Nodes.Num(), 3);
			AddExpectedMessage(TEXT("the graph holds only unwired default events"), ELogVerbosity::Warning,
				EAutomationExpectedMessageFlags::Contains, 1, false);
			const TValueOrError<FN2CCapture, FString> Result = N2C::CaptureNodes(Nodes);
			if (TestTrue(TEXT("the capture fails"), Result.HasError()))
			{
				TestTrue(TEXT("the error names the ghost nodes: ") + Result.GetError(), Result.GetError().Contains(TEXT("ghost")));
			}
		});

		It(TEXT("keeps the ghost events, marked disabled, when the graph-editing MCP tools ask for them"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CGolden_Untouched"), TEXT("/Game"));
			TArray<UK2Node*> Nodes;
			if (!TestTrue(TEXT("CollectNodesFromGraph succeeds"), Blueprint.EventGraph() != nullptr
				&& FN2CNodeCollector::Get().CollectNodesFromGraph(Blueprint.EventGraph(), Nodes)))
			{
				return;
			}
			FN2CCaptureOptions Options;
			Options.bIncludeGhostNodes = true;
			const TValueOrError<FN2CCapture, FString> Result = N2C::CaptureNodes(Nodes, Options);
			if (!TestTrue(TEXT("the capture succeeds"), Result.HasValue()))
			{
				return;
			}
			const FN2CGraph& Graph = Result.GetValue().Blueprint.Graphs[0];
			TArray<FString> Members;
			for (const FN2CNodeDefinition& Node : Graph.Nodes)
			{
				Members.Add(Node.MemberName + (Node.bDisabled ? TEXT(":disabled") : TEXT("")));
				TestFalse(Node.MemberName + TEXT(" is not unused or unreachable"), Node.bUnused || Node.bUnreachable);
			}
			TestEqual(TEXT("the ghost events"), FString::Join(Members, TEXT(",")),
				FString(TEXT("ReceiveBeginPlay:disabled,ReceiveActorBeginOverlap:disabled,ReceiveTick:disabled")));
			TestEqual(TEXT("one GUID entry per node"), Result.GetValue().NodeGuids.Num(), 3);
		});

		It(TEXT("marks disabled nodes, passes exec through a disabled Print String and stops at a disabled Branch"), [this]()
		{
			ExpectExecLinks(TEXT("DisabledNodes"), {
				{ TEXT("Disabled"), TEXT("then"), TEXT("CollectGarbage"), TEXT("execute") },
				{ TEXT("CollectGarbage"), TEXT("then"), TEXT("ControlScreensaver"), TEXT("execute") } });
			const FCaptured Captured = CaptureNamed(TEXT("DisabledNodes"));
			if (Captured.Capture.IsValid())
			{
				ExpectLiveness(TEXT("DisabledNodes"), Captured.Capture, {
					{ TEXT("CollectGarbage"), TEXT("") },
					{ TEXT("PrintString"), TEXT("disabled") },
					{ TEXT("ControlScreensaver"), TEXT("") },
					{ TEXT("Branch"), TEXT("disabled") },
					{ TEXT("ResetGamepadAssignments"), TEXT("unreachable") },
					{ TEXT("Conv_IntToString"), TEXT("unused") } });
			}
		});

		It(TEXT("marks an impure chain no entry reaches unreachable and the pure node only it reads unused"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("UnreachableChain"));
			if (Captured.Capture.IsValid())
			{
				ExpectLiveness(TEXT("UnreachableChain"), Captured.Capture, {
					{ TEXT("OnRun"), TEXT("") },
					{ TEXT("CollectGarbage"), TEXT("") },
					{ TEXT("ControlScreensaver"), TEXT("unreachable") },
					{ TEXT("ResetGamepadAssignments"), TEXT("unreachable") },
					{ TEXT("Not_PreBool"), TEXT("unused") } });
			}
		});

		It(TEXT("marks no node in the cases where every node runs"), [this]()
		{
			for (const FCaptureCase& Case : Cases)
			{
				if (Case.Name == TEXT("Issue13") || Case.Name == TEXT("DisabledNodes") || Case.Name == TEXT("UnreachableChain"))
				{
					continue;
				}
				const FCaptured Captured = Capture(Case);
				if (!Captured.Capture.IsValid())
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Graph : Captured.Capture->GetArrayField(TEXT("graphs")))
				{
					for (const TSharedPtr<FJsonValue>& Node : Graph->AsObject()->GetArrayField(TEXT("nodes")))
					{
						TestEqual(FString::Printf(TEXT("%s %s: liveness of %s"), *Case.Name, *Graph->AsObject()->GetStringField(TEXT("name")),
							*Node->AsObject()->GetStringField(TEXT("id"))), LivenessOf(Node->AsObject()), FString());
					}
				}
			}
		});

		It(TEXT("warns when a connected data input has no data flow into it"), [this]()
		{
			// N2's Count is connected but flows.data is empty: a lost edge or a hand-edited document
			FN2CBlueprint Read;
			const FString Document = ExecFlowDocument(TEXT("2.0.0"), TEXT("N1.P1->N2.P3"))
				.Replace(TEXT("{\"id\":\"P4\",\"name\":\"Count\",\"type\":\"Integer\"}"),
					TEXT("{\"id\":\"P4\",\"name\":\"Count\",\"type\":\"Integer\",\"connected\":true}"));
			if (!TestTrue(TEXT("the document marks Count connected"), Document.Contains(TEXT("\"Count\",\"type\":\"Integer\",\"connected\":true"))))
			{
				return;
			}
			AddExpectedMessage(TEXT("connected data input(s) N2.P4 have no data flow into them"), ELogVerbosity::Warning,
				EAutomationExpectedMessageFlags::Contains, 0, false);
			TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Document, Read));
			TestTrue(TEXT("it still validates"), Read.IsValid());
		});

		It(TEXT("reads and writes the liveness flags"), [this]()
		{
			FN2CBlueprint Read;
			const FString Document = ExecFlowDocument(TEXT("2.0.0"), TEXT("N1.P1->N2.P3"))
				.Replace(TEXT("\"member_name\":\"CollectGarbage\","), TEXT("\"member_name\":\"CollectGarbage\",\"disabled\":true,\"unreachable\":true,\"unused\":true,"));
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Document, Read)) || !TestEqual(TEXT("graphs"), Read.Graphs.Num(), 1)
				|| !TestEqual(TEXT("nodes"), Read.Graphs[0].Nodes.Num(), 2))
			{
				return;
			}
			const FN2CNodeDefinition& Node = Read.Graphs[0].Nodes[1];
			TestTrue(TEXT("disabled"), Node.bDisabled);
			TestTrue(TEXT("unreachable"), Node.bUnreachable);
			TestTrue(TEXT("unused"), Node.bUnused);
			const TSharedPtr<FJsonObject> Again = N2CTest::ParseJsonObject(FN2CSerializer::ToJson(Read, EN2CJsonStyle::Pretty));
			if (!TestTrue(TEXT("ToJson output parses"), Again.IsValid()))
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>& Nodes = Again->GetArrayField(TEXT("graphs"))[0]->AsObject()->GetArrayField(TEXT("nodes"));
			TestEqual(TEXT("written back"), LivenessOf(Nodes[1]->AsObject()), FString(TEXT("unused,unreachable,disabled")));
			TestEqual(TEXT("not written when false"), LivenessOf(Nodes[0]->AsObject()), FString());
		});
	});

	Describe(TEXT("V1Reader"), [this]()
	{
		// Written by ToJson before card 0135 (the goldens of FanOutData, Knots and UserEnum, and a struct and enum
		// document): FromJson then ToJson gives the same document with the current version
		for (const TCHAR* Name : { TEXT("FanOutData"), TEXT("Knots"), TEXT("UserEnum"), TEXT("StructsAndEnums") })
		{
			const FString FixtureName = Name;
			It(FString::Printf(TEXT("reads the v1 document %s into the v2 model unchanged"), Name), [this, FixtureName]()
			{
				FString Text;
				if (!TestTrue(TEXT("the v1 fixture loads"), FFileHelper::LoadFileToString(Text, *(V1Dir() / FixtureName + TEXT(".json")))))
				{
					return;
				}
				const TSharedPtr<FJsonObject> Original = N2CTest::ParseJsonObject(Text);
				if (!TestTrue(TEXT("the v1 fixture parses"), Original.IsValid()))
				{
					return;
				}
				TestEqual(TEXT("the fixture's version"), Original->GetStringField(N2CJsonContract::VersionField), FString(TEXT("1.0.0")));

				FN2CBlueprint Blueprint;
				if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)))
				{
					return;
				}
				TestEqual(TEXT("the model's version"), Blueprint.Version.Value, FString(N2CJsonContract::Version));
				TestTrue(TEXT("the model validates"), Blueprint.IsValid());
				int32 ExecPins = 0;
				for (const FN2CGraph& Graph : Blueprint.Graphs)
				{
					for (const FN2CNodeDefinition& Node : Graph.Nodes)
					{
						for (const TArray<FN2CPinDefinition>* Pins : { &Node.InputPins, &Node.OutputPins })
						{
							for (const FN2CPinDefinition& Pin : *Pins)
							{
								ExecPins += Pin.Type == EN2CPinType::Exec ? 1 : 0;
							}
						}
					}
					for (const TPair<FString, FN2CDataFlowTargets>& Flow : Graph.Flows.Data)
					{
						TestEqual(Flow.Key + TEXT(": a v1 data entry has one target"), Flow.Value.Targets.Num(), 1);
					}
				}
				TestTrue(TEXT("exec pins (no \"type\") are read as Exec"), ExecPins > 0);

				const FString Again = PrettyJson(N2CTest::ParseJsonObject(FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Pretty)));
				const FString Expected = AsCurrentVersion(Text);
				if (Again != Expected)
				{
					ReportDifference(FixtureName + TEXT(" v1 -> FromJson -> ToJson"), Expected, Again);
				}
			});
		}

		It(TEXT("reads a 1.2.3 translation's N2C_BP json"), [this]()
		{
			FString Text;
			if (!TestTrue(TEXT("the 1.2.3 fixture loads"), FFileHelper::LoadFileToString(Text, *(V1Dir() / TEXT("BP_Door_1.2.3.json")))))
			{
				return;
			}
			FN2CBlueprint Blueprint;
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)))
			{
				return;
			}
			TestEqual(TEXT("the model's version"), Blueprint.Version.Value, FString(N2CJsonContract::Version));
			TestTrue(TEXT("the model validates"), Blueprint.IsValid());
			TestEqual(TEXT("Blueprint name"), Blueprint.Metadata.Name, FString(TEXT("BP_Door")));
			if (TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1) && TestEqual(TEXT("nodes"), Blueprint.Graphs[0].Nodes.Num(), 2))
			{
				const FN2CNodeDefinition& Print = Blueprint.Graphs[0].Nodes[1];
				TestEqual(TEXT("Print String input pins"), Print.InputPins.Num(), 2);
				TestTrue(TEXT("Print String's first input is Exec"), Print.InputPins[0].Type == EN2CPinType::Exec);
				TestEqual(TEXT("In String's default"), Print.InputPins[1].DefaultValue, FString(TEXT("Door ready")));
				TestEqual(TEXT("execution flows"), FString::Join(Blueprint.Graphs[0].Flows.Execution, TEXT(",")), FString(TEXT("N1->N2")));
			}
		});

		It(TEXT("reads a fan-out written as an array and writes it back the same"), [this]()
		{
			const FString Text = TEXT("{\"version\":\"2.0.0\",\"metadata\":{\"name\":\"BP\",\"blueprint_type\":\"Normal\",")
				TEXT("\"blueprint_class\":\"BP\"},\"graphs\":[{\"name\":\"G\",\"graph_type\":\"Function\",\"nodes\":[")
				TEXT("{\"id\":\"N1\",\"type\":\"FunctionEntry\",\"title\":\"Entry\",\"input_pins\":[],\"output_pins\":[")
				TEXT("{\"id\":\"P1\",\"name\":\"Value\",\"type\":\"Integer\",\"connected\":true}]},")
				TEXT("{\"id\":\"N2\",\"type\":\"CallFunction\",\"title\":\"Add\",\"pure\":true,\"input_pins\":[")
				TEXT("{\"id\":\"P2\",\"name\":\"A\",\"type\":\"Integer\",\"connected\":true},")
				TEXT("{\"id\":\"P3\",\"name\":\"B\",\"type\":\"Integer\",\"connected\":true}],\"output_pins\":[]}],")
				TEXT("\"flows\":{\"execution\":[],\"data\":{\"N1.P1\":[\"N2.P2\",\"N2.P3\"]}}}],\"structs\":[],\"enums\":[]}");
			FN2CBlueprint Blueprint;
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)) || !TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1))
			{
				return;
			}
			const FN2CDataFlowTargets* Targets = Blueprint.Graphs[0].Flows.Data.Find(TEXT("N1.P1"));
			if (TestNotNull(TEXT("N1.P1's targets"), Targets))
			{
				TestEqual(TEXT("N1.P1's targets"), FString::Join(Targets->Targets, TEXT(",")), FString(TEXT("N2.P2,N2.P3")));
			}
			TestTrue(TEXT("the model validates"), Blueprint.IsValid());
			TestEqual(TEXT("ToJson writes it back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);
		});

		It(TEXT("reads a 2.0.0 document with node-only exec flows (2.0 builds before card 0136) and keeps them"), [this]()
		{
			const FString Text = ExecFlowDocument(TEXT("2.0.0"), TEXT("N1->N2"));
			FN2CBlueprint Blueprint;
			if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)) && TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1))
			{
				TestEqual(TEXT("execution flows"), FString::Join(Blueprint.Graphs[0].Flows.Execution, TEXT(",")), FString(TEXT("N1->N2")));
				TestTrue(TEXT("the model validates"), Blueprint.IsValid());
				TestEqual(TEXT("ToJson writes it back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);
			}
		});

		It(TEXT("reads pin-qualified exec flows and writes them back the same"), [this]()
		{
			const FString Text = ExecFlowDocument(N2CJsonContract::Version, TEXT("N1.P1->N2.P3"));
			FN2CBlueprint Blueprint;
			if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)) && TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 1))
			{
				TestEqual(TEXT("execution flows"), FString::Join(Blueprint.Graphs[0].Flows.Execution, TEXT(",")), FString(TEXT("N1.P1->N2.P3")));
				TestTrue(TEXT("the model validates"), Blueprint.IsValid());
				TestEqual(TEXT("ToJson writes it back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);
			}
		});

		It(TEXT("reads 1.x and 2.x and flags any other version"), [this]()
		{
			TestTrue(TEXT("1.0.0"), N2CJsonContract::IsReadable(TEXT("1.0.0")));
			TestTrue(TEXT("1.2"), N2CJsonContract::IsReadable(TEXT("1.2")));
			TestTrue(TEXT("2.0.0"), N2CJsonContract::IsReadable(TEXT("2.0.0")));
			TestTrue(TEXT("2.3.1"), N2CJsonContract::IsReadable(TEXT("2.3.1")));
			TestFalse(TEXT("3.0.0"), N2CJsonContract::IsReadable(TEXT("3.0.0")));
			TestFalse(TEXT("0.9"), N2CJsonContract::IsReadable(TEXT("0.9")));
			TestFalse(TEXT("empty"), N2CJsonContract::IsReadable(TEXT("")));
			TestFalse(TEXT("v2"), N2CJsonContract::IsReadable(TEXT("v2")));
			FN2CBlueprint Fresh;
			TestEqual(TEXT("a new model's version"), Fresh.Version.Value, FString(TEXT("2.0.0")));
		});
	});

	Describe(TEXT("Isolation"), [this]()
	{

		// Different depths, so a capture that read the other's depth or queue would not match its golden
		It(TEXT("gives each of two interleaved captures its single-run golden"), [this]()
		{
			const FCaptureCase* Outer = FindCase(TEXT("ShortestPath_Depth3"));
			const FCaptureCase* Inner = FindCase(TEXT("CallChain_Depth1"));
			if (!TestNotNull(TEXT("outer case"), Outer) || !TestNotNull(TEXT("inner case"), Inner))
			{
				return;
			}
			FCaptured InnerCaptured;
			bool bInnerRan = false;
			const FCaptured OuterCaptured = Capture(*Outer, [this, Inner, &InnerCaptured, &bInnerRan]()
			{
				bInnerRan = true;
				InnerCaptured = Capture(*Inner);
			});
			TestTrue(TEXT("the inner capture ran in the middle of the outer one"), bInnerRan);
			ExpectGolden(*Inner, InnerCaptured, TEXT(" (inner)"));
			ExpectGolden(*Outer, OuterCaptured, TEXT(" (outer)"));
		});

		// Capture A, capture B, then serialize A's result: A's JSON, not B's
		It(TEXT("keeps a capture's result when another graph is captured after it"), [this]()
		{
			const FCaptureCase* First = FindCase(TEXT("UserStruct"));
			const FCaptureCase* Second = FindCase(TEXT("NestedGraphs_Depth1"));
			if (!TestNotNull(TEXT("first case"), First) || !TestNotNull(TEXT("second case"), Second))
			{
				return;
			}
			FCaptured FirstCaptured = Capture(*First);
			const FCaptured SecondCaptured = Capture(*Second);
			Serialize(*First, FirstCaptured);
			ExpectGolden(*First, FirstCaptured, TEXT(" (first)"));
			ExpectGolden(*Second, SecondCaptured, TEXT(" (second)"));
		});
	});

	// Card 0134: IDs are allocated per node and pin object, graphs are deduplicated by identity
	Describe(TEXT("Identity"), [this]()
	{
		It(TEXT("gives a pasted node's pins IDs of their own and the capture validates"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("CopyPaste"));
			if (TestNotNull(TEXT("CopyPaste case"), Case))
			{
				const FCaptured Captured = Capture(*Case);
				ExpectConsistentIDs(TEXT("CopyPaste"), Captured.Result);
				TestEqual(TEXT("CopyPaste graphs"), Captured.Result.Blueprint.Graphs.Num(), 1);
			}
		});

		It(TEXT("captures a function that calls itself once"), [this]()
		{
			for (const TCHAR* Name : { TEXT("Recursion_Depth1"), TEXT("Recursion_Depth2") })
			{
				const FCaptureCase* Case = FindCase(Name);
				if (!TestNotNull(Name, Case))
				{
					continue;
				}
				const FCaptured Captured = Capture(*Case);
				ExpectConsistentIDs(Name, Captured.Result);
				int32 RootGraphs = 0;
				for (const FN2CGraph& Graph : Captured.Result.Blueprint.Graphs)
				{
					RootGraphs += Graph.Name == TEXT("Root") ? 1 : 0;
				}
				TestEqual(FString(Name) + TEXT(": Root graphs"), RootGraphs, 1);
			}
		});

		It(TEXT("captures same-named functions of two Blueprints, each with its owner"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("SameNameOwners"));
			if (!TestNotNull(TEXT("SameNameOwners case"), Case))
			{
				return;
			}
			const FCaptured Captured = Capture(*Case);
			ExpectConsistentIDs(TEXT("SameNameOwners"), Captured.Result);
			TArray<FString> InitOwners;
			for (const FN2CGraph& Graph : Captured.Result.Blueprint.Graphs)
			{
				if (Graph.Name == TEXT("Init"))
				{
					InitOwners.Add(Graph.OwnerBlueprint);
				}
				else
				{
					TestTrue(Graph.Name + TEXT(" (the captured Blueprint's own) has no owner_blueprint"), Graph.OwnerBlueprint.IsEmpty());
				}
			}
			InitOwners.Sort();
			TestEqual(TEXT("Init graphs and their owners"), FString::Join(InitOwners, TEXT("|")),
				FString(TEXT("|BP_N2CGolden_SameNameOwners_Other")));
		});

		It(TEXT("gives the same IDs when an unchanged graph is captured twice"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("ShortestPath_Depth3"));
			if (!TestNotNull(TEXT("ShortestPath_Depth3 case"), Case))
			{
				return;
			}
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CGolden_Twice"), TEXT("/Game"));
			UEdGraph* Graph = Blueprint.GetBlueprint() ? Case->Build(Blueprint) : nullptr;
			TArray<UK2Node*> Nodes;
			if (!TestNotNull(TEXT("graph"), Graph) || !TestTrue(TEXT("collect"), FN2CNodeCollector::Get().CollectNodesFromGraph(Graph, Nodes)))
			{
				return;
			}
			FN2CCaptureOptions Options;
			Options.Depth = Case->TranslationDepth.Get(0);
			const TValueOrError<FN2CCapture, FString> First = N2C::CaptureNodes(Nodes, Options);
			const TValueOrError<FN2CCapture, FString> Second = N2C::CaptureNodes(Nodes, Options);
			if (TestTrue(TEXT("both captures succeed"), First.HasValue() && Second.HasValue()))
			{
				ExpectConsistentIDs(TEXT("first capture"), First.GetValue());
				const FString FirstJson = FN2CSerializer::ToJson(First.GetValue(), EN2CJsonStyle::Pretty, EN2CJsonGuids::Table);
				const FString SecondJson = FN2CSerializer::ToJson(Second.GetValue(), EN2CJsonStyle::Pretty, EN2CJsonGuids::Table);
				if (FirstJson != SecondJson)
				{
					ReportDifference(TEXT("a second capture of the same graph (with the guids table)"), FirstJson, SecondJson);
				}
			}
		});

		It(TEXT("gives get-focused-blueprint one GUID entry per node and per pin"), [this]()
		{
			const FCaptureCase* Case = FindCase(TEXT("CopyPaste"));
			if (!TestNotNull(TEXT("CopyPaste case"), Case))
			{
				return;
			}
			const FCaptured Captured = Capture(*Case);
			const FN2CCapture& Result = Captured.Result;
			const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(FN2CMcpGetFocusedBlueprintTool::BuildResultJson(Result));
			const TSharedPtr<FJsonObject>* Guids = nullptr;
			if (!TestTrue(TEXT("the result parses"), Json.IsValid()) || !TestTrue(TEXT("the result has a guids table"), Json->TryGetObjectField(TEXT("guids"), Guids)))
			{
				return;
			}
			const TSharedPtr<FJsonObject> NodesTable = (*Guids)->GetObjectField(TEXT("nodes"));
			const TSharedPtr<FJsonObject> PinsTable = (*Guids)->GetObjectField(TEXT("pins"));
			TestEqual(TEXT("guids.nodes entries"), NodesTable->Values.Num(), Result.NodeGuids.Num());
			TestEqual(TEXT("guids.pins entries"), PinsTable->Values.Num(), Result.Pins.Num());
			TestEqual(TEXT("guids.graphs entries"), (*Guids)->GetArrayField(TEXT("graphs")).Num(), Result.Blueprint.Graphs.Num());

			// Every pin's inline ids.guid is its own pin's PinId, and the table names its node
			int32 PinsChecked = 0;
			for (const TSharedPtr<FJsonValue>& GraphValue : Json->GetArrayField(TEXT("graphs")))
			{
				for (const TSharedPtr<FJsonValue>& NodeValue : GraphValue->AsObject()->GetArrayField(TEXT("nodes")))
				{
					const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
					const FString NodeID = Node->GetObjectField(TEXT("ids"))->GetStringField(TEXT("short"));
					TestEqual(NodeID + TEXT(" ids.guid"), Node->GetObjectField(TEXT("ids"))->GetStringField(TEXT("guid")),
						Result.NodeGuids.FindRef(NodeID).ToString(EGuidFormats::DigitsWithHyphens));
					for (const TCHAR* Field : { TEXT("input_pins"), TEXT("output_pins") })
					{
						for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(Field))
						{
							const TSharedPtr<FJsonObject> Ids = PinValue->AsObject()->GetObjectField(TEXT("ids"));
							const FString PinID = Ids->GetStringField(TEXT("short"));
							const FN2CCapturedPin* Entry = Result.Pins.Find(PinID);
							const TSharedPtr<FJsonObject>* Row = nullptr;
							if (TestNotNull(PinID + TEXT(" in the capture's Pins"), Entry)
								&& TestTrue(PinID + TEXT(" in guids.pins"), PinsTable->TryGetObjectField(PinID, Row)))
							{
								const FString PinGuid = Entry->PinGuid.ToString(EGuidFormats::DigitsWithHyphens);
								TestEqual(PinID + TEXT(" ids.guid"), Ids->GetStringField(TEXT("guid")), PinGuid);
								TestEqual(PinID + TEXT(" guids.pins guid"), (*Row)->GetStringField(TEXT("guid")), PinGuid);
								TestEqual(PinID + TEXT(" guids.pins node"), (*Row)->GetStringField(TEXT("node")), NodeID);
								++PinsChecked;
							}
						}
					}
				}
			}
			TestEqual(TEXT("pins checked"), PinsChecked, Result.Pins.Num());
		});

		// Card 0505: the agent document writes each "id" as "ids" {"short", "guid"}; the reader once skipped every node
		// of it (an Error log line each) and still read the document
		It(TEXT("reads the agent document of every case back with every node and pin, without a warning"), [this]()
		{
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (!Captured.Capture.IsValid())
				{
					continue;
				}
				const FN2CBlueprint& Exported = Captured.Result.Blueprint;
				FN2CBlueprint Read;
				TArray<FN2CJsonReadWarning> Warnings;
				if (!TestTrue(Case.Name + TEXT(": FromJson reads the agent document"),
					FN2CSerializer::FromJson(N2CMcpBlueprintExport::ToResultJson(Captured.Result, /*bCompact*/ true), Read, Warnings)))
				{
					continue;
				}
				for (const FN2CJsonReadWarning& Warning : Warnings)
				{
					AddError(FString::Printf(TEXT("%s: read warning %s at %s"), *Case.Name, *Warning.Code, *Warning.Path));
				}
				int32 ExportedNodes = 0;
				int32 ReadNodes = 0;
				for (const FN2CGraph& Graph : Exported.Graphs)
				{
					ExportedNodes += Graph.Nodes.Num();
				}
				for (const FN2CGraph& Graph : Read.Graphs)
				{
					ReadNodes += Graph.Nodes.Num();
				}
				TestTrue(Case.Name + TEXT(": the case has nodes"), ExportedNodes > 0);
				TestEqual(Case.Name + TEXT(": nodes read back"), ReadNodes, ExportedNodes);
				// Every node, pin, ID and flow: the read model is the captured one (the GUIDs are not part of the model)
				const FString ExportedJson = FN2CSerializer::ToJson(Exported, EN2CJsonStyle::Pretty);
				const FString ReadJson = FN2CSerializer::ToJson(Read, EN2CJsonStyle::Pretty);
				if (ReadJson != ExportedJson)
				{
					ReportDifference(Case.Name + TEXT(": the agent document read back"), ExportedJson, ReadJson);
				}
			}
		});
	});

	// Card 0138: names are stable identifiers; display text is a hint (gap-5, translation-core-22, translation-core-28)
	Describe(TEXT("StableNames"), [this]()
	{
		It(TEXT("keeps member names as declared and names the declaring Blueprint as member_parent"), [this]()
		{
			const FCaptured Captured = CaptureNamed(TEXT("StableNames"));
			if (!Captured.Capture.IsValid())
			{
				return;
			}
			const TSharedPtr<FJsonObject> Health = FindNode(Captured.Capture, TEXT("Health"));
			const TSharedPtr<FJsonObject> Temperature = FindNode(Captured.Capture, TEXT("Temperature_C"));
			const TSharedPtr<FJsonObject> Convert = FindNode(Captured.Capture, TEXT("Convert_C"));
			if (!Health.IsValid() || !Temperature.IsValid() || !Convert.IsValid())
			{
				return;
			}
			TestEqual(TEXT("Health's member_parent (the Blueprint that declares it, not its type)"),
				Health->GetStringField(TEXT("member_parent")), FString(TEXT("BP_N2CGolden_Enemy")));
			TestEqual(TEXT("Temperature_C's member_parent"), Temperature->GetStringField(TEXT("member_parent")),
				FString(TEXT("BP_N2CGolden_StableNames")));
			TestEqual(TEXT("Convert_C's member_parent"), Convert->GetStringField(TEXT("member_parent")),
				FString(TEXT("BP_N2CGolden_StableNames")));
			TestEqual(TEXT("Convert_C's class"), Convert->GetStringField(N2CJsonContract::NodeClassField), FString(TEXT("K2Node_CallFunction")));
			TestEqual(TEXT("Health's class"), Health->GetStringField(N2CJsonContract::NodeClassField), FString(TEXT("K2Node_VariableGet")));
			for (const TCHAR* Exec : { TEXT("execute"), TEXT("then") })
			{
				TestFalse(FString(TEXT("Convert_C's exec pin ")) + Exec, ExecPinRef(Captured.Capture, TEXT("Convert_C"), Exec,
					FCString::Strcmp(Exec, TEXT("then")) == 0).IsEmpty());
			}
		});

		It(TEXT("gives every node a class and every pin its PinName in every case"), [this]()
		{
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (!Captured.Capture.IsValid())
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Graph : Captured.Capture->GetArrayField(TEXT("graphs")))
				{
					for (const TSharedPtr<FJsonValue>& NodeValue : Graph->AsObject()->GetArrayField(TEXT("nodes")))
					{
						const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
						const FString What = Case.Name + TEXT(" ") + Node->GetStringField(TEXT("id"));
						FString Class;
						TestTrue(What + TEXT(" has a K2Node class"), Node->TryGetStringField(N2CJsonContract::NodeClassField, Class) && Class.StartsWith(TEXT("K2Node_")));
						TestFalse(What + TEXT(" has no \"name\" (its title is a hint)"), Node->HasField(N2CJsonContract::NodeNameFieldV1));
						FString Title;
						FString Member;
						if (Node->TryGetStringField(N2CJsonContract::NodeTitleField, Title) && Node->TryGetStringField(TEXT("member_name"), Member))
						{
							TestFalse(What + TEXT("'s title says more than its member_name"), N2CJsonContract::IsRedundantHint(Title, Member));
						}
						for (const TCHAR* Pins : { TEXT("input_pins"), TEXT("output_pins") })
						{
							for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(Pins))
							{
								const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
								const FString Name = Pin->GetStringField(TEXT("name"));
								TestFalse(What + TEXT(".") + Pin->GetStringField(TEXT("id")) + TEXT(" has a name"), Name.IsEmpty());
								FString Label;
								if (Pin->TryGetStringField(N2CJsonContract::PinLabelField, Label))
								{
									TestFalse(What + TEXT(".") + Name + TEXT("'s label says more than its name"), N2CJsonContract::IsRedundantHint(Label, Name));
								}
							}
						}
					}
				}
			}
		});

		It(TEXT("captures the same JSON in English and Japanese, and the same identifiers with friendly names on and off"), [this]()
		{
			TMap<FString, FString> English;
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (Captured.Capture.IsValid())
				{
					English.Add(Case.Name, Captured.Json);
				}
			}

			FInternationalization& I18N = FInternationalization::Get();
			const FString SavedLanguage = I18N.GetCurrentLanguage()->GetName();
			const FString SavedLocale = I18N.GetCurrentLocale()->GetName();
			UEditorStyleSettings* Style = GetMutableDefault<UEditorStyleSettings>();
			const bool bSavedFriendlyNames = Style->bShowFriendlyNames;
			ON_SCOPE_EXIT
			{
				I18N.SetCurrentLanguage(SavedLanguage);
				I18N.SetCurrentLocale(SavedLocale);
				Style->bShowFriendlyNames = bSavedFriendlyNames;
			};

			// Japanese: the editor's own localization must really apply, or this check would prove nothing
			if (!TestTrue(TEXT("the editor switches to Japanese"), I18N.SetCurrentCulture(TEXT("ja")))
				|| !TestEqual(TEXT("the current language"), I18N.GetCurrentLanguage()->GetName(), FString(TEXT("ja"))))
			{
				return;
			}
			{
				N2CTest::FTestBlueprint Probe(TEXT("BP_N2CGolden_LocaleProbe"), TEXT("/Game"));
				UEdGraph* ProbeGraph = Probe.AddFunction(TEXT("Probe"));
				UK2Node_FunctionResult* ProbeResult = Probe.AddFunctionOutput(ProbeGraph, TEXT("Out"), N2CTest::FTestBlueprint::IntPinType());
				UK2Node_IfThenElse* ProbeBranch = Probe.AddBranch(ProbeGraph);
				bool bLocalized = false;
				for (const UEdGraphNode* Node : { static_cast<const UEdGraphNode*>(ProbeResult), static_cast<const UEdGraphNode*>(ProbeBranch) })
				{
					const FText Title = Node ? Node->GetNodeTitle(ENodeTitleType::MenuTitle) : FText::GetEmpty();
					bLocalized |= Title.ToString() != Title.BuildSourceString();
				}
				if (!TestTrue(TEXT("node titles are shown in Japanese (the engine's Editor/ja localization is loaded)"), bLocalized))
				{
					return;
				}

				// find-nodes-in-graph and the Python bridge match the English title, the class and the member name
				const UK2Node_CallFunction* ProbeCall = Probe.AddCall(ProbeGraph, UKismetSystemLibrary::StaticClass(),
					GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
				const FString SearchText = N2CStableNames::NodeSearchText(ProbeCall);
				for (const TCHAR* Term : { TEXT("Collect Garbage"), TEXT("CollectGarbage"), TEXT("K2Node_CallFunction") })
				{
					TestTrue(FString::Printf(TEXT("a keyword search finds Collect Garbage by '%s' in Japanese (%s)"), Term, *SearchText),
						SearchText.Contains(Term, ESearchCase::IgnoreCase));
				}
			}
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (Captured.Capture.IsValid() && English.Contains(Case.Name) && Captured.Json != English[Case.Name])
				{
					ReportDifference(Case.Name + TEXT(" in Japanese"), English[Case.Name], Captured.Json);
				}
			}
			I18N.SetCurrentLanguage(SavedLanguage);
			I18N.SetCurrentLocale(SavedLocale);

			// Friendly names flipped: identifiers are the same; only the title and label hints may differ
			Style->bShowFriendlyNames = !bSavedFriendlyNames;
			for (const FCaptureCase& Case : Cases)
			{
				const FCaptured Captured = Capture(Case);
				if (Captured.Capture.IsValid() && English.Contains(Case.Name))
				{
					const FString Expected = WithoutHints(English[Case.Name]);
					const FString Actual = WithoutHints(Captured.Json);
					if (Actual != Expected)
					{
						ReportDifference(Case.Name + (bSavedFriendlyNames ? TEXT(" with friendly names off") : TEXT(" with friendly names on")), Expected, Actual);
					}
				}
			}
		});
	});

	Describe(TEXT("Hygiene"), [this]()
	{
		It(TEXT("puts Max Translation Depth back after a case that sets it"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			const int32 Before = Settings->TranslationDepth;
			const FCaptureCase* DepthCase = Cases.FindByPredicate([Before](const FCaptureCase& Case)
			{
				return Case.TranslationDepth.IsSet() && Case.TranslationDepth.GetValue() != Before;
			});
			if (TestNotNull(TEXT("a case with another depth"), DepthCase))
			{
				Capture(*DepthCase);
				TestEqual(TEXT("TranslationDepth after the capture"), Settings->TranslationDepth, Before);
			}
		});

		It(TEXT("leaves no test package, user type or input action once garbage is collected"), [this]()
		{
			FString PackageName;
			{
				N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CGolden_Hygiene"), TEXT("/Game"));
				UUserDefinedEnum* Enum = Blueprint.AddUserEnum(TEXT("E_Hygiene"), { TEXT("One") });
				Blueprint.AddUserStruct(TEXT("S_Hygiene"), { { TEXT("Value"), N2CTest::FTestBlueprint::EnumPinType(Enum) } });
				Blueprint.AddEnhancedInputAction(Blueprint.EventGraph(), TEXT("IA_Hygiene"));
				for (const FString& Error : Blueprint.Errors())
				{
					AddError(Error);
				}
				PackageName = Blueprint.GetPackagePath();
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestNull(TEXT("the test package after garbage collection"), FindObject<UPackage>(nullptr, *PackageName));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
