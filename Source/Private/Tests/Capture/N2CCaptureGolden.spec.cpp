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
#include "Internationalization/Regex.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/StructureEditorUtils.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpGetFocusedBlueprintTool.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CJsonContract.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

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
	 * Replaces what changes from run to run with stable tokens: the case's transient package path (<PACKAGE>), any
	 * GUID (<GUID>), and the UObject index in the Var_<graph>_<index> name FN2CVariableProcessor makes up for a node
	 * without a variable reference (<OBJECT_INDEX>; translation-core-4, card 0234)
	 */
	static FString Normalise(const FString& Text, const FString& PackagePath)
	{
		FString Out = PackagePath.IsEmpty() ? Text : Text.Replace(*PackagePath, TEXT("<PACKAGE>"), ESearchCase::CaseSensitive);
		Out = ReplaceMatches(Out, TEXT("[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}"), TEXT("<GUID>"));
		return ReplaceMatches(Out, TEXT("\"Var_[A-Za-z0-9_]+_([0-9]+)\""), TEXT("<OBJECT_INDEX>"));
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

	/** The data targets of the output pin named OutputPin on the node named NodeName in the capture's first graph */
	TArray<FString> DataTargetsOf(const TSharedPtr<FJsonObject>& Capture, const FString& NodeName, const FString& OutputPin)
	{
		TArray<FString> Result;
		const TSharedPtr<FJsonObject> Graph = Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
		FString SourceRef;
		for (const TSharedPtr<FJsonValue>& NodeValue : Graph->GetArrayField(TEXT("nodes")))
		{
			const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
			if (Node->GetStringField(TEXT("name")) != NodeName)
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
			if (Node->GetStringField(TEXT("name")) != NodeName)
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

	/** A document with its version replaced, pretty printed: a v1 document as ToJson writes it after FromJson */
	static FString WithVersion(const FString& Text, const FString& Version)
	{
		const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(Text);
		if (!Object.IsValid())
		{
			return FString();
		}
		Object->SetStringField(N2CJsonContract::VersionField, Version);
		return PrettyJson(Object);
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

	UK2Node_CallFunction* AddMath(FTestBlueprint& BP, UEdGraph* Graph, const FName Function)
	{
		return BP.AddCall(Graph, UKismetMathLibrary::StaticClass(), Function);
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

	const FKnownDefect GhostEvents{ TEXT("translation-core-18, translation-core-19 (issue #13)"), TEXT("0137"),
		TEXT("The grayed-out default events CreateBlueprint places (BeginPlay, ActorBeginOverlap, Tick) are captured as "
			"live events (N1-N3), and unreachable nodes are not marked") };
	const FKnownDefect ExecNodeOnly{ TEXT("translation-core-2"), TEXT("0136"),
		TEXT("Execution flows are 'N1->N2' node pairs: which exec output and which exec input a link uses is lost, and "
			"two outputs into one node collapse to one entry") };
	const FKnownDefect DisplayNames{ TEXT("gap-5"), TEXT("0138"),
		TEXT("Node and pin names are display text (menu titles, friendly pin names, blank exec pin names), not stable "
			"identifiers, so they change with editor language and settings") };
	const FKnownDefect CustomEventNone{ TEXT("translation-core-16"), TEXT("0239"),
		TEXT("Custom events get member_name 'None' (the event processor's custom-event branch is unreachable)") };
	const FKnownDefect SwitchName{ TEXT("translation-core-32"), TEXT("0239"),
		TEXT("Switch nodes are named 'Switch', losing the switched type") };
	const FKnownDefect BreakAsVariable{ TEXT("translation-core-4"), TEXT("0234"),
		TEXT("Break Vector (a K2Node_BreakStruct) is captured as a Variable named 'None', with the first member's type "
			"as member_parent and member_name Var_<graph>_<UObject index>, which changes every session (normalised "
			"to <OBJECT_INDEX> here)") };
	const FKnownDefect RepeatMissingName{ TEXT("translation-core-24"), TEXT("0236"),
		TEXT("A user struct or enum met a second time comes back empty and the validator logs ERROR 'Missing name' "
			"(expected errors in this case)") };

	Cases.Add({ TEXT("FanOutData"),
		TEXT("Function FanOut(Value): Value feeds Add.A, Add.B and Multiply.A; Add feeds Multiply.B; Multiply is the result."),
		{ DisplayNames },
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
		{ DisplayNames },
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
		{ DisplayNames },
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
		{ GhostEvents, ExecNodeOnly, DisplayNames, CustomEventNone },
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

	Cases.Add({ TEXT("SwitchOnInt"),
		TEXT("Function SwitchInt(Selection): Switch on Int with cases 0 and 1 and Default; 0 and Default both run "
			"CollectGarbage, 1 runs ControlScreensaver."),
		{ ExecNodeOnly, DisplayNames, SwitchName },
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
		{ ExecNodeOnly, DisplayNames, SwitchName },
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
		{ BreakAsVariable, DisplayNames },
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
		{ GhostEvents, ExecNodeOnly, DisplayNames, CustomEventNone },
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
		{ GhostEvents, ExecNodeOnly, DisplayNames, CustomEventNone },
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

	// Card 0127: user types, recursion and depth, copy-paste, nested graphs, Enhanced Input

	Cases.Add({ TEXT("UserStruct"),
		TEXT("Function UseItem(Item: S_GoldenItem): S_GoldenItem is a user struct {Owner: Actor object, Count: int, "
			"Level: user enum E_GoldenLevel {Low, High}}. Break S_GoldenItem feeds Count to the Count result and "
			"Owner, Count and Level to Make S_GoldenItem, which is the Copy result."),
		{ { TEXT("translation-core-8"), TEXT("0236"),
			TEXT("S_GoldenItem is missing from 'structs': its object member Owner gets no type name, the validator rejects "
				"the struct (ERROR 'Member Owner requires a type name') and it is dropped without a warning") },
		  RepeatMissingName,
		  { TEXT("translation-core-4"), TEXT("0234"),
			TEXT("Break S_GoldenItem is captured as a Variable named 'None' with member_parent object/Actor (the first "
				"member's type) and member_name Var_<graph>_<UObject index> (normalised to <OBJECT_INDEX>)") },
		  DisplayNames },
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
		  RepeatMissingName, ExecNodeOnly, DisplayNames, SwitchName },
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

	for (const int32 Depth : { 1, 2 })
	{
		FCaptureCase& Recursion = Cases.Add_GetRef({ FString::Printf(TEXT("Recursion_Depth%d"), Depth),
			FString::Printf(TEXT("Max Translation Depth %d. Function Root(Count) calls Child, then itself with Count; Child "
				"calls Grandchild; Grandchild calls Root. Captured from Root."), Depth),
			{ ExecNodeOnly, DisplayNames },
			&BuildRecursion });
		Recursion.TranslationDepth = Depth;
	}

	// Max Translation Depth counts calls from the captured graph (translation-core-3, card 0130)
	for (const int32 Depth : { 1, 2 })
	{
		FCaptureCase& Chain = Cases.Add_GetRef({ FString::Printf(TEXT("CallChain_Depth%d"), Depth),
			FString::Printf(TEXT("Max Translation Depth %d. Function A calls B, B calls C, C calls D. Captured from A, so "
				"depth 1 adds B and depth 2 adds B and C; D is never captured."), Depth),
			{ ExecNodeOnly, DisplayNames },
			&BuildChain });
		Chain.TranslationDepth = Depth;
	}

	Cases.Add({ TEXT("ShortestPath_Depth3"),
		TEXT("Max Translation Depth 3. Root calls Near, then Far. Near calls Shared; Far calls Mid, Mid calls Shared; "
			"Shared calls Leaf, Leaf calls Beyond. Captured from Root. Shared is 2 calls away through Near (3 through "
			"Far), so Leaf (3) is captured and Beyond (4) is not."),
		{ ExecNodeOnly, DisplayNames },
		&BuildShortestPath });
	Cases.Last().TranslationDepth = 3;

	Cases.Add({ TEXT("CopyPaste"),
		TEXT("Function CopyPaste(Enabled): the entry runs ControlScreensaver (bAllowScreenSaver from Enabled), which "
			"runs a copy-pasted duplicate of itself (pasted as the editor does: new node GUID, pin IDs kept), also fed "
			"from Enabled."),
		{ ExecNodeOnly, DisplayNames },
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
		{ ExecNodeOnly, DisplayNames },
		&BuildSameNameOwners });
	Cases.Last().TranslationDepth = 2;

	for (const int32 Depth : { 0, 1 })
	{
		FCaptureCase& Nested = Cases.Add_GetRef({ FString::Printf(TEXT("NestedGraphs_Depth%d"), Depth),
			FString::Printf(TEXT("Max Translation Depth %d. Function Nested(): the entry runs a collapsed graph "
				"GoldenCollapsed (In -> CollectGarbage -> Out), whose Out runs the Blueprint's own macro LocalSteps "
				"(In -> ControlScreensaver -> Out)."), Depth),
			{ { TEXT("imp-translation-core-5"), TEXT("0235"),
				TEXT("Collapsed graphs and the Blueprint's own macros only count as nested graphs within the depth limit: "
					"at depth 0 (the default) GoldenCollapsed and LocalSteps are not captured and only the opaque "
					"Composite and MacroInstance nodes remain") },
			  { TEXT("imp-translation-core-4"), TEXT("0235"),
				TEXT("The Composite and MacroInstance nodes do not name the graph captured for them (the Composite has no "
					"member_parent or member_name)") },
			  ExecNodeOnly, DisplayNames },
			&BuildNested });
		Nested.TranslationDepth = Depth;
		if (Depth > 0)
		{
			Nested.KnownDefects.Insert({ TEXT("translation-core-20"), TEXT("0235"),
				TEXT("A user macro is classed as a Macro graph only when its name contains 'macro': LocalSteps comes out "
					"as an EventGraph") }, 2);
		}
	}

	Cases.Add({ TEXT("EnhancedInputEvent"),
		TEXT("Event graph: the Enhanced Input action event for a transient UInputAction IA_GoldenJump (Boolean); "
			"Triggered runs CollectGarbage, Completed runs ControlScreensaver."),
		{ { TEXT("translation-core-15"), TEXT("0234"),
			TEXT("The Enhanced Input action event is typed CallFunction, with no member_parent or member_name, instead "
				"of an event") },
		  GhostEvents, ExecNodeOnly, DisplayNames },
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

	// Card 0135: data edges are lossless (translation-core-1, PR #7, issue #13)
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
			const TSharedPtr<FJsonObject> Graph = Captured.Capture->GetArrayField(TEXT("graphs"))[0]->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Nodes = Graph->GetArrayField(TEXT("nodes"));
			auto NodeNamed = [&Nodes](const FString& MemberName) -> FString
			{
				for (const TSharedPtr<FJsonValue>& Node : Nodes)
				{
					FString Member;
					if (Node->AsObject()->TryGetStringField(TEXT("member_name"), Member) && Member == MemberName)
					{
						return Node->AsObject()->GetStringField(TEXT("name"));
					}
				}
				return FString();
			};
			const FString GetLocation = NodeNamed(TEXT("K2_GetActorLocation"));
			const FString ToText = NodeNamed(TEXT("Conv_VectorToString"));
			const FString SetLocation = NodeNamed(TEXT("K2_SetActorLocation"));
			const TArray<FString> Targets = DataTargetsOf(Captured.Capture, GetLocation, TEXT("Return Value"));
			TestEqual(TEXT("GetActorLocation.ReturnValue targets"), FString::Join(Targets, TEXT(",")),
				FString::Join(TArray<FString>{ InputRef(Captured.Capture, ToText, TEXT("In Vec")),
					InputRef(Captured.Capture, SetLocation, TEXT("New Location")) }, TEXT(",")));
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
				const FString Entry = Nodes[0]->AsObject()->GetStringField(TEXT("name"));
				FString AddName;
				for (const TSharedPtr<FJsonValue>& Node : Nodes)
				{
					FString Member;
					if (Node->AsObject()->TryGetStringField(TEXT("member_name"), Member) && Member == TEXT("Add_IntInt"))
					{
						AddName = Node->AsObject()->GetStringField(TEXT("name"));
					}
				}
				TestEqual(TEXT("Value's targets"), FString::Join(DataTargetsOf(Captured.Capture, Entry, TEXT("Value")), TEXT(",")),
					InputRef(Captured.Capture, AddName, TEXT("A")) + TEXT(",") + InputRef(Captured.Capture, AddName, TEXT("B")));
			}
		});
	});

	// Card 0135: N2C JSON 1.x (1.2.x and earlier 2.0 builds) reads into the v2 model
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
				const FString Expected = WithVersion(Text, N2CJsonContract::Version);
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
				TEXT("{\"id\":\"N1\",\"type\":\"FunctionEntry\",\"name\":\"Entry\",\"input_pins\":[],\"output_pins\":[")
				TEXT("{\"id\":\"P1\",\"name\":\"Value\",\"type\":\"Integer\",\"connected\":true}]},")
				TEXT("{\"id\":\"N2\",\"type\":\"CallFunction\",\"name\":\"Add\",\"pure\":true,\"input_pins\":[")
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
