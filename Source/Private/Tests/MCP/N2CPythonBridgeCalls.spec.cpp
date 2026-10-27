// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "IPythonScriptPlugin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PythonScriptTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Keeps the tag cases' saves out of the real Saved/NodeToCode
N2C_TEST_PRIVATE_MEMBER(FBridgeCallsStateDirectory, FString UN2CGraphStateManager::*,
	&UN2CGraphStateManager::StateDirectoryOverride)

/**
 * Every UN2CPythonBridge UFUNCTION, called from Python through the `unreal` module as the nodetocode package calls it,
 * answers with text json.loads reads: once with normal arguments and once more per string argument with an apostrophe
 * in it (python-bridge-1: a hand-built response wrote ' as \', and every wrapper's json.loads failed). The calls are in
 * Fixtures/Python/n2c_bridge_calls.py; this spec builds the scratch Blueprint, runs that file in a fresh Python
 * namespace through IPythonScriptPlugin and reports each call. Review ids: imp-python-bridge-10, python-bridge-1
 * (card 0201). NodeToCode.Python.Bridge checks the same responses' content from C++.
 */
BEGIN_DEFINE_SPEC(FN2CPythonBridgeCallsSpec, "NodeToCode.Python.BridgeCalls", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FToolSpecBlueprint> Scratch;
	UEdGraph* Function = nullptr;
	UK2Node_CallFunction* Print = nullptr;
	UK2Node_VariableGet* VariableGet = nullptr;

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

	/** A Python expression for Text that survives any characters: base64 of its UTF-8, decoded in Python */
	static FString PythonString(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return FString::Printf(TEXT("__import__('base64').b64decode('%s').decode('utf-8')"),
			*FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
	}

	/** The scratch Blueprint's function graph, a Print String and a Get of a float member variable in it */
	bool BuildScratch()
	{
		Scratch = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CBridgeCalls"), TEXT("/Game"), /*bTrackDirty*/ false);
		UBlueprint* Blueprint = Scratch->GetBlueprint();
		if (!Blueprint)
		{
			return false;
		}
		FEdGraphPinType FloatType;
		FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
		FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("N2CBridgeValue"), FloatType);
		Function = Scratch->AddFunction(TEXT("N2CBridgeFn"));
		Print = Function ? Scratch->AddCall(Function, UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)) : nullptr;
		if (!Print)
		{
			return false;
		}
		FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Function);
		VariableGet = GetCreator.CreateNode();
		VariableGet->VariableReference.SetSelfMember(TEXT("N2CBridgeValue"));
		VariableGet->NodePosY = 200;
		GetCreator.Finalize();
		Scratch->Settle();
		return true;
	}

	/**
	 * Runs the fixture's group in a fresh namespace and returns its report ({calls, called, functions}), or null with
	 * a test error
	 */
	TSharedPtr<FJsonObject> RunGroup(const FString& Group)
	{
		const FString Script = N2CTest::FixturesDir(TEXT("Python")) / TEXT("n2c_bridge_calls.py");
		if (!TestTrue(FString::Printf(TEXT("the fixture %s exists"), *Script), FPaths::FileExists(Script)))
		{
			return nullptr;
		}
		N2CTest::FScratchDir Out(TEXT("N2CBridgeCalls"));
		const FString ReportPath = Out.Path / TEXT("report.json");

		const TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
		Context->SetStringField(TEXT("group"), Group);
		Context->SetStringField(TEXT("out"), ReportPath);
		Context->SetStringField(TEXT("blueprintPath"), Scratch->Path());
		Context->SetStringField(TEXT("graphGuid"), Function->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Context->SetStringField(TEXT("functionName"), Function->GetName());
		Context->SetStringField(TEXT("printGuid"), Print->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		const UEdGraphPin* InString = Print->FindPin(TEXT("InString"));
		Context->SetStringField(TEXT("printInStringPinId"), InString ? InString->PinId.ToString(EGuidFormats::DigitsWithHyphens) : FString());
		Context->SetStringField(TEXT("variableGetGuid"), VariableGet->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Context->SetStringField(TEXT("v1Document"), N2CTest::FixturesDir(TEXT("Capture/V1")) / TEXT("FanOutData.json"));
		FString ContextText;
		FJsonSerializer::Serialize(Context.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ContextText));

		// One statement, so ExecuteStatement mode: the file runs in a namespace of its own (not the console's shared
		// globals) and its path is never taken for a command line (ExecuteFile mode's '.py ' rule)
		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		Command.Command = FString::Printf(TEXT("exec(compile(open(%s, encoding='utf-8').read(), 'n2c_bridge_calls.py', 'exec'), ")
			TEXT("{'__name__': 'n2c_bridge_calls', 'N2C_CTX': %s})"), *PythonString(Script), *PythonString(ContextText));
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
		TestTrue(TEXT("the report is JSON"), Report.IsValid());
		return Report;
	}

	/** One test error per call whose response json.loads did not read, or that broke the case's other rules */
	int32 ExpectCallsOk(const TSharedPtr<FJsonObject>& Report)
	{
		int32 Count = 0;
		const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
		if (!TestTrue(TEXT("the report lists calls"), Report->TryGetArrayField(TEXT("calls"), Calls) && Calls->Num() > 0))
		{
			return 0;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Calls)
		{
			const TSharedPtr<FJsonObject> Call = Value->AsObject();
			TestTrue(FString::Printf(TEXT("%s: %s"), *Call->GetStringField(TEXT("call")), *Call->GetStringField(TEXT("problem"))),
				Call->GetBoolField(TEXT("ok")));
			++Count;
		}
		AddInfo(FString::Printf(TEXT("%d bridge calls checked"), Count));
		return Count;
	}

	static TSet<FString> StringSet(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TSet<FString> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Result.Add(Value->AsString());
			}
		}
		return Result;
	}

	/** Saves the graph state manager makes during a case go to a scratch folder; the real folder comes back after */
	struct FScopedStateFolder
	{
		N2CTest::FScratchDir Folder{ TEXT("N2CBridgeCallsState") };
		FString Previous;
		FScopedStateFolder()
		{
			FString& Override = UN2CGraphStateManager::Get().*GetPrivateMember(N2CTest::FBridgeCallsStateDirectory());
			Previous = Override;
			Override = Folder.Path;
		}
		~FScopedStateFolder()
		{
			UN2CGraphStateManager::Get().*GetPrivateMember(N2CTest::FBridgeCallsStateDirectory()) = Previous;
		}
	};

	void RemoveScratchTags()
	{
		if (Function)
		{
			for (const FN2CTaggedBlueprintGraph& Tag : UN2CGraphStateManager::Get().GetTaggedGraphs(Function->GraphGuid))
			{
				UN2CGraphStateManager::Get().RemoveTag(Tag.GraphGuid, Tag.Tag, Tag.Category);
			}
		}
	}

END_DEFINE_SPEC(FN2CPythonBridgeCallsSpec)

void FN2CPythonBridgeCallsSpec::Define()
{
	BeforeEach([this]()
	{
		TestTrue(TEXT("the scratch Blueprint is built"), BuildScratch());
	});

	AfterEach([this]()
	{
		RemoveScratchTags();
		Function = nullptr;
		Print = nullptr;
		VariableGet = nullptr;
		Scratch.Reset();
	});

	It(TEXT("calls every bridge function with no Blueprint focused, and json.loads reads every response, also with an apostrophe in any string argument"), [this]()
	{
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()) || !Function)
		{
			return;
		}
		// The calls that need the focused graph must find none, or they would edit whatever graph another case left
		// focused
		if (!TestNull(TEXT("no Blueprint graph is focused"), FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor()))
		{
			return;
		}
		FScopedStateFolder StateFolder;
		// read_n2c_json of JSON that is not N2C JSON, once plain and once with the apostrophe
		AddExpectedError(TEXT("Missing version field in JSON"), EAutomationExpectedErrorFlags::Contains, 2);
		const TSharedPtr<FJsonObject> Report = RunGroup(TEXT("unfocused"));
		if (!Report.IsValid())
		{
			return;
		}
		ExpectCallsOk(Report);

		// Every UFUNCTION the class declares is in the module's Python names and was called
		const TSet<FString> Functions = StringSet(Report, TEXT("functions"));
		const TSet<FString> Called = StringSet(Report, TEXT("called"));
		int32 Declared = 0;
		for (TFieldIterator<UFunction> It(UN2CPythonBridge::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			++Declared;
		}
		TestEqual(TEXT("Python sees each UFUNCTION UN2CPythonBridge declares"), Functions.Num(), Declared);
		TArray<FString> NotCalled = Functions.Difference(Called).Array();
		NotCalled.Sort();
		TestTrue(FString::Printf(TEXT("every bridge function is called (not called: %s)"), *FString::Join(NotCalled, TEXT(", "))),
			NotCalled.IsEmpty());
		TArray<FString> Unknown = Called.Difference(Functions).Array();
		TestTrue(FString::Printf(TEXT("every name called is a bridge function (unknown: %s)"), *FString::Join(Unknown, TEXT(", "))),
			Unknown.IsEmpty());
	});

	// Card 0505: get_blueprint_n2c_json writes node and pin ids as {short, guid}; the reader once took only a string id,
	// logged "Missing required node fields in JSON" per node and reported success with no nodes
	It(TEXT("read_n2c_json reads get_blueprint_n2c_json's document with every node"), [this]()
	{
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()) || !Function)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Report = RunGroup(TEXT("export_round_trip"));
		if (Report.IsValid())
		{
			ExpectCallsOk(Report);
		}
	});

	It(TEXT("on a focused Blueprint, every editing call succeeds with normal arguments, and json.loads reads every response, also with an apostrophe in any string argument"), [this]()
	{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
		AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there; the case above ")
			TEXT("calls the same functions without a focused graph"));
#else
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()) || !Function)
		{
			return;
		}
		if (!Scratch->OpenInEditor(*this, Function))
		{
			return;
		}
		if (!TestTrue(TEXT("the function graph is focused"), FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor() == Function))
		{
			return;
		}
		FScopedStateFolder StateFolder;
		const TSharedPtr<FJsonObject> Report = RunGroup(TEXT("focused"));
		if (Report.IsValid())
		{
			ExpectCallsOk(Report);
		}
#endif
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
