// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CGraphStateManager.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "IPythonScriptPlugin.h"
#include "Internationalization/Regex.h"
#include "K2Node_FunctionEntry.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PythonScriptTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Keeps the tag steps' saves out of the real Saved/NodeToCode
N2C_TEST_PRIVATE_MEMBER(FToolParityStateDirectory, FString UN2CGraphStateManager::*,
	&UN2CGraphStateManager::StateDirectoryOverride)

/**
 * Card 0342: the nodetocode functions run the MCP tools through UN2CPythonBridge::InvokeTool, so a Python call and an
 * MCP tools/call do the same thing. Fixtures/Python/n2c_tool_parity.py runs every such function, in order, on one
 * scratch Blueprint; this spec runs the matching MCP tools (N2CTest::RunTool, the tools/call path) with the same
 * arguments on an identical twin Blueprint, and each step's outcome must match: the same success, the same error
 * text, and on a success every field of the tool's result in the nodetocode result with the same value (the
 * nodetocode result may add the 1.x keys). GUIDs, the twin's name, timestamps, compile times and compiler messages
 * are normalised away. The twins are transient Blueprints under /Game (open-blueprint-asset refuses /Temp), never
 * dirtied, so save_blueprint finds them clean and writes nothing. Review ids: later-1, imp-python-bridge-2.
 */
BEGIN_DEFINE_SPEC(FN2CPythonToolParitySpec, "NodeToCode.Python.ToolParity", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FToolSpecBlueprint> PyBlueprint;
	TUniquePtr<N2CTest::FToolSpecBlueprint> McpBlueprint;
	UEdGraph* PyFunction = nullptr;
	UEdGraph* McpFunction = nullptr;

	static constexpr const TCHAR* FunctionName = TEXT("N2CParityFn");
	static constexpr const TCHAR* PyName = TEXT("BP_N2CParityPy");
	static constexpr const TCHAR* McpName = TEXT("BP_N2CParityMcp");

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

	static FString PythonString(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return FString::Printf(TEXT("__import__('base64').b64decode('%s').decode('utf-8')"),
			*FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
	}

	bool Build()
	{
		PyBlueprint = MakeUnique<N2CTest::FToolSpecBlueprint>(PyName, TEXT("/Game"), /*bTrackDirty*/ false);
		McpBlueprint = MakeUnique<N2CTest::FToolSpecBlueprint>(McpName, TEXT("/Game"), /*bTrackDirty*/ false);
		if (!PyBlueprint->GetBlueprint() || !McpBlueprint->GetBlueprint())
		{
			return false;
		}
		PyFunction = PyBlueprint->AddFunction(FunctionName);
		McpFunction = McpBlueprint->AddFunction(FunctionName);
		PyBlueprint->Settle();
		McpBlueprint->Settle();
		return PyFunction && McpFunction;
	}

	/** Runs the Python fixture on the Py twin and returns its report, or null with a test error */
	TSharedPtr<FJsonObject> RunPython()
	{
		const FString Script = N2CTest::FixturesDir(TEXT("Python")) / TEXT("n2c_tool_parity.py");
		if (!TestTrue(FString::Printf(TEXT("the fixture %s exists"), *Script), FPaths::FileExists(Script)))
		{
			return nullptr;
		}
		N2CTest::FScratchDir Out(TEXT("N2CToolParity"));
		const FString ReportPath = Out.Path / TEXT("report.json");
		const TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
		Context->SetStringField(TEXT("out"), ReportPath);
		Context->SetStringField(TEXT("blueprintPath"), PyBlueprint->Path());
		Context->SetStringField(TEXT("functionName"), FunctionName);
		FString ContextText;
		FJsonSerializer::Serialize(Context.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ContextText));

		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		Command.Command = FString::Printf(TEXT("exec(compile(open(%s, encoding='utf-8').read(), 'n2c_tool_parity.py', 'exec'), ")
			TEXT("{'__name__': 'n2c_tool_parity', 'N2C_CTX': %s})"), *PythonString(Script), *PythonString(ContextText));
		const bool bRan = IPythonScriptPlugin::Get()->ExecPythonCommandEx(Command);
		if (!TestTrue(FString::Printf(TEXT("the fixture runs (%s)"), *Command.CommandResult.Left(1500)), bRan))
		{
			return nullptr;
		}
		FString ReportText;
		if (!TestTrue(TEXT("the fixture writes its report"), FFileHelper::LoadFileToString(ReportText, *ReportPath)))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Report = N2CTest::ParseJsonObject(ReportText);
		if (!TestTrue(TEXT("the report is JSON"), Report.IsValid()))
		{
			return nullptr;
		}
		const FString Problem = Report->GetStringField(TEXT("problem"));
		TestTrue(FString::Printf(TEXT("the fixture runs to its end: %s"), *Problem), Problem.IsEmpty());
		return Report;
	}

	/** One MCP step's outcome as nodetocode reports it: {success, data, error}, a value's "success": false a failure */
	static TSharedPtr<FJsonObject> McpStep(const FString& Step, const FString& Tool, const TSharedPtr<FJsonObject>& Args)
	{
		const FN2CToolOutcome Outcome = FN2CToolOutcome::FromCallToolResult(N2CTest::RunTool(Tool, Args));
		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("step"), Step);
		bool bSuccess = !Outcome.IsError();
		FString Error = Outcome.IsError() ? Outcome.Error->Message : FString();
		const TSharedPtr<FJsonObject> Data = Outcome.Value.IsValid() && Outcome.Value->Type == EJson::Object ? Outcome.Value->AsObject() : nullptr;
		bool bInner = true;
		if (bSuccess && Data.IsValid() && Data->TryGetBoolField(TEXT("success"), bInner) && !bInner)
		{
			bSuccess = false;
			if (!Data->TryGetStringField(TEXT("message"), Error))
			{
				Data->TryGetStringField(TEXT("error"), Error);
			}
		}
		Entry->SetBoolField(TEXT("success"), bSuccess);
		Entry->SetField(TEXT("data"), Outcome.Value.IsValid() ? Outcome.Value : MakeShared<FJsonValueNull>());
		Entry->SetStringField(TEXT("error"), Error);
		return Entry;
	}

	TSharedPtr<FJsonObject> Target() const
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), McpBlueprint->Path());
		Args->SetStringField(TEXT("graphName"), FunctionName);
		return Args;
	}

	static TSharedPtr<FJsonObject> With(const TSharedPtr<FJsonObject>& Base, TFunctionRef<void(FJsonObject&)> Fill)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		if (Base.IsValid())
		{
			Args->Values = Base->Values;
		}
		Fill(*Args);
		return Args;
	}

	static TArray<TSharedPtr<FJsonValue>> StringArray(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueString>(Text));
		return Values;
	}

	static TSharedPtr<IN2CMcpTool> RegistryTool(const FString& Name)
	{
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (Tool.IsValid() && Tool->GetDefinition().Name == Name)
			{
				return Tool;
			}
		}
		return nullptr;
	}

	static TSharedPtr<FJsonValue> PinRef(const UEdGraphPin* Pin)
	{
		const TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
		Ref->SetStringField(TEXT("nodeGuid"), Pin ? Pin->GetOwningNode()->NodeGuid.ToString() : FString());
		Ref->SetStringField(TEXT("pinGuid"), Pin ? Pin->PinId.ToString() : FString());
		return MakeShared<FJsonValueObject>(Ref);
	}

	/** The same steps as the fixture, with the MCP tools on the twin */
	TArray<TSharedPtr<FJsonObject>> RunMcp()
	{
		TArray<TSharedPtr<FJsonObject>> Steps;
		const FString Path = McpBlueprint->Path();
		UBlueprint* Blueprint = McpBlueprint->GetBlueprint();
		const TSharedPtr<FJsonObject> T = Target();

		Steps.Add(McpStep(TEXT("open_blueprint"), TEXT("open-blueprint-asset"), With(nullptr, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("asset_path"), Path);
			A.SetStringField(TEXT("focus_graph"), FunctionName);
		})));
		Steps.Add(McpStep(TEXT("open_blueprint_function"), TEXT("open-blueprint-function"), With(nullptr, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("graphName"), FunctionName);
		})));
		const TSharedPtr<FJsonObject> Search = McpStep(TEXT("search_blueprint_nodes"), TEXT("search-blueprint-nodes"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("searchTerm"), TEXT("Print String"));
			A.SetBoolField(TEXT("contextSensitive"), true);
			A.SetNumberField(TEXT("maxResults"), 5);
			A.SetBoolField(TEXT("excludeVMFunctions"), true);
		}));
		Steps.Add(Search);
		FString NodeName = TEXT("Print String");
		FString ActionIdentifier;
		const TSharedPtr<FJsonObject>* SearchData = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (Search->TryGetObjectField(TEXT("data"), SearchData) && (*SearchData)->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			for (const TSharedPtr<FJsonValue>& Node : *Nodes)
			{
				const TSharedPtr<FJsonObject>* Meta = nullptr;
				FString Name;
				if (Node->AsObject()->TryGetObjectField(TEXT("spawnMetadata"), Meta) && (*Meta)->TryGetStringField(TEXT("nodeName"), Name)
					&& Name == TEXT("Print String"))
				{
					(*Meta)->TryGetStringField(TEXT("actionIdentifier"), ActionIdentifier);
					break;
				}
			}
		}
		const TSharedPtr<FJsonObject> Added = McpStep(TEXT("add_node_to_graph"), TEXT("add-bp-node-to-active-graph"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("nodeName"), NodeName);
			A.SetStringField(TEXT("actionIdentifier"), ActionIdentifier);
			const TSharedPtr<FJsonObject> Location = MakeShared<FJsonObject>();
			Location->SetNumberField(TEXT("x"), 400);
			Location->SetNumberField(TEXT("y"), 0);
			A.SetObjectField(TEXT("location"), Location);
		}));
		Steps.Add(Added);
		FString NodeGuid;
		const TSharedPtr<FJsonObject>* AddedData = nullptr;
		if (Added->TryGetObjectField(TEXT("data"), AddedData))
		{
			(*AddedData)->TryGetStringField(TEXT("nodeGuid"), NodeGuid);
		}
		UEdGraphNode* Print = N2CTest::FindNodeByGuid(Blueprint, NodeGuid);
		UEdGraphPin* PrintExec = Print ? N2CTest::ExecIn(Print) : nullptr;
		UEdGraphPin* PrintString = Print ? Print->FindPin(TEXT("InString")) : nullptr;
		UEdGraphPin* EntryThen = N2CTest::ExecOut(McpBlueprint->FunctionEntry(McpFunction));

		Steps.Add(McpStep(TEXT("find_nodes_in_graph"), TEXT("find-nodes-in-graph"), With(T, [&](FJsonObject& A)
		{
			A.SetArrayField(TEXT("searchTerms"), StringArray(FunctionName));
			A.SetStringField(TEXT("searchType"), TEXT("keyword"));
			A.SetBoolField(TEXT("caseSensitive"), false);
			A.SetNumberField(TEXT("maxResults"), 50);
		})));
		Steps.Add(McpStep(TEXT("connect_pins"), TEXT("connect-pins"), With(T, [&](FJsonObject& A)
		{
			const TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetField(TEXT("from"), PinRef(EntryThen));
			Link->SetField(TEXT("to"), PinRef(PrintExec));
			A.SetArrayField(TEXT("connections"), TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(Link) });
			const TSharedPtr<FJsonObject> Options = MakeShared<FJsonObject>();
			Options->SetBoolField(TEXT("breakExistingLinks"), false);
			A.SetObjectField(TEXT("options"), Options);
		})));
		Steps.Add(McpStep(TEXT("set_pin_value"), TEXT("set-input-pin-value"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("nodeGuid"), NodeGuid);
			A.SetStringField(TEXT("pinGuid"), PrintString ? PrintString->PinId.ToString() : FString());
			A.SetStringField(TEXT("value"), TEXT("it's \"quoted\""));
		})));
		Steps.Add(McpStep(TEXT("create_comment_node"), TEXT("create-comment-node"), With(T, [&](FJsonObject& A)
		{
			A.SetArrayField(TEXT("nodeGuids"), StringArray(NodeGuid));
			A.SetStringField(TEXT("commentText"), TEXT("Player's comment"));
			const TSharedPtr<FJsonObject> Color = MakeShared<FJsonObject>();
			Color->SetNumberField(TEXT("r"), 0.075);
			Color->SetNumberField(TEXT("g"), 0.075);
			Color->SetNumberField(TEXT("b"), 0.075);
			A.SetObjectField(TEXT("color"), Color);
			A.SetNumberField(TEXT("fontSize"), 18);
			A.SetStringField(TEXT("moveMode"), TEXT("group"));
			A.SetNumberField(TEXT("padding"), 50);
		})));
		Steps.Add(McpStep(TEXT("add_function_input_pin"), TEXT("add-function-input-pin"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("pinName"), TEXT("Speed"));
			A.SetStringField(TEXT("typeIdentifier"), TEXT("float"));
			A.SetStringField(TEXT("defaultValue"), TEXT("1.5"));
			A.SetBoolField(TEXT("isPassByReference"), false);
			A.SetStringField(TEXT("tooltip"), TEXT(""));
		})));
		Steps.Add(McpStep(TEXT("add_function_return_pin"), TEXT("add-function-return-pin"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("pinName"), TEXT("Result"));
			A.SetStringField(TEXT("typeIdentifier"), TEXT("int"));
			A.SetStringField(TEXT("tooltip"), TEXT(""));
		})));
		Steps.Add(McpStep(TEXT("remove_function_entry_pin"), TEXT("remove-function-entry-pin"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("pinName"), TEXT("Speed"));
		})));
		Steps.Add(McpStep(TEXT("remove_function_return_pin"), TEXT("remove-function-return-pin"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("pinName"), TEXT("Result"));
		})));
		Steps.Add(McpStep(TEXT("compile_blueprint"), TEXT("compile-blueprint"), With(nullptr, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("blueprintPath"), Path);
		})));
		Steps.Add(McpStep(TEXT("save_blueprint"), TEXT("save-blueprint"), With(nullptr, [&](FJsonObject& A)
		{
			A.SetBoolField(TEXT("saveOnlyIfDirty"), true);
			A.SetStringField(TEXT("blueprintPath"), Path);
		})));
		Steps.Add(McpStep(TEXT("tag_graph"), TEXT("tag-blueprint-graph"), With(T, [&](FJsonObject& A)
		{
			A.SetStringField(TEXT("tag"), TEXT("N2CParity"));
			A.SetStringField(TEXT("category"), TEXT("N2CParity"));
			A.SetStringField(TEXT("description"), TEXT("Player's input"));
		})));
		Steps.Add(McpStep(TEXT("delete_nodes"), TEXT("delete-blueprint-node"), With(T, [&](FJsonObject& A)
		{
			A.SetArrayField(TEXT("nodeGuids"), StringArray(NodeGuid));
			A.SetBoolField(TEXT("preserveConnections"), false);
			A.SetBoolField(TEXT("force"), false);
		})));
		return Steps;
	}

	// ---- Comparing ----

	/** A value with what differs between the twins by construction taken out */
	static TSharedPtr<FJsonValue> Normalise(const TSharedPtr<FJsonValue>& Value)
	{
		static const TSet<FString> Dropped = { TEXT("timestamp"), TEXT("compilationTime"), TEXT("compileSeconds"), TEXT("messages") };
		if (!Value.IsValid())
		{
			return MakeShared<FJsonValueNull>();
		}
		switch (Value->Type)
		{
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values)
			{
				if (!Dropped.Contains(Field.Key))
				{
					Out->SetField(Field.Key, Normalise(Field.Value));
				}
			}
			return MakeShared<FJsonValueObject>(Out);
		}
		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				Out.Add(Normalise(Item));
			}
			return MakeShared<FJsonValueArray>(Out);
		}
		case EJson::String:
			return MakeShared<FJsonValueString>(NormaliseText(Value->AsString()));
		default:
			return Value;
		}
	}

	static FString NormaliseText(const FString& Text)
	{
		FString Out = Text.Replace(McpName, PyName, ESearchCase::CaseSensitive);
		static const FRegexPattern Guid(TEXT("[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}"));
		FString Result;
		int32 Last = 0;
		FRegexMatcher Matcher(Guid, Out);
		while (Matcher.FindNext())
		{
			Result += Out.Mid(Last, Matcher.GetMatchBeginning() - Last) + TEXT("<guid>");
			Last = Matcher.GetMatchEnding();
		}
		return Result + Out.Mid(Last);
	}

	/** Empty when Py holds every field of Mcp with the same value (Py may have more), else where they first differ */
	static FString FirstGap(const TSharedPtr<FJsonValue>& Py, const TSharedPtr<FJsonValue>& Mcp, const FString& Where)
	{
		if (Mcp->Type == EJson::Object)
		{
			if (Py->Type != EJson::Object)
			{
				return Where + TEXT(": not an object in the nodetocode result");
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Mcp->AsObject()->Values)
			{
				const TSharedPtr<FJsonValue> Other = Py->AsObject()->TryGetField(Field.Key);
				if (!Other.IsValid())
				{
					return Where + TEXT(".") + Field.Key + TEXT(": missing in the nodetocode result");
				}
				const FString Gap = FirstGap(Other, Field.Value, Where + TEXT(".") + Field.Key);
				if (!Gap.IsEmpty())
				{
					return Gap;
				}
			}
			return FString();
		}
		if (Mcp->Type == EJson::Array)
		{
			if (Py->Type != EJson::Array || Py->AsArray().Num() != Mcp->AsArray().Num())
			{
				return Where + TEXT(": arrays of different length");
			}
			for (int32 Index = 0; Index < Mcp->AsArray().Num(); ++Index)
			{
				const FString Gap = FirstGap(Py->AsArray()[Index], Mcp->AsArray()[Index], FString::Printf(TEXT("%s[%d]"), *Where, Index));
				if (!Gap.IsEmpty())
				{
					return Gap;
				}
			}
			return FString();
		}
		if (Mcp->Type == EJson::Number && Py->Type == EJson::Number)
		{
			return FMath::IsNearlyEqual(Mcp->AsNumber(), Py->AsNumber(), 1e-4) ? FString()
				: FString::Printf(TEXT("%s: %g (nodetocode) vs %g (MCP)"), *Where, Py->AsNumber(), Mcp->AsNumber());
		}
		FString PyText, McpText;
		const bool bSame = Py->Type == Mcp->Type && Py->TryGetString(PyText) == Mcp->TryGetString(McpText) && PyText == McpText
			&& (Mcp->Type != EJson::Boolean || Py->AsBool() == Mcp->AsBool());
		return bSame ? FString() : FString::Printf(TEXT("%s: '%s' (nodetocode) vs '%s' (MCP)"), *Where, *PyText, *McpText);
	}

	void RemoveScratchTags()
	{
		for (UEdGraph* Graph : { PyFunction, McpFunction })
		{
			if (Graph)
			{
				for (const FN2CTaggedBlueprintGraph& Tag : UN2CGraphStateManager::Get().GetTaggedGraphs(Graph->GraphGuid))
				{
					UN2CGraphStateManager::Get().RemoveTag(Tag.GraphGuid, Tag.Tag, Tag.Category);
				}
			}
		}
	}

	struct FScopedStateFolder
	{
		N2CTest::FScratchDir Folder{ TEXT("N2CToolParityState") };
		FString Previous;
		FScopedStateFolder()
		{
			FString& Override = UN2CGraphStateManager::Get().*GetPrivateMember(N2CTest::FToolParityStateDirectory());
			Previous = Override;
			Override = Folder.Path;
		}
		~FScopedStateFolder()
		{
			UN2CGraphStateManager::Get().*GetPrivateMember(N2CTest::FToolParityStateDirectory()) = Previous;
		}
	};

END_DEFINE_SPEC(FN2CPythonToolParitySpec)

void FN2CPythonToolParitySpec::Define()
{
	AfterEach([this]()
	{
		RemoveScratchTags();
		PyFunction = nullptr;
		McpFunction = nullptr;
		PyBlueprint.Reset();
		McpBlueprint.Reset();
	});

	It(TEXT("gives each nodetocode function that runs a tool the outcome the MCP tool gives for the same call"), [this]()
	{
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()) || !TestTrue(TEXT("the twin Blueprints are built"), Build()))
		{
			return;
		}
		FScopedStateFolder StateFolder;
		const TSharedPtr<FJsonObject> Report = RunPython();
		if (!Report.IsValid())
		{
			return;
		}
		const TArray<TSharedPtr<FJsonObject>> McpSteps = RunMcp();
		const TArray<TSharedPtr<FJsonValue>>* PySteps = nullptr;
		if (!TestTrue(TEXT("the report lists the steps"), Report->TryGetArrayField(TEXT("steps"), PySteps))
			|| !TestEqual(TEXT("the fixture and the spec run the same number of steps"), PySteps->Num(), McpSteps.Num()))
		{
			return;
		}
		for (int32 Index = 0; Index < McpSteps.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Py = (*PySteps)[Index]->AsObject();
			const TSharedPtr<FJsonObject> Mcp = McpSteps[Index];
			const FString Step = Mcp->GetStringField(TEXT("step"));
			if (!TestEqual(FString::Printf(TEXT("step %d is the same function"), Index), Py->GetStringField(TEXT("step")), Step))
			{
				return;
			}
			const bool bPySuccess = Py->GetBoolField(TEXT("success"));
			const bool bMcpSuccess = Mcp->GetBoolField(TEXT("success"));
			FString PyError, McpError;
			Py->TryGetStringField(TEXT("error"), PyError);
			Mcp->TryGetStringField(TEXT("error"), McpError);
			TestEqual(FString::Printf(TEXT("%s: the same success (nodetocode error: %s; MCP error: %s)"), *Step, *PyError, *McpError),
				bPySuccess, bMcpSuccess);
			if (!bMcpSuccess)
			{
				TestEqual(FString::Printf(TEXT("%s: the same error text"), *Step), NormaliseText(PyError), NormaliseText(McpError));
				continue;
			}
			const FString Gap = FirstGap(Normalise(Py->TryGetField(TEXT("data"))), Normalise(Mcp->TryGetField(TEXT("data"))), TEXT("data"));
			TestTrue(FString::Printf(TEXT("%s: the nodetocode result holds the MCP result (%s)"), *Step, *Gap), Gap.IsEmpty());
		}

		// The card's tag case: "Player's input" round-trips through json.loads, and removing a tag the graph does not
		// have returns the real error text
		const TSharedPtr<FJsonObject>* TagCase = nullptr;
		if (TestTrue(TEXT("the report has the tag case"), Report->TryGetObjectField(TEXT("tagCase"), TagCase)))
		{
			const TSharedPtr<FJsonObject> Tagged = (*TagCase)->GetObjectField(TEXT("tagged"));
			const TSharedPtr<FJsonObject> Missing = (*TagCase)->GetObjectField(TEXT("missing"));
			TestTrue(TEXT("tag_graph with \"Player's input\" succeeds"), Tagged->GetBoolField(TEXT("success")));
			const TSharedPtr<FJsonObject>* TaggedData = nullptr;
			const TSharedPtr<FJsonObject>* TaggedGraph = nullptr;
			TestTrue(TEXT("the tag keeps its description"), Tagged->TryGetObjectField(TEXT("data"), TaggedData)
				&& (*TaggedData)->TryGetObjectField(TEXT("taggedGraph"), TaggedGraph)
				&& (*TaggedGraph)->GetStringField(TEXT("description")) == TEXT("Player's input"));
			TestFalse(TEXT("remove_tag of a missing tag fails"), Missing->GetBoolField(TEXT("success")));
			const FString Error = Missing->GetStringField(TEXT("error"));
			TestTrue(FString::Printf(TEXT("remove_tag's error names the missing tag (%s)"), *Error),
				Error.Contains(TEXT("N2CParityMissing")) && Error.Contains(TEXT("not found")));
		}
	});

	It(TEXT("lets InvokeTool reach a tool the MCP server does not serve, and refuses unknown and Python tools with their codes"), [this]()
	{
		const TSharedPtr<FJsonObject> Unknown = N2CTest::ParseJsonObject(UN2CPythonBridge::InvokeTool(TEXT("no-such-tool"), TEXT("{}")));
		TestTrue(TEXT("an unknown tool is UNKNOWN_TOOL"), Unknown.IsValid() && !Unknown->GetBoolField(TEXT("success"))
			&& Unknown->GetStringField(TEXT("code")) == TEXT("UNKNOWN_TOOL"));
		const TSharedPtr<FJsonObject> Python = N2CTest::ParseJsonObject(UN2CPythonBridge::InvokeTool(TEXT("run-python"), TEXT("{\"code\": \"x = 1\"}")));
		TestTrue(TEXT("run-python is refused"), Python.IsValid() && !Python->GetBoolField(TEXT("success"))
			&& Python->GetStringField(TEXT("error")).Contains(TEXT("PythonTools")));
		const TSharedPtr<FJsonObject> Missing = N2CTest::ParseJsonObject(UN2CPythonBridge::InvokeTool(TEXT("connect-pins"), TEXT("")));
		TestTrue(TEXT("a missing required argument is INVALID_ARGUMENT naming it"), Missing.IsValid()
			&& Missing->GetStringField(TEXT("code")) == TEXT("INVALID_ARGUMENT") && Missing->GetStringField(TEXT("field")) == TEXT("connections"));
		const TSharedPtr<FJsonObject> NotObject = N2CTest::ParseJsonObject(UN2CPythonBridge::InvokeTool(TEXT("connect-pins"), TEXT("[1, 2]")));
		TestTrue(TEXT("arguments that are not a JSON object are INVALID_ARGUMENT"), NotObject.IsValid()
			&& NotObject->GetStringField(TEXT("code")) == TEXT("INVALID_ARGUMENT"));

		// The server's manager serves only the active tool set; InvokeTool resolves from every registered tool
		FN2CMcpToolManager& Server = FN2CMcpToolManager::Get();
		const bool bServed = Server.IsToolRegistered(TEXT("list-blueprint-graphs"));
		if (bServed)
		{
			Server.UnregisterTool(TEXT("list-blueprint-graphs"));
		}
		const TSharedPtr<FJsonObject> Graphs = N2CTest::ParseJsonObject(UN2CPythonBridge::InvokeTool(TEXT("list-blueprint-graphs"),
			TEXT("{\"blueprintPath\": \"/Engine/EngineSky/BP_Sky_Sphere\"}")));
		if (bServed)
		{
			Server.RegisterTool(RegistryTool(TEXT("list-blueprint-graphs")));
		}
		TestTrue(TEXT("a tool the server does not serve runs"), Graphs.IsValid() && Graphs->GetBoolField(TEXT("success")));
		const TSharedPtr<FJsonObject>* Data = nullptr;
		TestTrue(TEXT("its value is the tool's JSON object"), Graphs.IsValid() && Graphs->TryGetObjectField(TEXT("data"), Data)
			&& (*Data)->HasField(TEXT("graphs")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
