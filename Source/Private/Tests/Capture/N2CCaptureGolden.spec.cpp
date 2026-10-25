// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CNodeCollector.h"
#include "Core/N2CNodeTranslator.h"
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
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
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
 * (FN2CNodeCollector::CollectNodesFromGraph, FN2CNodeTranslator::GenerateN2CStruct, FN2CSerializer::ToJson) and
 * compares the N2C JSON v1 output with Goldens/Capture/<case>.json. The transient package path and any GUID are
 * replaced with <PACKAGE> and <GUID> first.
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

	FCaptured Capture(const FCaptureCase& Case)
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
		FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
		if (!TestTrue(Case.Name + TEXT(": GenerateN2CStruct succeeds"), Translator.GenerateN2CStruct(Nodes)))
		{
			return Captured;
		}
		const FString Raw = FN2CSerializer::ToJson(Translator.GetN2CBlueprint());
		const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(Normalise(Raw, Blueprint.GetPackagePath()));
		if (!TestTrue(Case.Name + TEXT(": ToJson output parses"), Parsed.IsValid()))
		{
			return Captured;
		}
		Captured.Capture = Parsed;
		Captured.Json = PrettyJson(Parsed);
		return Captured;
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
		const FString Expected = PrettyJson(GoldenObject(Case, Captured.Capture));
		const FString Path = GoldenPath(Case.Name);
		if (UpdateGoldens())
		{
			TestTrue(TEXT("golden written: ") + Path,
				FFileHelper::SaveStringToFile(Expected, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			return;
		}
		FString GoldenText;
		if (!FFileHelper::LoadFileToString(GoldenText, *Path))
		{
			AddError(FString::Printf(TEXT("No golden at %s; run with N2C_UPDATE_GOLDENS=1 to write it"), *Path));
			return;
		}
		GoldenText.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		if (GoldenText != Expected)
		{
			const FString ActualPath = SaveActual(Case.Name, TEXT(".actual"), Expected);
			ReportDifference(FString::Printf(TEXT("%s capture (golden %s, actual %s)"), *Case.Name, *Path, *ActualPath),
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
		const TSharedPtr<FJsonObject> Again = N2CTest::ParseJsonObject(FN2CSerializer::ToJson(Reread));
		return Again.IsValid() ? PrettyJson(Again) : FString();
	}

	void RunRoundTrip(const FCaptureCase& Case)
	{
		const FCaptured Captured = Capture(Case);
		if (!Captured.Capture.IsValid())
		{
			return;
		}
		AddExpectedError(TEXT("Missing required pin fields in JSON"), EAutomationExpectedErrorFlags::Contains, 0);
		const FString Again = RoundTrip(Case, Captured);
		if (Again != Captured.Json)
		{
			ReportDifference(Case.Name + TEXT(" ToJson -> FromJson -> ToJson"), Captured.Json, Again);
		}
	}

	/** Removes every pin without a "type" (exec pins), as FromJson does today */
	static void DropUntypedPins(const TSharedPtr<FJsonObject>& Capture)
	{
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!Capture->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& GraphValue : *Graphs)
		{
			const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
			if (!GraphValue->AsObject()->TryGetArrayField(TEXT("nodes"), NodesArray))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
			{
				const TSharedPtr<FJsonObject> Node = NodeValue->AsObject();
				for (const TCHAR* Field : { TEXT("input_pins"), TEXT("output_pins") })
				{
					TArray<TSharedPtr<FJsonValue>> Kept;
					for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(Field))
					{
						if (PinValue->AsObject()->HasField(TEXT("type")))
						{
							Kept.Add(PinValue);
						}
					}
					Node->SetArrayField(Field, Kept);
				}
			}
		}
		// ParseBlueprintFromJson never reads structs or enums
		Capture->SetArrayField(TEXT("structs"), TArray<TSharedPtr<FJsonValue>>());
		Capture->SetArrayField(TEXT("enums"), TArray<TSharedPtr<FJsonValue>>());
	}

	void RunRoundTripToday(const FCaptureCase& Case)
	{
		const FCaptured Captured = Capture(Case);
		if (!Captured.Capture.IsValid())
		{
			return;
		}
		AddExpectedError(TEXT("Missing required pin fields in JSON"), EAutomationExpectedErrorFlags::Contains, 0);
		const FString Again = RoundTrip(Case, Captured);
		const TSharedPtr<FJsonObject> Expected = N2CTest::ParseJsonObject(Captured.Json);
		DropUntypedPins(Expected);
		const FString ExpectedText = PrettyJson(Expected);
		if (Again != ExpectedText)
		{
			ReportDifference(Case.Name + TEXT(" ToJson -> FromJson -> ToJson (today: exec pins, structs and enums dropped)"),
				ExpectedText, Again);
		}
	}

	void AddCases();

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

	/**
	 * Nested(): the entry runs a collapsed graph GoldenCollapsed (In -> CollectGarbage -> Out), whose Out runs an
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
	const FKnownDefect DataLossy{ TEXT("translation-core-1 (PR #7, issue #13)"), TEXT("0135"),
		TEXT("flows.data is keyed by one end of each link, so an output feeding several inputs keeps only the last edge "
			"recorded; the other inputs say connected with no source") };
	const FKnownDefect GlobalPinIds{ TEXT("translation-core-7"), TEXT("0134"),
		TEXT("Pin IDs are one global counter (P1..Pn across the graph) keyed by pin GUID, and node IDs depend on "
			"traversal order") };
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
		{ DataLossy, GlobalPinIds, DisplayNames },
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
		{ { TEXT("translation-core-1 (knot fan-out)"), TEXT("0135"),
			TEXT("A knot's fan-out is followed through LinkedTo[0] only, and a knot traced from the input side records "
				"the edge backwards, so one of Add's inputs loses its source") },
		  DataLossy, GlobalPinIds, DisplayNames },
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

	Cases.Add({ TEXT("Branch"),
		TEXT("Event graph: custom event OnBranch runs a Branch on Less(A, B); True runs CollectGarbage, False runs "
			"ControlScreensaver."),
		{ GhostEvents, ExecNodeOnly, GlobalPinIds, DisplayNames, CustomEventNone },
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
		{ ExecNodeOnly, GlobalPinIds, DisplayNames, SwitchName },
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
		{ ExecNodeOnly, GlobalPinIds, DisplayNames, SwitchName },
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
		{ BreakAsVariable, GlobalPinIds, DisplayNames },
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
		{ GhostEvents, ExecNodeOnly, GlobalPinIds, DisplayNames, CustomEventNone },
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
		{ GhostEvents, ExecNodeOnly, GlobalPinIds, DisplayNames, CustomEventNone },
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
		  DataLossy, GlobalPinIds, DisplayNames },
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
		  RepeatMissingName, ExecNodeOnly, GlobalPinIds, DisplayNames, SwitchName },
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
			{ { TEXT("translation-core-3 (qw-12)"), TEXT("0130"),
				TEXT("The depth never grows past the first level: every nested user graph is checked as depth 1, so depth "
					"1 captures Grandchild as well and depth 1 and 2 capture the same graphs") },
			  { TEXT("translation-core-9"), TEXT("0134"),
				TEXT("Root calls itself before Root is in the graph list, so Root is captured twice; the second copy "
					"reuses node IDs N1-N3 and gives every pin the same ID (P10, also Child's entry exec pin), and the "
					"validator logs ERROR 'Duplicate pin ID' (expected errors in this case)") },
			  ExecNodeOnly, GlobalPinIds, DisplayNames },
			&BuildRecursion });
		Recursion.TranslationDepth = Depth;
		Recursion.ExpectedErrors = { TEXT("Duplicate pin ID") };
	}

	Cases.Add({ TEXT("CopyPaste"),
		TEXT("Function CopyPaste(Enabled): the entry runs ControlScreensaver (bAllowScreenSaver from Enabled), which "
			"runs a copy-pasted duplicate of itself (pasted as the editor does: new node GUID, pin IDs kept), also fed "
			"from Enabled."),
		{ { TEXT("translation-core-7"), TEXT("0134"),
			TEXT("Pin IDs are keyed by pin GUID, which a paste keeps: every pin of the pasted node gets the same ID (P6), "
				"its flows point at that one ID and the validator logs ERROR 'Duplicate pin ID' (expected errors in "
				"this case)") },
		  DataLossy, ExecNodeOnly, DisplayNames },
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
	Cases.Last().ExpectedErrors = { TEXT("Duplicate pin ID") };

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
			  ExecNodeOnly, GlobalPinIds, DisplayNames },
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
		  GhostEvents, ExecNodeOnly, GlobalPinIds, DisplayNames },
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
	const bool bRunKnownDefects = FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_RUN_KNOWN_DEFECTS")) == TEXT("1");

	for (const FCaptureCase& Case : Cases)
	{
		Describe(Case.Name, [this, Case, bRunKnownDefects]()
		{
			It(TEXT("captures the golden N2C JSON"), [this, Case]() { RunGolden(Case); });

			// Pins today's lossy reader; card 0135 deletes this case and enables the next one
			It(TEXT("drops exec pins, structs and enums in ToJson -> FromJson -> ToJson today"), [this, Case]()
			{
				RunRoundTripToday(Case);
			});

			// Known defect translation-core-25: FromJson requires a pin "type" that ToJson never
			// writes for exec pins, and never reads structs or enums. Enable (xIt -> It) with card 0135.
			if (bRunKnownDefects)
			{
				It(TEXT("survives ToJson -> FromJson -> ToJson unchanged"), [this, Case]() { RunRoundTrip(Case); });
			}
			else
			{
				xIt(TEXT("survives ToJson -> FromJson -> ToJson unchanged"), [this, Case]() { RunRoundTrip(Case); });
			}
		});
	}

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
