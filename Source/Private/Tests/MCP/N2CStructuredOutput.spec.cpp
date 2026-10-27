// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "Core/N2CCapture.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpGetFocusedBlueprintTool.h"
#include "MCP/Tools/Implementations/Translation/N2CTranslateBlueprintCall.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CStructuredOutputSpec
{
	/** The tools card 0207 gives an outputSchema (unfrozen discovery, translation and file system tools) */
	const TCHAR* const ToolsWithSchema[] = {
		TEXT("get-focused-blueprint"), TEXT("list-blueprint-graphs"), TEXT("get-open-blueprint-editors"),
		TEXT("translate-focused-blueprint"), TEXT("get-available-translation-targets"), TEXT("get-available-llm-providers"),
		TEXT("get-translation-output-directory"), TEXT("read-path"),
	};

	FString Condensed(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		if (Object.IsValid())
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		return Text;
	}

	FString FirstText(const FMcpToolCallResult& Result)
	{
		FString Text;
		for (const TSharedPtr<FJsonObject>& Item : Result.Content)
		{
			FString Type;
			if (Item.IsValid() && Item->TryGetStringField(TEXT("type"), Type) && Type == TEXT("text") && Item->TryGetStringField(TEXT("text"), Text))
			{
				break;
			}
		}
		return Text;
	}

	/** A translation as the LLM module returns it: one graph whose implementation is CodeChars characters long */
	FString CannedTranslation(int32 CodeChars)
	{
		const TSharedRef<FJsonObject> Code = MakeShared<FJsonObject>();
		Code->SetStringField(TEXT("graphDeclaration"), TEXT("void OnDoorOpened();"));
		Code->SetStringField(TEXT("graphImplementation"), TEXT("void AMyDoor::OnDoorOpened() { /* </script> */ }") + FString::ChrN(CodeChars, TEXT('x')));
		Code->SetStringField(TEXT("implementationNotes"), TEXT("Notes"));
		const TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
		Graph->SetStringField(TEXT("graph_name"), TEXT("OnDoorOpened"));
		Graph->SetStringField(TEXT("graph_type"), TEXT("Function"));
		Graph->SetStringField(TEXT("graph_class"), TEXT("BP_Door"));
		Graph->SetObjectField(TEXT("code"), Code);
		const TSharedRef<FJsonObject> Usage = MakeShared<FJsonObject>();
		Usage->SetNumberField(TEXT("input_tokens"), 120);
		Usage->SetNumberField(TEXT("output_tokens"), 80);
		const TSharedRef<FJsonObject> Translation = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Graphs;
		Graphs.Add(MakeShared<FJsonValueObject>(Graph));
		Translation->SetArrayField(TEXT("graphs"), Graphs);
		Translation->SetObjectField(TEXT("usage"), Usage);
		return Condensed(Translation);
	}
}

/**
 * Card 0207: tools that declare an outputSchema return structuredContent that matches it, with the same JSON as text
 * (both marked when the tool returns Blueprint text); a translation over the result limit is a saved file with a
 * resource_link; every isError result carries a stable code in _meta["nodetocode/error"], and tools/call answers
 * arguments that are not an object with one instead of a JSON-RPC error.
 */
BEGIN_DEFINE_SPEC(FN2CStructuredOutputSpec, "NodeToCode.MCP.StructuredOutput", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	TUniquePtr<N2CTest::FScratchDir> Scratch;

	TSharedPtr<FJsonObject> SchemaOf(const FString& Name)
	{
		const TOptional<FN2CToolDescriptor> Descriptor = FN2CToolCatalog::Get().Find(Name);
		if (Descriptor.IsSet() && Descriptor->OutputSchema.IsValid())
		{
			return Descriptor->OutputSchema;
		}
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (Tool.IsValid() && Tool->GetDefinition().Name == Name)
			{
				return Tool->GetDefinition().OutputSchema;
			}
		}
		return nullptr;
	}

	/** A successful result whose structuredContent matches Name's outputSchema and whose text is the same JSON */
	bool ExpectStructured(const FString& Name, const FMcpToolCallResult& Result)
	{
		using namespace N2CStructuredOutputSpec;
		if (!TestFalse(Name + TEXT(": succeeds: ") + FirstText(Result), Result.bIsError)
			|| !TestTrue(Name + TEXT(": has structuredContent"), Result.StructuredContent.IsValid()))
		{
			return false;
		}
		TArray<FString> Errors;
		N2CMcpOutputSchema::Validate(SchemaOf(Name), MakeShared<FJsonValueObject>(Result.StructuredContent), Errors);
		TestEqual(Name + TEXT(": matches its outputSchema: ") + FString::Join(Errors, TEXT("; ")), Errors.Num(), 0);
		TestEqual(Name + TEXT(": the text block is the same JSON"), Condensed(N2CMcpOutputSchema::ParseObject(FirstText(Result))),
			Condensed(Result.StructuredContent));
		TestTrue(Name + TEXT(": serialized with structuredContent"), Result.ToJson()->HasTypedField<EJson::Object>(TEXT("structuredContent")));
		return Errors.Num() == 0;
	}

	/** Runs translate-focused-blueprint's call with the LLM answering Response at once */
	FMcpToolCallResult Translate(const FString& Response)
	{
		const TSharedRef<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> Call = MakeShared<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe>(
			FN2CToolCallContext::MakeDetached(), FString(), FString(), FString());
		Call->SetRequestStarter([Response](const FString&, const FString&, const FString&, const FOnLLMResponseReceived& OnComplete)
		{
			const FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
			Handle->Complete(OnComplete, Response);
			return Handle;
		});
		TFuture<FMcpToolCallResult> Future = Call->Start();
		return TestTrue(TEXT("the call finished"), Future.IsReady()) ? Future.Get() : FMcpToolCallResult::CreateErrorResult(TEXT("not finished"));
	}

END_DEFINE_SPEC(FN2CStructuredOutputSpec)

void FN2CStructuredOutputSpec::Define()
{
	using namespace N2CStructuredOutputSpec;

	BeforeEach([this]()
	{
		Test = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CStructured"));
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CStructuredOutput"));
		N2CMcpBlueprintExport::SetExportDirectoryForTests(Scratch->Path);
	});

	AfterEach([this]()
	{
		N2CMcpBlueprintExport::SetExportDirectoryForTests(FString());
		Scratch.Reset();
		Test.Reset();
	});

	It(TEXT("declares an object outputSchema on exactly the tools card 0207 covers, and lists it in tools/list"), [this]()
	{
		TSet<FString> Expected;
		for (const TCHAR* Name : ToolsWithSchema)
		{
			Expected.Add(Name);
			const TSharedPtr<FJsonObject> Schema = SchemaOf(Name);
			if (TestTrue(FString::Printf(TEXT("%s has an outputSchema"), Name), Schema.IsValid()))
			{
				TestEqual(FString::Printf(TEXT("%s: its type is object"), Name), Schema->GetStringField(TEXT("type")), FString(TEXT("object")));
			}
		}
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (!Tool.IsValid())
			{
				continue;
			}
			const FMcpToolDefinition Definition = Tool->GetDefinition();
			if (Definition.OutputSchema.IsValid())
			{
				TestTrue(FString::Printf(TEXT("%s is in this spec's list (add a sample case for a new tool with an outputSchema)"), *Definition.Name),
					Expected.Contains(Definition.Name));
				TestTrue(FString::Printf(TEXT("%s: tools/list carries outputSchema"), *Definition.Name),
					Definition.ToJson()->HasTypedField<EJson::Object>(TEXT("outputSchema")));
			}
		}
	});

	It(TEXT("returns structuredContent matching the schema from the discovery, translation-settings and file tools"), [this]()
	{
		const TSharedPtr<FJsonObject> GraphArgs = MakeShared<FJsonObject>();
		GraphArgs->SetStringField(TEXT("blueprintPath"), Test->Path());
		const FMcpToolCallResult Graphs = N2CTest::RunTool(TEXT("list-blueprint-graphs"), GraphArgs);
		if (ExpectStructured(TEXT("list-blueprint-graphs"), Graphs))
		{
			TestTrue(TEXT("list-blueprint-graphs: structuredContent is marked as untrusted, like its text"),
				Graphs.StructuredContent->HasField(FN2CMcpUntrustedContent::MarkerField) && N2CTest::IsMarkedUntrusted(Graphs));
			TestTrue(TEXT("list-blueprint-graphs: lists the Blueprint's graphs"), Graphs.StructuredContent->GetArrayField(TEXT("graphs")).Num() > 0);
		}

		ExpectStructured(TEXT("get-open-blueprint-editors"), N2CTest::RunTool(TEXT("get-open-blueprint-editors"), MakeShared<FJsonObject>()));
		ExpectStructured(TEXT("get-available-translation-targets"), N2CTest::RunTool(TEXT("get-available-translation-targets"), MakeShared<FJsonObject>()));
		ExpectStructured(TEXT("get-available-llm-providers"), N2CTest::RunTool(TEXT("get-available-llm-providers"), MakeShared<FJsonObject>()));
		ExpectStructured(TEXT("get-translation-output-directory"), N2CTest::RunTool(TEXT("get-translation-output-directory"), MakeShared<FJsonObject>()));

		const TSharedPtr<FJsonObject> PathArgs = MakeShared<FJsonObject>();
		PathArgs->SetStringField(TEXT("relativePath"), TEXT("Config"));
		PathArgs->SetNumberField(TEXT("pageSize"), 1);
		const FMcpToolCallResult Listing = N2CTest::RunTool(TEXT("read-path"), PathArgs);
		if (ExpectStructured(TEXT("read-path"), Listing))
		{
			TestTrue(TEXT("read-path: a one-entry page of Config is truncated, with a next cursor"),
				Listing.StructuredContent->GetBoolField(TEXT("truncated")) && Listing.StructuredContent->HasField(TEXT("nextCursor")));
		}
	});

	It(TEXT("returns get-focused-blueprint's N2C JSON document as structuredContent matching the schema"), [this]()
	{
		// The document get-focused-blueprint returns for a graph (FN2CMcpGetFocusedBlueprintTool::BuildResultJson), on
		// every engine
		Test->AddCustomEvent(Test->EventGraph(), TEXT("OnStructured"));
		TValueOrError<FN2CCapture, FString> Capture = N2CMcpBlueprintExport::CaptureGraph(*Test->EventGraph());
		if (!TestFalse(TEXT("captures the event graph"), Capture.HasError()))
		{
			return;
		}
		TArray<FString> Errors;
		N2CMcpOutputSchema::Validate(SchemaOf(TEXT("get-focused-blueprint")),
			MakeShared<FJsonValueObject>(N2CMcpOutputSchema::ParseObject(FN2CMcpGetFocusedBlueprintTool::BuildResultJson(Capture.GetValue()))), Errors);
		TestEqual(TEXT("the document matches the schema: ") + FString::Join(Errors, TEXT("; ")), Errors.Num(), 0);

		// The tool itself, with the graph focused (where a headless Blueprint editor can be closed again)
		if (Test->OpenInEditor(*this, Test->EventGraph()))
		{
			const FMcpToolCallResult Focused = N2CTest::RunTool(TEXT("get-focused-blueprint"), MakeShared<FJsonObject>());
			if (ExpectStructured(TEXT("get-focused-blueprint"), Focused))
			{
				TestTrue(TEXT("get-focused-blueprint: structuredContent is marked as untrusted"),
					Focused.StructuredContent->HasField(FN2CMcpUntrustedContent::MarkerField));
			}
		}
	});

	It(TEXT("returns a translation as structuredContent matching translate-focused-blueprint's schema, marked as untrusted"), [this]()
	{
		const FMcpToolCallResult Result = Translate(CannedTranslation(10));
		if (ExpectStructured(TEXT("translate-focused-blueprint"), Result))
		{
			TestTrue(TEXT("structuredContent carries the notice"), Result.StructuredContent->HasField(FN2CMcpUntrustedContent::MarkerField));
			TestTrue(TEXT("the text is marked"), FN2CMcpUntrustedContent::IsMarked(FirstText(Result)));
			TestFalse(TEXT("closing tags are escaped in the text"), FirstText(Result).Contains(TEXT("</script>")));
			TestEqual(TEXT("one content item: no resource_link"), Result.Content.Num(), 1);
		}
		const TOptional<FN2CToolDescriptor> Descriptor = FN2CToolCatalog::Get().Find(TEXT("translate-focused-blueprint"));
		if (TestTrue(TEXT("translate-focused-blueprint is registered"), Descriptor.IsSet() && Descriptor->Source.IsValid()))
		{
			TestEqual(TEXT("MaxInlineChars is the tool's maxResultSizeChars"), Descriptor->Source->MaxResultSizeChars.Get(0),
				FN2CTranslateBlueprintCall::MaxInlineChars);
		}

		// A provider error is an isError result with the error's kind as its code
		const FMcpToolCallResult Failed = Translate(TEXT("{\"error\":\"Rate limited\",\"error_kind\":\"RateLimited\",\"message\":\"slow down\"}"));
		TestTrue(TEXT("a provider error is isError"), Failed.bIsError);
		TestEqual(TEXT("with its kind as the code"), Failed.GetErrorCode(), FString(TEXT("TRANSLATION_RATE_LIMITED")));
		TestFalse(TEXT("and no structuredContent"), Failed.StructuredContent.IsValid());
	});

	It(TEXT("saves a translation larger than the result limit and returns its summary with a resource_link"), [this]()
	{
		const TOptional<FMcpToolCallResult> Saved = FN2CTranslateBlueprintCall::BuildSuccessResult(CannedTranslation(5000), /*MaxInline*/ 1000);
		if (!TestTrue(TEXT("a result"), Saved.IsSet()) || !ExpectStructured(TEXT("translate-focused-blueprint"), Saved.GetValue()))
		{
			return;
		}
		const FMcpToolCallResult& Result = Saved.GetValue();
		const TSharedPtr<FJsonObject>& Summary = Result.StructuredContent;
		TestTrue(TEXT("the summary is under the limit"), FirstText(Result).Len() <= 1000);
		TestTrue(TEXT("the summary names savedTo"), Summary->HasTypedField<EJson::String>(TEXT("savedTo")));
		const TArray<TSharedPtr<FJsonValue>> Graphs = Summary->GetArrayField(TEXT("graphs"));
		if (TestEqual(TEXT("one graph"), Graphs.Num(), 1))
		{
			TestEqual(TEXT("its name"), Graphs[0]->AsObject()->GetStringField(TEXT("graph_name")), FString(TEXT("OnDoorOpened")));
			TestFalse(TEXT("without its code"), Graphs[0]->AsObject()->HasField(TEXT("code")));
		}
		TestTrue(TEXT("usage is kept"), Summary->HasTypedField<EJson::Object>(TEXT("usage")));

		if (!TestEqual(TEXT("a text block and a resource_link"), Result.Content.Num(), 2))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Link = Result.Content[1];
		TestEqual(TEXT("resource_link type"), Link->GetStringField(TEXT("type")), FString(TEXT("resource_link")));
		FString Uri = Link->GetStringField(TEXT("uri"));
		TestTrue(TEXT("a file:// uri"), Uri.StartsWith(TEXT("file:///")));
		TestEqual(TEXT("its mimeType"), Link->GetStringField(TEXT("mimeType")), FString(TEXT("application/json")));
		FString FilePath = Uri.RightChop(FCString::Strlen(TEXT("file:///")));
#if !PLATFORM_WINDOWS
		FilePath = TEXT("/") + FilePath;
#endif
		TestTrue(TEXT("the file is in the export directory"), FPaths::IsUnderDirectory(FilePath, Scratch->Path));
		FString Saved2;
		if (TestTrue(TEXT("the file exists"), FFileHelper::LoadFileToString(Saved2, *FilePath)))
		{
			const TSharedPtr<FJsonObject> Whole = N2CMcpOutputSchema::ParseObject(Saved2);
			TestTrue(TEXT("the file holds the whole translation, marked"), Whole.IsValid() && Whole->HasField(FN2CMcpUntrustedContent::MarkerField)
				&& Whole->GetArrayField(TEXT("graphs"))[0]->AsObject()->GetObjectField(TEXT("code"))->GetStringField(TEXT("graphImplementation")).Len() > 5000);
			TestEqual(TEXT("size is the file's"), static_cast<int64>(Link->GetNumberField(TEXT("size"))), IFileManager::Get().FileSize(*FilePath));
		}
	});

	It(TEXT("gives every isError result a stable code, and refuses arguments that are not an object with one"), [this]()
	{
		// A required argument left out: the catalog's check
		const FMcpToolCallResult Missing = FN2CToolCatalog::Get().InvokeNow(TEXT("list-blueprint-graphs"), MakeShared<FJsonObject>()).ToCallToolResult();
		TestTrue(TEXT("missing argument: isError"), Missing.bIsError);
		TestEqual(TEXT("missing argument: INVALID_ARGUMENT"), Missing.GetErrorCode(), FString(TEXT("INVALID_ARGUMENT")));
		const TSharedPtr<FJsonObject> Meta = Missing.ToJson()->GetObjectField(TEXT("_meta"));
		TestEqual(TEXT("missing argument: _meta names the field"), Meta->GetObjectField(FMcpToolCallResult::ErrorMetaKey)->GetStringField(TEXT("field")),
			FString(TEXT("blueprintPath")));

		// A tool's own "CODE: message" error keeps its code; one without a code is TOOL_FAILED
		const TSharedPtr<FJsonObject> BadDepth = MakeShared<FJsonObject>();
		BadDepth->SetStringField(TEXT("blueprintPath"), Test->Path());
		BadDepth->SetNumberField(TEXT("depth"), 9);
		const FMcpToolCallResult Depth = FN2CToolCatalog::Get().InvokeNow(TEXT("get-blueprint-n2cjson"), BadDepth).ToCallToolResult();
		TestEqual(TEXT("the tool's own code"), Depth.GetErrorCode(), FString(TEXT("INVALID_ARGUMENT")));
		TestEqual(TEXT("CODE|message"), FMcpToolCallResult::ParseErrorCode(TEXT("PIE_ACTIVE|stop the session")), FString(TEXT("PIE_ACTIVE")));
		TestEqual(TEXT("no code in plain text"), FMcpToolCallResult::ParseErrorCode(TEXT("Missing required parameter: x")), FString());
		TestEqual(TEXT("no code in a lone word"), FMcpToolCallResult::ParseErrorCode(TEXT("A: b")), FString());
		const TSharedPtr<FJsonObject> NoFile = MakeShared<FJsonObject>();
		NoFile->SetStringField(TEXT("relativePath"), TEXT("Config/N2CNoSuchFolder"));
		const FMcpToolCallResult Plain = FN2CToolCatalog::Get().InvokeNow(TEXT("read-path"), NoFile).ToCallToolResult();
		if (TestTrue(TEXT("a missing folder is an error"), Plain.bIsError) && FMcpToolCallResult::ParseErrorCode(FirstText(Plain)).IsEmpty())
		{
			TestEqual(TEXT("an error text without a code is TOOL_FAILED"), Plain.GetErrorCode(), FString(TEXT("TOOL_FAILED")));
		}

		// tools/call with arguments that are not an object: an isError result, not a JSON-RPC error (SEP-1303)
		FN2CMcpProtocol Core;
		auto Send = [&Core](const FString& Body, const FString& SessionId)
		{
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
			Core.Handle(N2CTest::MakeMcpInbound(Body, TEXT("application/json, text/event-stream"), SessionId), Responder);
			return Responder;
		};
		const FString SessionId = Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",")
			TEXT("\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-structured-spec\",\"version\":\"1\"}}}"), FString())->LastReply().SessionId;
		for (const TCHAR* Arguments : { TEXT("\"oops\""), TEXT("[1,2]"), TEXT("7") })
		{
			for (const TCHAR* Tool : { TEXT("get-available-translation-targets"), TEXT("translate-focused-blueprint") })
			{
				const FString What = FString::Printf(TEXT("%s with arguments %s"), Tool, Arguments);
				const TSharedPtr<FJsonObject> Message = N2CMcpOutputSchema::ParseObject(Send(FString::Printf(
					TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}"), Tool, Arguments), SessionId)->LastReply().Body);
				if (!TestTrue(What + TEXT(": a JSON-RPC message"), Message.IsValid()))
				{
					continue;
				}
				TestFalse(What + TEXT(": no JSON-RPC error"), Message->HasField(TEXT("error")));
				const TSharedPtr<FJsonObject>* Result = nullptr;
				if (TestTrue(What + TEXT(": a result"), Message->TryGetObjectField(TEXT("result"), Result)))
				{
					TestTrue(What + TEXT(": isError"), (*Result)->GetBoolField(TEXT("isError")));
					TestEqual(What + TEXT(": INVALID_ARGUMENT"), (*Result)->GetObjectField(TEXT("_meta"))->GetObjectField(FMcpToolCallResult::ErrorMetaKey)
						->GetStringField(TEXT("code")), FString(TEXT("INVALID_ARGUMENT")));
				}
			}
		}
	});

	It(TEXT("has a schema checker that catches a missing field, a wrong type and a value outside an enum"), [this]()
	{
		using namespace N2CMcpOutputSchema;
		const TSharedRef<FJsonObject> Schema = Object({
			{ TEXT("name"), String() },
			{ TEXT("count"), Integer() },
			{ TEXT("kind"), Enum({ TEXT("a"), TEXT("b") }) },
			{ TEXT("items"), Array(Object({ { TEXT("id"), String() } }, { TEXT("id") })) },
		}, { TEXT("name"), TEXT("count") });
		auto Errors = [&Schema](const FString& Json)
		{
			TArray<FString> Out;
			Validate(Schema, MakeShared<FJsonValueObject>(ParseObject(Json)), Out);
			return Out.Num();
		};
		TestEqual(TEXT("a matching object"), Errors(TEXT("{\"name\":\"x\",\"count\":2,\"kind\":\"a\",\"items\":[{\"id\":\"i\"}],\"extra\":true}")), 0);
		TestEqual(TEXT("a missing required field"), Errors(TEXT("{\"name\":\"x\"}")), 1);
		TestEqual(TEXT("a wrong type"), Errors(TEXT("{\"name\":1,\"count\":2}")), 1);
		TestEqual(TEXT("a fraction for an integer"), Errors(TEXT("{\"name\":\"x\",\"count\":2.5}")), 1);
		TestEqual(TEXT("a value outside the enum"), Errors(TEXT("{\"name\":\"x\",\"count\":2,\"kind\":\"c\"}")), 1);
		TestEqual(TEXT("an array item without its required field"), Errors(TEXT("{\"name\":\"x\",\"count\":2,\"items\":[{}]}")), 1);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
