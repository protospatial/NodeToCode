// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Subsystems/N2CEditorSubsystem.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "IPythonScriptPlugin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PythonScriptTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/**
 * Card 0301: UN2CEditorSubsystem, NodeToCode's typed, path-based Blueprint functions. In C++: each function's result on
 * a test Blueprint (a compile with an error, the graph list, the parent class, the N2C JSON read, a tag's round trip),
 * and each one's raised script error naming a missing path. Through Python (Fixtures/Python/n2c_editor_subsystem.py):
 * unreal.get_editor_subsystem(unreal.N2CEditorSubsystem) on a saved scratch Blueprint, RuntimeError for a bad path,
 * the nodetocode wrappers on top of it, and the replaced UN2CPythonBridge functions still listed but deprecated.
 */
BEGIN_DEFINE_SPEC(FN2CEditorSubsystemSpec, "NodeToCode.Editor.Subsystem", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Function = nullptr;

	static UN2CEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UN2CEditorSubsystem>() : nullptr;
	}

	static FString PythonString(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return FString::Printf(TEXT("__import__('base64').b64decode('%s').decode('utf-8')"),
			*FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
	}
END_DEFINE_SPEC(FN2CEditorSubsystemSpec)

void FN2CEditorSubsystemSpec::Define()
{
	BeforeEach([this]()
	{
		Test = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CEditorSubsystem"));
		Blueprint = Test->GetBlueprint();
		Function = Blueprint ? Test->AddFunction(TEXT("N2CBroken")) : nullptr;
		if (!TestNotNull(TEXT("the test Blueprint and its function"), Function))
		{
			return;
		}
		// Set Static Mesh after the entry with no target: an Actor Blueprint is not a StaticMeshComponent, so the
		// compile reports an error that names the node
		UK2Node_CallFunction* Call = Test->AddCall(Function, UStaticMeshComponent::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UStaticMeshComponent, SetStaticMesh));
		TestTrue(TEXT("Set Static Mesh wired after the entry"), Call && Test->Link(Test->FunctionEntry(Function),
			UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute));
	});

	AfterEach([this]()
	{
		Function = nullptr;
		Blueprint = nullptr;
		Test.Reset();
	});

	It(TEXT("is an editor subsystem"), [this]()
	{
		TestNotNull(TEXT("GEditor->GetEditorSubsystem<UN2CEditorSubsystem>()"), Subsystem());
	});

	It(TEXT("compiles a Blueprint and reports the compiler's errors in its result"), [this]()
	{
		if (!Function || !Subsystem())
		{
			return;
		}
		const FN2CCompileResult Result = Subsystem()->CompileBlueprint(Blueprint->GetPathName());
		TestEqual(TEXT("the Blueprint compiled"), Result.BlueprintPath, Blueprint->GetPathName());
		TestEqual(TEXT("status Error"), Result.Status, FString(TEXT("Error")));
		TestTrue(TEXT("an error counted"), Result.ErrorCount >= 1);
		const FN2CCompileMessageInfo* Error = Result.Messages.FindByPredicate([](const FN2CCompileMessageInfo& Message)
			{ return Message.Severity == TEXT("Error"); });
		if (TestNotNull(TEXT("an Error message"), Error))
		{
			TestEqual(TEXT("in the function graph"), Error->GraphName, FString(TEXT("N2CBroken")));
			TestFalse(TEXT("naming the node"), Error->NodeGuid.IsEmpty());
		}
	});

	It(TEXT("lists the Blueprint's graphs and its parent class"), [this]()
	{
		if (!Function || !Subsystem())
		{
			return;
		}
		const FN2CBlueprintGraphList Graphs = Subsystem()->ListBlueprintGraphs(Blueprint->GetPathName());
		TestEqual(TEXT("the Blueprint's name"), Graphs.BlueprintName, FString(TEXT("BP_N2CEditorSubsystem")));
		const FN2CBlueprintGraphInfo* Listed = Graphs.Graphs.FindByPredicate([](const FN2CBlueprintGraphInfo& Graph)
			{ return Graph.Name == TEXT("N2CBroken"); });
		if (TestNotNull(TEXT("the function listed"), Listed))
		{
			TestEqual(TEXT("kind function"), Listed->Kind, FString(TEXT("function")));
			TestEqual(TEXT("its GraphGuid"), Listed->GraphGuid, Function->GraphGuid.ToString());
			TestEqual(TEXT("entry and call"), Listed->NodeCount, 2);
			TestTrue(TEXT("translatable"), Listed->bTranslatable);
		}
		TestTrue(TEXT("the event graph listed"), Graphs.Graphs.ContainsByPredicate([](const FN2CBlueprintGraphInfo& Graph)
			{ return Graph.Kind == TEXT("event"); }));

		const FN2CBlueprintParentInfo Parent = Subsystem()->GetBlueprintParentClass(Blueprint->GetPathName());
		TestTrue(TEXT("parent Actor"), Parent.ParentClass == AActor::StaticClass());
		TestEqual(TEXT("its path"), Parent.ParentClassPath, FString(TEXT("/Script/Engine.Actor")));
		TestEqual(TEXT("the native parent"), Parent.NativeParentClassPath, FString(TEXT("/Script/Engine.Actor")));
	});

	It(TEXT("reads the Blueprint as N2C JSON, every graph or one by name or GUID"), [this]()
	{
		if (!Function || !Subsystem())
		{
			return;
		}
		const FN2CBlueprintN2CJson All = Subsystem()->GetBlueprintN2CJson(Blueprint->GetPathName());
		TestTrue(TEXT("every graph read"), All.GraphCount >= 2);
		TestTrue(TEXT("the document is a JSON object"), N2CTest::ParseJsonObject(All.N2CJson).IsValid());
		TestFalse(TEXT("the untrusted-content notice"), All.UntrustedContent.IsEmpty());
		TestTrue(TEXT("no graph named"), All.GraphName.IsEmpty());

		const FN2CBlueprintN2CJson ByGuid = Subsystem()->GetBlueprintN2CJson(Blueprint->GetPathName(), Function->GraphGuid.ToString(), 0);
		TestEqual(TEXT("one graph by GUID"), ByGuid.GraphCount, 1);
		TestEqual(TEXT("the graph read"), ByGuid.GraphName, FString(TEXT("N2CBroken")));
		TestEqual(TEXT("its kind"), ByGuid.GraphKind, FString(TEXT("function")));
		TestEqual(TEXT("by name, the same graph"), Subsystem()->GetBlueprintN2CJson(Blueprint->GetPathName(), TEXT("N2CBroken")).GraphGuid,
			Function->GraphGuid.ToString());
	});

	It(TEXT("tags a graph, lists the tag and removes it"), [this]()
	{
		if (!Function || !Subsystem())
		{
			return;
		}
		const FString Path = Blueprint->GetPathName();
		const FString TagName = FString::Printf(TEXT("N2CSubsystemSpec%s"), *FGuid::NewGuid().ToString());
		const FN2CTagInfo Tagged = Subsystem()->TagGraph(Path, TEXT("N2CBroken"), TagName, TEXT("N2CSpec"), TEXT("card 0301"));
		TestEqual(TEXT("the tag"), Tagged.Tag, TagName);
		TestEqual(TEXT("on the function"), Tagged.GraphName, FString(TEXT("N2CBroken")));
		TestEqual(TEXT("of the Blueprint"), Tagged.BlueprintPath, Path);

		const TArray<FN2CTagInfo> Listed = Subsystem()->ListTags(TEXT("N2CSpec"), TagName, Path);
		if (TestEqual(TEXT("listed once"), Listed.Num(), 1))
		{
			TestEqual(TEXT("with its description"), Listed[0].Description, FString(TEXT("card 0301")));
		}

		const FN2CTagRemoval Removed = Subsystem()->RemoveTag(Path, TEXT("N2CBroken"), TagName);
		TestEqual(TEXT("removed once"), Removed.RemovedCount, 1);
		TestEqual(TEXT("from the function"), Removed.GraphGuid, Function->GraphGuid.ToString());
		TestEqual(TEXT("no longer listed"), Subsystem()->ListTags(TEXT(""), TagName).Num(), 0);

		AddExpectedError(TEXT("not found on graph"), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("Script call stack"), EAutomationExpectedErrorFlags::Contains, 0);
		TestEqual(TEXT("a second removal raises and removes nothing"), Subsystem()->RemoveTag(Path, Function->GraphGuid.ToString(), TagName).RemovedCount, 0);
	});

	It(TEXT("raises a script error that names a missing Blueprint's path, from every function"), [this]()
	{
		UN2CEditorSubsystem* Editor = Subsystem();
		if (!Editor)
		{
			return;
		}
		const FString Missing = TEXT("/Game/N2CNoSuchFolder/BP_N2CMissing");
		AddExpectedError(Missing, EAutomationExpectedErrorFlags::Contains, 0);
		// Outside Python a raised script error is logged with the (empty) script call stack
		AddExpectedError(TEXT("Script call stack"), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("INVALID_ARGUMENT: a Blueprint asset path is required"), EAutomationExpectedErrorFlags::Contains, 0);
		TestTrue(TEXT("CompileBlueprint"), Editor->CompileBlueprint(Missing).BlueprintPath.IsEmpty());
		TestFalse(TEXT("SaveBlueprint"), Editor->SaveBlueprint(Missing).bWasSaved);
		TestEqual(TEXT("ListBlueprintGraphs"), Editor->ListBlueprintGraphs(Missing).Graphs.Num(), 0);
		TestTrue(TEXT("GetBlueprintParentClass"), Editor->GetBlueprintParentClass(Missing).ParentClass == nullptr);
		TestTrue(TEXT("GetBlueprintN2CJson"), Editor->GetBlueprintN2CJson(Missing).N2CJson.IsEmpty());
		TestTrue(TEXT("TagGraph"), Editor->TagGraph(Missing, TEXT(""), TEXT("N2CNever")).Tag.IsEmpty());
		TestEqual(TEXT("RemoveTag"), Editor->RemoveTag(Missing, TEXT("EventGraph"), TEXT("N2CNever")).RemovedCount, 0);
		// An empty path never falls back to the focused Blueprint
		TestTrue(TEXT("CompileBlueprint with no path"), Editor->CompileBlueprint(TEXT("")).BlueprintPath.IsEmpty());
		TestTrue(TEXT("TagGraph with no path"), Editor->TagGraph(TEXT(""), TEXT(""), TEXT("N2CNever")).Tag.IsEmpty());
	});

	It(TEXT("leaves the replaced bridge functions callable but deprecated, so Blueprint menus hide them"), [this]()
	{
		for (const TCHAR* Name : { TEXT("GetBlueprintN2CJson"), TEXT("ListBlueprintGraphs"), TEXT("CompileFocusedBlueprint"),
			TEXT("SaveFocusedBlueprint"), TEXT("TagFocusedGraph") })
		{
			const UFunction* Found = UN2CPythonBridge::StaticClass()->FindFunctionByName(Name);
			if (TestNotNull(FString::Printf(TEXT("%s still exists"), Name), Found))
			{
				TestTrue(FString::Printf(TEXT("%s is BlueprintCallable (so Python lists it)"), Name), Found->HasAnyFunctionFlags(FUNC_BlueprintCallable));
				TestTrue(FString::Printf(TEXT("%s is deprecated"), Name), Found->HasMetaData(TEXT("DeprecatedFunction")));
				TestFalse(FString::Printf(TEXT("%s is not BlueprintInternalUseOnly (which would hide it from Python)"), Name),
					Found->HasMetaData(TEXT("BlueprintInternalUseOnly")));
			}
		}
	});

	It(TEXT("works from Python through unreal.get_editor_subsystem"), [this]()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!TestTrue(TEXT("Python is available"), Python && Python->IsPythonAvailable()))
		{
			return;
		}
		const FString Script = N2CTest::FixturesDir(TEXT("Python")) / TEXT("n2c_editor_subsystem.py");
		if (!TestTrue(FString::Printf(TEXT("the fixture %s exists"), *Script), FPaths::FileExists(Script)))
		{
			return;
		}
		N2CTest::FScratchDir Out(TEXT("N2CEditorSubsystem"));
		const FString ReportPath = Out.Path / TEXT("report.json");
		const TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
		Context->SetStringField(TEXT("out"), ReportPath);
		Context->SetStringField(TEXT("folder"), FString::Printf(TEXT("/Game/__N2CTest__Subsystem_%s"), *FDateTime::UtcNow().ToString(TEXT("%H%M%S"))));
		FString ContextText;
		FJsonSerializer::Serialize(Context.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ContextText));

		// The fixture's raised errors are caught in Python; outside Python the same calls would log them
		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		Command.Command = FString::Printf(TEXT("exec(compile(open(%s, encoding='utf-8').read(), 'n2c_editor_subsystem.py', 'exec'), ")
			TEXT("{'__name__': 'n2c_editor_subsystem', 'N2C_CTX': %s})"), *PythonString(Script), *PythonString(ContextText));
		const bool bRan = Python->ExecPythonCommandEx(Command);
		if (!TestTrue(FString::Printf(TEXT("the fixture runs (%s)"), *Command.CommandResult.Left(1500)), bRan))
		{
			return;
		}
		FString ReportText;
		if (!TestTrue(TEXT("the fixture writes its report"), FFileHelper::LoadFileToString(ReportText, *ReportPath)))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Report = N2CTest::ParseJsonObject(ReportText);
		if (!TestTrue(TEXT("the report is JSON"), Report.IsValid()))
		{
			return;
		}
		const FString Problem = Report->GetStringField(TEXT("problem"));
		TestTrue(FString::Printf(TEXT("the fixture runs to its end: %s"), *Problem), Problem.IsEmpty());
		const TArray<TSharedPtr<FJsonValue>>* Checks = nullptr;
		if (!TestTrue(TEXT("the report lists checks"), Report->TryGetArrayField(TEXT("checks"), Checks) && Checks->Num() >= 12))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Checks)
		{
			const TSharedPtr<FJsonObject> Check = Value->AsObject();
			TestTrue(FString::Printf(TEXT("%s %s"), *Check->GetStringField(TEXT("check")), *Check->GetStringField(TEXT("detail"))),
				Check->GetBoolField(TEXT("ok")));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
