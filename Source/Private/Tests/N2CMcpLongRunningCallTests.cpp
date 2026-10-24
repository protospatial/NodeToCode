// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CMcpLongRunningCallTest
{
	const TCHAR* TranslateToolName = TEXT("translate-focused-blueprint");

	/** One call of the stand-in tool: the test reports progress and sets the result when it says so */
	class FStubCall
	{
	public:
		explicit FStubCall(const FN2CToolCallContextRef& InContext) : Context(InContext) {}
		~FStubCall() { Set(FMcpToolCallResult::CreateErrorResult(TEXT("stub call dropped"))); }

		void Progress(float Value, const FString& Message) { Context->ReportProgress(Value, Message); }
		void Finish() { Set(FMcpToolCallResult::CreateTextResult(TEXT("stub translation"))); }
		void Set(const FMcpToolCallResult& Result)
		{
			if (!bSet)
			{
				bSet = true;
				Promise.SetValue(Result);
			}
		}

		FN2CToolCallContextRef Context;
		TPromise<FMcpToolCallResult> Promise;
		bool bSet = false;
	};

	/** Stand-in for the translation tool: never calls an LLM */
	class FStubTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(TranslateToolName);
			Definition.bIsLongRunning = true;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>&) override
		{
			LastCall = MakeShared<FStubCall>(Context);
			TFuture<FMcpToolCallResult> Future = LastCall->Promise.GetFuture();
			LastCall->Progress(0.02f, TEXT("Collecting the focused Blueprint"));
			if (bCompleteInInvoke)
			{
				LastCall->Set(FMcpToolCallResult::CreateErrorResult(TEXT("stub failed before sending")));
			}
			return Future;
		}

		bool bCompleteInInvoke = false;
		TSharedPtr<FStubCall> LastCall;
	};

	/** Everything the server wrote through OnComplete */
	struct FCapture
	{
		TArray<TUniquePtr<FHttpServerResponse>> Responses;
	};

	/** The Mcp-Session-Id MakeRequest sends (the test initializes a session first; card 0109) */
	FString SessionId;

	FHttpServerRequest MakeRequest(const FString& Body, const FString& Accept,
		EHttpServerRequestVerbs Verb = EHttpServerRequestVerbs::VERB_POST)
	{
		FHttpServerRequest Request;
		Request.Verb = Verb;
		Request.Headers.Add(TEXT("Content-Type"), { TEXT("application/json") });
		Request.Headers.Add(TEXT("Accept"), { Accept });
		if (!SessionId.IsEmpty())
		{
			Request.Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });
		}
		const FTCHARToUTF8 Utf8(*Body);
		Request.Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Request;
	}

	FString CallBody(const FString& MetaJson)
	{
		return FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{}%s}}"),
			TranslateToolName, *MetaJson);
	}

	FString Utf8Text(const TArray<uint8>& Bytes)
	{
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converter.Length(), Converter.Get());
	}

	FString BodyText(const FHttpServerResponse& Response)
	{
		return Utf8Text(Response.Body);
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString HeaderValue(const FHttpServerResponse& Response, const FString& Name)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Response.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				return Pair.Value[0];
			}
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CMcpLongRunningCallTest, "NodeToCode.MCP.Transport.LongRunningCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CMcpLongRunningCallTest::RunTest(const FString& Parameters)
{
	using namespace N2CMcpLongRunningCallTest;

	if (!TestTrue(TEXT("test runs on the game thread"), IsInGameThread()))
	{
		return false;
	}

	// The server treats a tools/call as long-running when the registered tool says so: register the translate tool
	// for the test if the session has not (MCP off), and give the task manager stub tasks instead of real ones.
	FN2CMcpToolManager& Tools = FN2CMcpToolManager::Get();
	const bool bRegisteredHere = !Tools.IsToolRegistered(TranslateToolName);
	if (bRegisteredHere)
	{
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (Tool.IsValid() && Tool->GetDefinition().Name == TranslateToolName)
			{
				Tools.RegisterTool(Tool);
			}
		}
	}
	if (!TestTrue(TEXT("translate-focused-blueprint is registered"), Tools.IsToolRegistered(TranslateToolName)))
	{
		return false;
	}

	const TSharedRef<FStubTool> StubTool = MakeShared<FStubTool>();
	TSharedPtr<FStubCall>& LastTask = StubTool->LastCall;
	bool& bCompleteInExecute = StubTool->bCompleteInInvoke;
	FN2CToolAsyncTaskManager::Get().SetToolResolverForTesting([StubTool](const FString&) -> TSharedPtr<IN2CMcpTool>
	{
		return StubTool;
	});

	FN2CMcpHttpServerManager& Server = FN2CMcpHttpServerManager::Get();
	const TSharedRef<FCapture> Capture = MakeShared<FCapture>();
	const FHttpResultCallback OnComplete = [Capture](TUniquePtr<FHttpServerResponse>&& Response)
	{
		Capture->Responses.Add(MoveTemp(Response));
	};

	// 0. Sessions through the HTTP transport (card 0109): a POST without Mcp-Session-Id is 400, initialize returns a new
	//    session in the Mcp-Session-Id header, GET is 405 with Allow, and the calls below run in that session
	{
		SessionId.Reset();
		Capture->Responses.Reset();
		Server.HandleMcpRequest(MakeRequest(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"), TEXT("application/json")), OnComplete);
		if (TestEqual(TEXT("no session: one response"), Capture->Responses.Num(), 1))
		{
			TestEqual(TEXT("no session: 400"), static_cast<int32>(Capture->Responses[0]->Code), 400);
		}
		Capture->Responses.Reset();
		Server.HandleMcpRequest(MakeRequest(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",")
			TEXT("\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-transport-test\",\"version\":\"1.0\"}}}"), TEXT("application/json")), OnComplete);
		if (TestEqual(TEXT("initialize: one response"), Capture->Responses.Num(), 1))
		{
			TestEqual(TEXT("initialize: 200"), static_cast<int32>(Capture->Responses[0]->Code), 200);
			SessionId = HeaderValue(*Capture->Responses[0], TEXT("Mcp-Session-Id"));
		}
		TestFalse(TEXT("initialize: Mcp-Session-Id header"), SessionId.IsEmpty());
		Capture->Responses.Reset();
		Server.HandleMcpRequest(MakeRequest(FString(), TEXT("text/event-stream"), EHttpServerRequestVerbs::VERB_GET), OnComplete);
		if (TestEqual(TEXT("GET: one response"), Capture->Responses.Num(), 1))
		{
			TestEqual(TEXT("GET: 405"), static_cast<int32>(Capture->Responses[0]->Code), 405);
			TestEqual(TEXT("GET: Allow"), HeaderValue(*Capture->Responses[0], TEXT("Allow")), FString(TEXT("POST, DELETE")));
		}
	}

	/** Checks a JSON-RPC response body: id 7 and a CallToolResult (content array) with the expected text */
	auto CheckCallToolResult = [this](const FString& What, const FString& Json, const FString& ExpectedText, bool bExpectError)
	{
		// The old {status: "accepted", ...} answer (result sent later on the removed port+1 server)
		TestFalse(What + TEXT(": no 'accepted' status"), Json.Contains(TEXT("\"accepted\"")));
		const TSharedPtr<FJsonObject> Message = ParseObject(Json);
		if (!TestTrue(What + TEXT(": response is JSON"), Message.IsValid()))
		{
			return;
		}
		double Id = 0.0;
		TestTrue(What + TEXT(": response id is the request's id 7"), Message->TryGetNumberField(TEXT("id"), Id) && Id == 7.0);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (!TestTrue(What + TEXT(": response has a result"), Message->TryGetObjectField(TEXT("result"), Result)))
		{
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (TestTrue(What + TEXT(": result has a content array (a CallToolResult)"), (*Result)->TryGetArrayField(TEXT("content"), Content))
			&& TestTrue(What + TEXT(": content is not empty"), Content->Num() > 0))
		{
			FString Text;
			(*Content)[0]->AsObject()->TryGetStringField(TEXT("text"), Text);
			TestEqual(What + TEXT(": content text"), Text, ExpectedText);
		}
		bool bIsError = false;
		(*Result)->TryGetBoolField(TEXT("isError"), bIsError);
		TestEqual(What + TEXT(": isError"), bIsError, bExpectError);
	};

	// 1. A task that fails inside Execute() answers before the handler returns, with one application/json response
	{
		bCompleteInExecute = true;
		Capture->Responses.Reset();
		TestTrue(TEXT("handled"), Server.HandleMcpRequest(MakeRequest(CallBody(TEXT(",\"_meta\":{\"progressToken\":\"t1\"}")),
			TEXT("application/json")), OnComplete));
		if (TestEqual(TEXT("failing task: one response, written before the handler returned"), Capture->Responses.Num(), 1))
		{
			const FHttpServerResponse& Response = *Capture->Responses[0];
			TestEqual(TEXT("failing task: HTTP 200"), static_cast<int32>(Response.Code), 200);
			TestTrue(TEXT("failing task: application/json"), HeaderValue(Response, TEXT("Content-Type")).StartsWith(TEXT("application/json")));
			CheckCallToolResult(TEXT("failing task"), BodyText(Response), TEXT("stub failed before sending"), true);
		}
	}

	// 2. Without text/event-stream in Accept the call is held open and answered once, when the task finishes
	{
		bCompleteInExecute = false;
		Capture->Responses.Reset();
		TestTrue(TEXT("handled"), Server.HandleMcpRequest(MakeRequest(CallBody(TEXT(",\"_meta\":{\"progressToken\":7}")),
			TEXT("application/json")), OnComplete));
		TestEqual(TEXT("held call: nothing written while the task runs"), Capture->Responses.Num(), 0);
		if (TestTrue(TEXT("held call: task started"), LastTask.IsValid()))
		{
			LastTask->Progress(0.5f, TEXT("halfway"));
			TestEqual(TEXT("held call: progress writes nothing to a JSON response"), Capture->Responses.Num(), 0);
			LastTask->Finish();
		}
		if (TestEqual(TEXT("held call: one response once the task finished"), Capture->Responses.Num(), 1))
		{
			const FHttpServerResponse& Response = *Capture->Responses[0];
			TestTrue(TEXT("held call: application/json"), HeaderValue(Response, TEXT("Content-Type")).StartsWith(TEXT("application/json")));
			CheckCallToolResult(TEXT("held call"), BodyText(Response), TEXT("stub translation"), false);
		}
		LastTask.Reset();
	}

	// 3. text/event-stream accepted but no progressToken: also one JSON response at the end
	{
		bCompleteInExecute = false;
		Capture->Responses.Reset();
		Server.HandleMcpRequest(MakeRequest(CallBody(FString()), TEXT("application/json, text/event-stream")), OnComplete);
		TestEqual(TEXT("no token: nothing written while the task runs"), Capture->Responses.Num(), 0);
		if (LastTask.IsValid())
		{
			LastTask->Finish();
		}
		if (TestEqual(TEXT("no token: one response"), Capture->Responses.Num(), 1))
		{
			TestTrue(TEXT("no token: application/json"),
				HeaderValue(*Capture->Responses[0], TEXT("Content-Type")).StartsWith(TEXT("application/json")));
			CheckCallToolResult(TEXT("no token"), BodyText(*Capture->Responses[0]), TEXT("stub translation"), false);
		}
		LastTask.Reset();
	}

	// 4. Accept lists text/event-stream and a numeric progressToken is set: on 5.8 the same POST becomes an SSE stream
	//    of progress notifications (token still a number) and then the response; on 5.6/5.7 it is held as in 2.
	{
		bCompleteInExecute = false;
		Capture->Responses.Reset();
		Server.HandleMcpRequest(MakeRequest(CallBody(TEXT(",\"_meta\":{\"progressToken\":7}")),
			TEXT("application/json, text/event-stream")), OnComplete);

		if (FN2CMcpHttpServerManager::CanStreamToolProgress())
		{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			if (TestEqual(TEXT("stream: one streamed response opened at once"), Capture->Responses.Num(), 1)
				&& TestTrue(TEXT("stream: task started"), LastTask.IsValid()))
			{
				FHttpServerResponse& Response = *Capture->Responses[0];
				TestTrue(TEXT("stream: text/event-stream"), HeaderValue(Response, TEXT("Content-Type")).StartsWith(TEXT("text/event-stream")));
				TestTrue(TEXT("stream: connection closes after the last event"), EnumHasAnyFlags(Response.Flags, EHttpServerResponseFlags::CloseAfterWrite));
				if (!TestTrue(TEXT("stream: body is a queue"), Response.StreamingBodyQueue.IsValid() && Response.StreamingBodyComplete.IsValid()))
				{
					LastTask->Finish();
					LastTask.Reset();
					FN2CToolAsyncTaskManager::Get().SetToolResolverForTesting(nullptr);
					if (bRegisteredHere)
					{
						Tools.UnregisterTool(TranslateToolName);
					}
					return false;
				}

				LastTask->Progress(0.10f, TEXT("Translation request sent to the LLM provider"));
				LastTask->Progress(0.20f, TEXT("Waiting for the LLM response (1 s elapsed)"));
				TestFalse(TEXT("stream: open while the task runs"), Response.StreamingBodyComplete->Load());
				LastTask->Finish();
				TestTrue(TEXT("stream: complete once the task finished"), Response.StreamingBodyComplete->Load());

				// Read the queued events
				FString Stream;
				TArray<uint8> Chunk;
				while (Response.StreamingBodyQueue.IsValid() && Response.StreamingBodyQueue->Dequeue(Chunk))
				{
					Stream += Utf8Text(Chunk);
				}
				TArray<FString> Events;
				Stream.ParseIntoArray(Events, TEXT("\r\n\r\n"));
				TArray<TSharedPtr<FJsonObject>> Messages;
				for (const FString& Event : Events)
				{
					FString Data;
					TArray<FString> Lines;
					Event.ParseIntoArrayLines(Lines);
					for (const FString& Line : Lines)
					{
						if (Line.StartsWith(TEXT("data: ")))
						{
							Data += Line.Mid(6);
						}
					}
					Messages.Add(ParseObject(Data));
				}

				// Execute()'s report plus the two above, then the response
				if (TestEqual(TEXT("stream: three progress events and one response"), Messages.Num(), 4))
				{
					double LastProgress = -1.0;
					for (int32 Index = 0; Index < 3; ++Index)
					{
						const TSharedPtr<FJsonObject>& Message = Messages[Index];
						FString Method;
						const TSharedPtr<FJsonObject>* Params = nullptr;
						if (TestTrue(TEXT("stream: progress event parses"), Message.IsValid())
							&& TestTrue(TEXT("stream: notifications/progress"), Message->TryGetStringField(TEXT("method"), Method) && Method == TEXT("notifications/progress"))
							&& TestTrue(TEXT("stream: progress has params"), Message->TryGetObjectField(TEXT("params"), Params)))
						{
							const TSharedPtr<FJsonValue> Token = (*Params)->TryGetField(TEXT("progressToken"));
							TestTrue(TEXT("stream: the progress token is still the number 7"), Token.IsValid() && Token->Type == EJson::Number && Token->AsNumber() == 7.0);
							double Progress = 0.0;
							TestTrue(TEXT("stream: progress increases"), (*Params)->TryGetNumberField(TEXT("progress"), Progress) && Progress > LastProgress);
							LastProgress = Progress;
							FString ProgressMessage;
							TestTrue(TEXT("stream: progress has a message"), (*Params)->TryGetStringField(TEXT("message"), ProgressMessage) && !ProgressMessage.IsEmpty());
						}
					}
					FString LastEvent;
					FJsonSerializer::Serialize(Messages[3].ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&LastEvent));
					CheckCallToolResult(TEXT("stream"), LastEvent, TEXT("stub translation"), false);
				}
			}
#endif
		}
		else
		{
			TestEqual(TEXT("5.6/5.7: nothing written while the task runs"), Capture->Responses.Num(), 0);
			if (LastTask.IsValid())
			{
				LastTask->Progress(0.5f, TEXT("halfway"));
				LastTask->Finish();
			}
			if (TestEqual(TEXT("5.6/5.7: one JSON response at the end"), Capture->Responses.Num(), 1))
			{
				CheckCallToolResult(TEXT("5.6/5.7 held"), BodyText(*Capture->Responses[0]), TEXT("stub translation"), false);
			}
		}
		LastTask.Reset();
	}

	// 5. DELETE ends the session: 204, then 404 for it
	{
		Capture->Responses.Reset();
		Server.HandleMcpRequest(MakeRequest(FString(), TEXT("application/json"), EHttpServerRequestVerbs::VERB_DELETE), OnComplete);
		Server.HandleMcpRequest(MakeRequest(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}"), TEXT("application/json")), OnComplete);
		if (TestEqual(TEXT("DELETE then ping: two responses"), Capture->Responses.Num(), 2))
		{
			TestEqual(TEXT("DELETE: 204"), static_cast<int32>(Capture->Responses[0]->Code), 204);
			TestEqual(TEXT("ended session: 404"), static_cast<int32>(Capture->Responses[1]->Code), 404);
		}
		SessionId.Reset();
	}

	FN2CToolAsyncTaskManager::Get().SetToolResolverForTesting(nullptr);
	if (bRegisteredHere)
	{
		Tools.UnregisterTool(TranslateToolName);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
