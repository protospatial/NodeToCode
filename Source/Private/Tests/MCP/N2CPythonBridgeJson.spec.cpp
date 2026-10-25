// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTagManager.h"
#include "Models/N2CJsonContract.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Misc/FileHelper.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "IPythonScriptPlugin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

// The response helpers are private to the bridge; the graph state manager's folder override is private too (it keeps
// the tag cases' saves out of the real Saved/NodeToCode)
N2C_TEST_PRIVATE_MEMBER(FBridgeJsonMakeErrorJson, FString (*)(const FString&, const TSharedPtr<FJsonObject>&),
	&UN2CPythonBridge::MakeErrorJson)
N2C_TEST_PRIVATE_MEMBER(FBridgeJsonMakeSuccessJson, FString (*)(const TSharedPtr<FJsonObject>&),
	&UN2CPythonBridge::MakeSuccessJson)
N2C_TEST_PRIVATE_MEMBER(FBridgeJsonStateDirectory, FString UN2CGraphStateManager::*,
	&UN2CGraphStateManager::StateDirectoryOverride)

namespace N2CBridgeJsonSpec
{
	/** Text that breaks hand-built JSON: an apostrophe (UE escapes it as \', not a JSON escape), a double quote,
	 *  backslashes, a newline, a tab, non-ASCII and an emoji outside the BMP */
	static const TCHAR* Awkward = TEXT("it's \"q\" C:\\dir\\ line1\nline2\ttab caf\u00e9 \u6f22\u5b57 \U0001F600");

	/** Names for the Blueprint, graph, variable and pins: the same characters */
	static const TCHAR* BlueprintName = TEXT("BP_N2C it's \"q\" C:\\dir\\ line1\nline2 caf\u00e9 \U0001F600");
	static const TCHAR* FunctionName = TEXT("Fn it's \"q\" C:\\dir\\ line1\nline2 \u6f22\u5b57 \U0001F600");
	static const TCHAR* VariableName = TEXT("Var it's \"q\" C:\\dir\\ line1\nline2 caf\u00e9 \U0001F600");
	static const TCHAR* InputPinName = TEXT("In it's \"q\" C:\\dir\\ line1\nline2 caf\u00e9 \U0001F600");
	static const TCHAR* ReturnPinName = TEXT("Out it's \"q\" C:\\dir\\ line1\nline2 \u6f22\u5b57 \U0001F600");
	static const TCHAR* GraphGuidText = TEXT("A1B2C3D4-0000-0000-0000-000000000001");
}

/**
 * Every UN2CPythonBridge response is one JSON object {success, data, error} that a strict JSON parser reads, whatever
 * the names and values in it: the bridge builds it with a TJsonWriter, not Printf with ReplaceCharWithEscapedChar
 * (which wrote an apostrophe as \', so the nodetocode package's json.loads failed and the caller lost the real
 * result). Review ids: qw-14, python-bridge-1, imp-python-bridge-1 (card 0120).
 */
BEGIN_DEFINE_SPEC(FN2CPythonBridgeJsonSpec, "NodeToCode.Python.Bridge", N2C_TEST_FLAGS)

	/** Parses one bridge response; null (with a test error) when it is not a JSON object with success and error */
	TSharedPtr<FJsonObject> Parse(const FString& What, const FString& Json)
	{
		const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(Json);
		if (!TestTrue(FString::Printf(TEXT("%s returns a JSON object (got: %s)"), *What, *Json.Left(400)), Object.IsValid()))
		{
			return nullptr;
		}
		TestTrue(FString::Printf(TEXT("%s has a boolean success"), *What), Object->HasTypedField<EJson::Boolean>(TEXT("success")));
		TestTrue(FString::Printf(TEXT("%s has data"), *What), Object->HasField(TEXT("data")));
		TestTrue(FString::Printf(TEXT("%s has error"), *What), Object->HasField(TEXT("error")));
		return Object;
	}

	/** The data object of a successful response, or null (with a test error naming the response's error) */
	TSharedPtr<FJsonObject> SuccessData(const FString& What, const FString& Json)
	{
		const TSharedPtr<FJsonObject> Object = Parse(What, Json);
		if (!Object.IsValid())
		{
			return nullptr;
		}
		FString Error;
		Object->TryGetStringField(TEXT("error"), Error);
		if (!TestTrue(FString::Printf(TEXT("%s succeeds (error: %s)"), *What, *Error), Object->GetBoolField(TEXT("success"))))
		{
			return nullptr;
		}
		TestTrue(FString::Printf(TEXT("%s error is null"), *What), Object->HasTypedField<EJson::Null>(TEXT("error")));
		const TSharedPtr<FJsonObject>* Data = nullptr;
		return TestTrue(FString::Printf(TEXT("%s data is an object"), *What), Object->TryGetObjectField(TEXT("data"), Data))
			? *Data : nullptr;
	}

	/** The error text of a failed response, or empty (with a test error) */
	FString FailureError(const FString& What, const FString& Json)
	{
		const TSharedPtr<FJsonObject> Object = Parse(What, Json);
		if (!Object.IsValid())
		{
			return FString();
		}
		TestFalse(FString::Printf(TEXT("%s fails"), *What), Object->GetBoolField(TEXT("success")));
		FString Error;
		TestTrue(FString::Printf(TEXT("%s error is a string"), *What), Object->TryGetStringField(TEXT("error"), Error));
		return Error;
	}

	void ExpectString(const FString& What, const TSharedPtr<FJsonObject>& Data, const TCHAR* Field, const FString& Expected)
	{
		if (Data.IsValid())
		{
			FString Value;
			TestTrue(FString::Printf(TEXT("%s has %s"), *What, Field), Data->TryGetStringField(Field, Value));
			TestEqual(FString::Printf(TEXT("%s %s"), *What, Field), Value, Expected);
		}
	}

	/** Saves the graph state manager makes during a case go to a scratch folder, and the real folder comes back after */
	struct FScopedStateFolder
	{
		N2CTest::FScratchDir Scratch{ TEXT("N2CBridgeJson") };
		FString Previous;
		FScopedStateFolder()
		{
			FString& Override = UN2CGraphStateManager::Get().*GetPrivateMember(N2CTest::FBridgeJsonStateDirectory());
			Previous = Override;
			Override = Scratch.Path;
		}
		~FScopedStateFolder()
		{
			UN2CGraphStateManager::Get().*GetPrivateMember(N2CTest::FBridgeJsonStateDirectory()) = Previous;
		}
	};

	/** A Blueprint with a function graph, a member variable read in that graph and a Print String, open in an editor
	 *  with the function graph focused. Every name holds the awkward characters */
	struct FFocusedBlueprint
	{
		UPackage* Package = nullptr;
		TStrongObjectPtr<UBlueprint> Blueprint;
		UEdGraph* Function = nullptr;
		UK2Node_CallFunction* Print = nullptr;
		UK2Node_VariableGet* VariableGet = nullptr;
		bool bOpen = false;

		bool Create()
		{
			using namespace N2CBridgeJsonSpec;
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/N2CBridgeJsonSpec_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			Blueprint.Reset(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(BlueprintName),
				BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
			if (!Blueprint.IsValid())
			{
				return false;
			}

			FEdGraphPinType FloatType;
			FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
			FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), FName(VariableName), FloatType);

			Function = FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), FName(FunctionName), UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint.Get(), Function, /*bIsUserCreated*/ true, nullptr);

			FGraphNodeCreator<UK2Node_CallFunction> PrintCreator(*Function);
			Print = PrintCreator.CreateNode();
			Print->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
			Print->NodePosX = 400;
			PrintCreator.Finalize();

			FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Function);
			VariableGet = GetCreator.CreateNode();
			VariableGet->VariableReference.SetSelfMember(FName(VariableName));
			VariableGet->NodePosY = 200;
			GetCreator.Finalize();

			FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());

			TSharedPtr<IBlueprintEditor> Editor;
			FString OpenError;
			if (!FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint.Get(), Editor, OpenError) || !Editor.IsValid())
			{
				return false;
			}
			bOpen = true;
			Editor->JumpToHyperlink(Function, false);
			return FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor() == Function;
		}

		~FFocusedBlueprint()
		{
			if (Blueprint.IsValid())
			{
				for (const FN2CTaggedBlueprintGraph& Tag : UN2CTagManager::Get().GetTagsForGraph(Function ? Function->GraphGuid : FGuid()))
				{
					UN2CTagManager::Get().RemoveTag(Tag.GraphGuid, Tag.Tag, Tag.Category);
				}
				Package->SetDirtyFlag(false);
				if (bOpen && GEditor)
				{
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint.Get());
				}
				Blueprint->MarkAsGarbage();
			}
		}
	};

END_DEFINE_SPEC(FN2CPythonBridgeJsonSpec)

void FN2CPythonBridgeJsonSpec::Define()
{
	using namespace N2CBridgeJsonSpec;

	Describe(TEXT("the response helper"), [this]()
	{
		It(TEXT("writes an error with quotes, backslashes, newlines, apostrophes and emoji as valid JSON"), [this]()
		{
			const FString Message = FString::Printf(TEXT("Function '%s' not found"), Awkward);
			const FString Json = (*GetPrivateMember(N2CTest::FBridgeJsonMakeErrorJson()))(Message, nullptr);
			TestEqual(TEXT("error round-trips"), FailureError(TEXT("MakeErrorJson"), Json), Message);
			const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(Json);
			TestTrue(TEXT("data is null"), Object.IsValid() && Object->HasTypedField<EJson::Null>(TEXT("data")));
			TestFalse(TEXT("no \\' escape"), Json.Contains(TEXT("\\'")));
		});

		It(TEXT("writes awkward values in nested data as valid JSON with a null error"), [this]()
		{
			const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("name"), Awkward);
			const TSharedPtr<FJsonObject> Nested = MakeShared<FJsonObject>();
			Nested->SetStringField(TEXT("value"), Awkward);
			Data->SetObjectField(TEXT("nested"), Nested);
			Data->SetNumberField(TEXT("count"), 3);
			const FString Json = (*GetPrivateMember(N2CTest::FBridgeJsonMakeSuccessJson()))(Data);
			const TSharedPtr<FJsonObject> Out = SuccessData(TEXT("MakeSuccessJson"), Json);
			ExpectString(TEXT("MakeSuccessJson"), Out, TEXT("name"), Awkward);
			if (Out.IsValid())
			{
				TestEqual(TEXT("nested value"), Out->GetObjectField(TEXT("nested"))->GetStringField(TEXT("value")), FString(Awkward));
				TestEqual(TEXT("count"), Out->GetIntegerField(TEXT("count")), 3);
			}
			TestTrue(TEXT("count is written as an integer"), Json.Contains(TEXT("\"count\":3")));
		});
	});

	Describe(TEXT("calls that need no editor"), [this]()
	{
		It(TEXT("RemoveTag reports a missing awkward tag with its name"), [this]()
		{
			FScopedStateFolder StateFolder;
			const FString Error = FailureError(TEXT("RemoveTag"), UN2CPythonBridge::RemoveTag(GraphGuidText, Awkward));
			TestEqual(TEXT("error"), Error, FString::Printf(TEXT("Tag '%s' not found on graph %s"), Awkward, GraphGuidText));
		});

		// Card 0135: the v1 reader is reachable from Python (Tests/Migration reads a 1.2.3 save with it)
		It(TEXT("ReadN2CJson reads a v1 document into the current version and refuses text that is not N2C JSON"), [this]()
		{
			FString V1;
			if (TestTrue(TEXT("the v1 fixture loads"), FFileHelper::LoadFileToString(V1, *(N2CTest::FixturesDir(TEXT("Capture/V1")) / TEXT("FanOutData.json")))))
			{
				const TSharedPtr<FJsonObject> Data = SuccessData(TEXT("ReadN2CJson"), UN2CPythonBridge::ReadN2CJson(V1));
				ExpectString(TEXT("ReadN2CJson"), Data, TEXT("source_version"), TEXT("1.0.0"));
				ExpectString(TEXT("ReadN2CJson"), Data, TEXT("version"), N2CJsonContract::Version);
				if (Data.IsValid())
				{
					TestTrue(TEXT("valid"), Data->GetBoolField(TEXT("valid")));
					TestEqual(TEXT("graphs"), static_cast<int32>(Data->GetNumberField(TEXT("graphs"))), 1);
					TestEqual(TEXT("nodes"), static_cast<int32>(Data->GetNumberField(TEXT("nodes"))), 4);
					// The v1 document kept one edge per output: three
					TestEqual(TEXT("data_edges"), static_cast<int32>(Data->GetNumberField(TEXT("data_edges"))), 3);
					const TSharedPtr<FJsonObject>* N2CJson = nullptr;
					if (TestTrue(TEXT("n2c_json is an object"), Data->TryGetObjectField(TEXT("n2c_json"), N2CJson)))
					{
						TestEqual(TEXT("n2c_json version"), (*N2CJson)->GetStringField(TEXT("version")), FString(N2CJsonContract::Version));
					}
				}
			}
			TestFalse(TEXT("ReadN2CJson of text that is not JSON"), FailureError(TEXT("ReadN2CJson"), UN2CPythonBridge::ReadN2CJson(Awkward)).IsEmpty());
			AddExpectedError(TEXT("Missing version field in JSON"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("ReadN2CJson of JSON that is not N2C JSON"), FailureError(TEXT("ReadN2CJson"), UN2CPythonBridge::ReadN2CJson(TEXT("{\"name\":\"x\"}"))).IsEmpty());
		});

		It(TEXT("RemoveTag reports an awkward invalid GUID"), [this]()
		{
			const FString Error = FailureError(TEXT("RemoveTag"), UN2CPythonBridge::RemoveTag(Awkward, TEXT("x")));
			TestEqual(TEXT("error"), Error, FString::Printf(TEXT("Invalid GUID format: %s"), Awkward));
		});

		It(TEXT("ListTags and RemoveTag round-trip awkward tags, categories, descriptions, graph names and paths"), [this]()
		{
			FScopedStateFolder StateFolder;
			const FGuid GraphGuid = FGuid::NewGuid();
			const FN2CTaggedBlueprintGraph Tagged(Awkward, Awkward, Awkward, GraphGuid, FunctionName,
				FSoftObjectPath(FString::Printf(TEXT("/Game/N2CTest/%s.%s"), TEXT("BP_Quote"), TEXT("BP_Quote"))));
			if (!TestTrue(TEXT("tag added"), UN2CTagManager::Get().AddTag(Tagged)))
			{
				return;
			}

			const TSharedPtr<FJsonObject> Listed = SuccessData(TEXT("ListTags"), UN2CPythonBridge::ListTags(Awkward, Awkward));
			const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
			if (Listed.IsValid() && TestTrue(TEXT("one tag listed"), Listed->TryGetArrayField(TEXT("tags"), Tags) && Tags->Num() == 1))
			{
				const TSharedPtr<FJsonObject> Tag = (*Tags)[0]->AsObject();
				ExpectString(TEXT("ListTags"), Tag, TEXT("tag"), Awkward);
				ExpectString(TEXT("ListTags"), Tag, TEXT("category"), Awkward);
				ExpectString(TEXT("ListTags"), Tag, TEXT("description"), Awkward);
				ExpectString(TEXT("ListTags"), Tag, TEXT("graph_name"), FunctionName);
				ExpectString(TEXT("ListTags"), Tag, TEXT("graph_guid"), GraphGuid.ToString());
				TestEqual(TEXT("count"), Listed->GetIntegerField(TEXT("count")), 1);
			}

			const TSharedPtr<FJsonObject> Removed = SuccessData(TEXT("RemoveTag"), UN2CPythonBridge::RemoveTag(GraphGuid.ToString(), Awkward));
			ExpectString(TEXT("RemoveTag"), Removed, TEXT("tag"), Awkward);
			if (Removed.IsValid())
			{
				TestEqual(TEXT("removed_count"), Removed->GetIntegerField(TEXT("removed_count")), 1);
				TestEqual(TEXT("remaining_tags"), Removed->GetIntegerField(TEXT("remaining_tags")), 0);
			}
			for (const FN2CTaggedBlueprintGraph& Left : UN2CTagManager::Get().GetTagsForGraph(GraphGuid))
			{
				UN2CTagManager::Get().RemoveTag(Left.GraphGuid, Left.Tag, Left.Category);
			}
		});

		It(TEXT("GetActiveProvider and GetLLMProviders write an awkward model and endpoint as valid JSON"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			const EN2CLLMProvider OldProvider = Settings->Provider;
			const FString OldModel = Settings->OllamaModel;
			const FString OldEndpoint = Settings->OllamaConfig.OllamaEndpoint;
			Settings->Provider = EN2CLLMProvider::Ollama;
			Settings->OllamaModel = Awkward;
			Settings->OllamaConfig.OllamaEndpoint = Awkward;

			const TSharedPtr<FJsonObject> Active = SuccessData(TEXT("GetActiveProvider"), UN2CPythonBridge::GetActiveProvider());
			const TSharedPtr<FJsonObject> All = SuccessData(TEXT("GetLLMProviders"), UN2CPythonBridge::GetLLMProviders());

			Settings->Provider = OldProvider;
			Settings->OllamaModel = OldModel;
			Settings->OllamaConfig.OllamaEndpoint = OldEndpoint;

			ExpectString(TEXT("GetActiveProvider"), Active, TEXT("model"), Awkward);
			ExpectString(TEXT("GetActiveProvider"), Active, TEXT("endpoint"), Awkward);
			if (Active.IsValid())
			{
				TestTrue(TEXT("is_local"), Active->GetBoolField(TEXT("is_local")));
			}
			ExpectString(TEXT("GetLLMProviders"), All, TEXT("current_provider"), TEXT("Ollama"));
			if (All.IsValid())
			{
				TestEqual(TEXT("provider_count"), All->GetIntegerField(TEXT("provider_count")), 6);
			}
		});

		It(TEXT("OpenBlueprint reports an awkward missing path as valid JSON"), [this]()
		{
			const FString Path = FString::Printf(TEXT("/Game/N2CTest/%s"), Awkward);
			const FString Error = FailureError(TEXT("OpenBlueprint"), UN2CPythonBridge::OpenBlueprint(Path, Awkward));
			TestTrue(TEXT("error says the Blueprint failed to load"), Error.StartsWith(TEXT("Failed to load Blueprint")));
		});
	});

	Describe(TEXT("calls on a focused Blueprint with awkward names"), [this]()
	{
		It(TEXT("return the Blueprint, graph, node, pin and variable names as written"), [this]()
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
			// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor's window (closing it, or a later Slate
			// layout pass while it is open, asks the null native window for its restored size: a fatal error in
			// GenericWindow.cpp, "GetRestoredDimensions is not expected to be called on this platform"). This case runs
			// on 5.8; on 5.6 and 5.7 the same calls are checked live through run-python
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
			return;
#endif
			FScopedStateFolder StateFolder;
			FFocusedBlueprint Focused;
			if (!TestTrue(TEXT("the awkward Blueprint is open with its function graph focused"), Focused.Create()))
			{
				return;
			}
			const FString BlueprintObjectName = Focused.Blueprint->GetName();
			const FString GraphName = Focused.Function->GetName();
			TestEqual(TEXT("Blueprint name kept"), BlueprintObjectName, FString(BlueprintName));
			TestEqual(TEXT("graph name kept"), GraphName, FString(FunctionName));

			// Reading
			const TSharedPtr<FJsonObject> Focus = SuccessData(TEXT("GetFocusedBlueprintJson"), UN2CPythonBridge::GetFocusedBlueprintJson());
			ExpectString(TEXT("GetFocusedBlueprintJson"), Focus, TEXT("name"), BlueprintObjectName);
			ExpectString(TEXT("GetFocusedBlueprintJson"), Focus, TEXT("path"), Focused.Blueprint->GetPathName());
			ExpectString(TEXT("GetFocusedBlueprintJson"), Focus, TEXT("graph_name"), GraphName);
			if (Focus.IsValid())
			{
				TestTrue(TEXT("n2c_json is an object"), Focus->HasTypedField<EJson::Object>(TEXT("n2c_json")));
				TestTrue(TEXT("node_count"), Focus->GetIntegerField(TEXT("node_count")) > 0);
			}

			const TSharedPtr<FJsonObject> Opened = SuccessData(TEXT("OpenBlueprintFunction"), UN2CPythonBridge::OpenBlueprintFunction(GraphName));
			ExpectString(TEXT("OpenBlueprintFunction"), Opened, TEXT("functionName"), GraphName);
			ExpectString(TEXT("OpenBlueprintFunction"), Opened, TEXT("blueprintName"), BlueprintObjectName);

			const FString Missing = FString::Printf(TEXT("Missing %s"), Awkward);
			const FString MissingError = FailureError(TEXT("OpenBlueprintFunction (missing)"), UN2CPythonBridge::OpenBlueprintFunction(Missing));
			TestTrue(TEXT("missing-function error names the function"), MissingError.StartsWith(FString::Printf(TEXT("Function '%s' not found"), *Missing)));
			TestTrue(TEXT("missing-function error lists the awkward function"), MissingError.Contains(GraphName));

			const TSharedPtr<FJsonObject> Found = SuccessData(TEXT("FindNodesInGraph"),
				UN2CPythonBridge::FindNodesInGraph(TEXT("[\"Print\"]"), TEXT("keyword"), false, 50));
			ExpectString(TEXT("FindNodesInGraph metadata"), Found.IsValid() ? Found->GetObjectField(TEXT("metadata")) : nullptr,
				TEXT("blueprintName"), BlueprintObjectName);
			ExpectString(TEXT("FindNodesInGraph metadata"), Found.IsValid() ? Found->GetObjectField(TEXT("metadata")) : nullptr,
				TEXT("graphName"), GraphName);

			const FString VariableGuid = Focused.VariableGet->NodeGuid.ToString();
			const TSharedPtr<FJsonObject> ByGuid = SuccessData(TEXT("FindNodesInGraph (guid)"),
				UN2CPythonBridge::FindNodesInGraph(FString::Printf(TEXT("[\"%s\"]"), *VariableGuid), TEXT("guid"), false, 50));
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (ByGuid.IsValid() && TestTrue(TEXT("the variable node is found"), ByGuid->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes->Num() == 1))
			{
				ExpectString(TEXT("variable node"), (*Nodes)[0]->AsObject(), TEXT("nodeName"),
					Focused.VariableGet->GetNodeTitle(ENodeTitleType::ListView).ToString());
				const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
				if (TestTrue(TEXT("variable node has an output pin"), (*Nodes)[0]->AsObject()->TryGetArrayField(TEXT("outputPins"), Outputs) && Outputs->Num() > 0))
				{
					ExpectString(TEXT("variable pin"), (*Outputs)[0]->AsObject(), TEXT("pinName"), VariableName);
				}
			}

			// Editing
			UEdGraphPin* InString = Focused.Print->FindPin(TEXT("InString"));
			if (TestNotNull(TEXT("Print String has InString"), InString))
			{
				const TSharedPtr<FJsonObject> Set = SuccessData(TEXT("SetPinValue"), UN2CPythonBridge::SetPinValue(
					Focused.Print->NodeGuid.ToString(), InString->PinId.ToString(), Awkward, FString()));
				ExpectString(TEXT("SetPinValue"), Set, TEXT("newValue"), Awkward);
				ExpectString(TEXT("SetPinValue"), Set, TEXT("oldValue"), TEXT("Hello"));
			}

			const FString PrintGuidArray = FString::Printf(TEXT("[\"%s\"]"), *Focused.Print->NodeGuid.ToString());
			const TSharedPtr<FJsonObject> Comment = SuccessData(TEXT("CreateCommentNode"),
				UN2CPythonBridge::CreateCommentNode(PrintGuidArray, Awkward, 0.1f, 0.2f, 0.3f, 18, TEXT("group"), 50.0f));
			ExpectString(TEXT("CreateCommentNode"), Comment, TEXT("commentText"), Awkward);
			if (Comment.IsValid())
			{
				TestEqual(TEXT("includedNodeCount"), Comment->GetIntegerField(TEXT("includedNodeCount")), 1);
				TestTrue(TEXT("size.width"), Comment->GetObjectField(TEXT("size"))->GetIntegerField(TEXT("width")) > 0);
				TestTrue(TEXT("position.x is a number"), Comment->GetObjectField(TEXT("position"))->HasTypedField<EJson::Number>(TEXT("x")));
			}

			const TSharedPtr<FJsonObject> Input = SuccessData(TEXT("AddFunctionInputPin"),
				UN2CPythonBridge::AddFunctionInputPin(InputPinName, TEXT("float"), TEXT("1.5"), false, Awkward));
			ExpectString(TEXT("AddFunctionInputPin"), Input, TEXT("pinName"), InputPinName);
			ExpectString(TEXT("AddFunctionInputPin"), Input, TEXT("functionName"), GraphName);
			FString InputActual;
			if (Input.IsValid())
			{
				InputActual = Input->GetStringField(TEXT("actualName"));
			}

			const TSharedPtr<FJsonObject> Output = SuccessData(TEXT("AddFunctionReturnPin"),
				UN2CPythonBridge::AddFunctionReturnPin(ReturnPinName, TEXT("float"), Awkward));
			ExpectString(TEXT("AddFunctionReturnPin"), Output, TEXT("pinName"), ReturnPinName);
			FString OutputActual;
			if (Output.IsValid())
			{
				OutputActual = Output->GetStringField(TEXT("actualName"));
			}

			const TSharedPtr<FJsonObject> Tagged = SuccessData(TEXT("TagFocusedGraph"), UN2CPythonBridge::TagFocusedGraph(Awkward, Awkward, Awkward));
			ExpectString(TEXT("TagFocusedGraph"), Tagged, TEXT("tag"), Awkward);
			ExpectString(TEXT("TagFocusedGraph"), Tagged, TEXT("description"), Awkward);
			ExpectString(TEXT("TagFocusedGraph"), Tagged, TEXT("graph_name"), GraphName);
			ExpectString(TEXT("TagFocusedGraph"), Tagged, TEXT("blueprint_name"), BlueprintObjectName);
			const TSharedPtr<FJsonObject> Untagged = SuccessData(TEXT("RemoveTag"),
				UN2CPythonBridge::RemoveTag(Focused.Function->GraphGuid.ToString(), Awkward));
			ExpectString(TEXT("RemoveTag"), Untagged, TEXT("tag"), Awkward);

			const TSharedPtr<FJsonObject> Compiled = Parse(TEXT("CompileFocusedBlueprint"), UN2CPythonBridge::CompileFocusedBlueprint());
			const TSharedPtr<FJsonObject>* CompiledData = nullptr;
			if (Compiled.IsValid() && TestTrue(TEXT("compile data is an object"), Compiled->TryGetObjectField(TEXT("data"), CompiledData)))
			{
				ExpectString(TEXT("CompileFocusedBlueprint"), *CompiledData, TEXT("blueprint_name"), BlueprintObjectName);
			}

			// Not dirty, so nothing is written: the Blueprint lives in /Temp
			Focused.Package->SetDirtyFlag(false);
			const TSharedPtr<FJsonObject> Saved = SuccessData(TEXT("SaveFocusedBlueprint"), UN2CPythonBridge::SaveFocusedBlueprint(true));
			ExpectString(TEXT("SaveFocusedBlueprint"), Saved, TEXT("blueprint_name"), BlueprintObjectName);
			if (Saved.IsValid())
			{
				TestFalse(TEXT("was_saved"), Saved->GetBoolField(TEXT("was_saved")));
			}

			if (!InputActual.IsEmpty())
			{
				const TSharedPtr<FJsonObject> Removed = SuccessData(TEXT("RemoveFunctionEntryPin"), UN2CPythonBridge::RemoveFunctionEntryPin(InputActual));
				ExpectString(TEXT("RemoveFunctionEntryPin"), Removed, TEXT("removedPin"), InputActual);
				ExpectString(TEXT("RemoveFunctionEntryPin"), Removed, TEXT("functionName"), GraphName);
			}
			if (!OutputActual.IsEmpty())
			{
				const TSharedPtr<FJsonObject> Removed = SuccessData(TEXT("RemoveFunctionReturnPin"), UN2CPythonBridge::RemoveFunctionReturnPin(OutputActual));
				ExpectString(TEXT("RemoveFunctionReturnPin"), Removed, TEXT("removedPin"), OutputActual);
			}
			const FString MissingPin = FailureError(TEXT("RemoveFunctionEntryPin (missing)"), UN2CPythonBridge::RemoveFunctionEntryPin(Awkward));
			TestFalse(TEXT("missing-pin error is not empty"), MissingPin.IsEmpty());

			const TSharedPtr<FJsonObject> Connected = SuccessData(TEXT("ConnectPins"), UN2CPythonBridge::ConnectPins(FString::Printf(
				TEXT("[{\"from\":{\"nodeGuid\":\"%s\",\"pinGuid\":\"\",\"pinName\":\"no such pin\"},\"to\":{\"nodeGuid\":\"%s\",\"pinGuid\":\"\",\"pinName\":\"InString\"}}]"),
				*VariableGuid, *Focused.Print->NodeGuid.ToString()), false));
			if (Connected.IsValid())
			{
				TestEqual(TEXT("one failed connection"), Connected->GetObjectField(TEXT("summary"))->GetIntegerField(TEXT("failed")), 1);
			}

			const TSharedPtr<FJsonObject> Deleted = SuccessData(TEXT("DeleteNodes"),
				UN2CPythonBridge::DeleteNodes(FString::Printf(TEXT("[\"%s\"]"), *VariableGuid), false, false));
			const TArray<TSharedPtr<FJsonValue>>* DeletedNodes = nullptr;
			if (Deleted.IsValid() && TestTrue(TEXT("one node deleted"), Deleted->TryGetArrayField(TEXT("deletedNodes"), DeletedNodes) && DeletedNodes->Num() == 1))
			{
				ExpectString(TEXT("DeleteNodes"), (*DeletedNodes)[0]->AsObject(), TEXT("guid"), VariableGuid);
			}

			const TSharedPtr<FJsonObject> Searched = SuccessData(TEXT("SearchBlueprintNodes"),
				UN2CPythonBridge::SearchBlueprintNodes(TEXT("Print String"), true, 5, FString(), true));
			const TArray<TSharedPtr<FJsonValue>>* SearchNodes = nullptr;
			if (Searched.IsValid() && TestTrue(TEXT("search finds Print String"), Searched->TryGetArrayField(TEXT("nodes"), SearchNodes) && SearchNodes->Num() > 0))
			{
				const TSharedPtr<FJsonObject> First = (*SearchNodes)[0]->AsObject();
				const FString Action = First->GetObjectField(TEXT("spawnMetadata"))->GetStringField(TEXT("actionIdentifier"));
				const TSharedPtr<FJsonObject> Added = SuccessData(TEXT("AddNodeToGraph"), UN2CPythonBridge::AddNodeToGraph(
					TEXT("Print String"), Action, 800.0f, 0.0f));
				ExpectString(TEXT("AddNodeToGraph"), Added, TEXT("graphName"), GraphName);
				ExpectString(TEXT("AddNodeToGraph"), Added, TEXT("blueprintName"), BlueprintObjectName);
			}
		});
	});

	Describe(TEXT("through the nodetocode package"), [this]()
	{
		It(TEXT("n2c.remove_tag returns the bridge error with the apostrophe instead of a parse failure"), [this]()
		{
			IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
			if (!TestTrue(TEXT("Python is available"), Python && Python->IsPythonAvailable())) { return; }
			FScopedStateFolder StateFolder;
			const FMcpToolCallResult Result = N2CTest::CallTool(TEXT("run-python"), { { TEXT("code"), FString::Printf(TEXT(
				"import nodetocode as n2c\n"
				"r = n2c.remove_tag('%s', \"it's \\\"q\\\" \\\\ caf\\u00e9 \\U0001F600\")\n"
				"t = n2c.list_tags()\n"
				"result = {'remove': r, 'list_success': t['success'], 'list_error': t['error']}\n"), GraphGuidText) } });
			const TSharedPtr<FJsonObject> Body = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
			if (!TestTrue(TEXT("run-python answers with JSON"), Body.IsValid()) || !TestTrue(TEXT("the script ran"), Body->GetBoolField(TEXT("success"))))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Data = Body->GetObjectField(TEXT("data"));
			const TSharedPtr<FJsonObject> Remove = Data->GetObjectField(TEXT("remove"));
			TestFalse(TEXT("remove_tag success"), Remove->GetBoolField(TEXT("success")));
			TestEqual(TEXT("remove_tag error"), Remove->GetStringField(TEXT("error")),
				FString::Printf(TEXT("Tag 'it's \"q\" \\ caf\u00e9 \U0001F600' not found on graph %s"), GraphGuidText));
			FString ListError;
			Data->TryGetStringField(TEXT("list_error"), ListError);
			TestTrue(FString::Printf(TEXT("list_tags success (error: %s)"), *ListError), Data->GetBoolField(TEXT("list_success")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
