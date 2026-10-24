// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpProtocolCoreSpec
{
	const TCHAR* EchoToolName = TEXT("n2c-test-core-echo");
	const TCHAR* LongToolName = TEXT("n2c-test-core-long");

	TSharedPtr<FJsonObject> ObjectSchema()
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		return Schema;
	}

	/** A quick tool: answers "echo: <text>" */
	class FEchoTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(EchoToolName, TEXT("Spec tool registered by NodeToCode.MCP.Core"));
			Definition.InputSchema = ObjectSchema();
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			FString Text;
			Arguments->TryGetStringField(TEXT("text"), Text);
			return FMcpToolCallResult::CreateTextResult(FString::Printf(TEXT("echo: %s"), *Text));
		}
	};

	/** A long-running tool whose result the spec sets: reports progress once in InvokeAsync */
	class FLongTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(LongToolName, TEXT("Long-running spec tool registered by NodeToCode.MCP.Core"));
			Definition.InputSchema = ObjectSchema();
			Definition.bIsLongRunning = true;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>&) override
		{
			Promise = MakeShared<TPromise<FMcpToolCallResult>>();
			LastContext = Context;
			++Invocations;
			TFuture<FMcpToolCallResult> Future = Promise->GetFuture();
			Context->ReportProgress(0.1f, TEXT("started"));
			return Future;
		}

		void Progress(float Value) { if (LastContext.IsValid()) { LastContext->ReportProgress(Value, TEXT("working")); } }

		void Finish(const FString& Text)
		{
			if (Promise.IsValid())
			{
				Promise->SetValue(FMcpToolCallResult::CreateTextResult(Text));
				Promise.Reset();
			}
		}

		TSharedPtr<TPromise<FMcpToolCallResult>> Promise;
		FN2CToolCallContextPtr LastContext;
		int32 Invocations = 0;
	};

	TSharedPtr<FJsonValue> Parse(const FString& Text)
	{
		TSharedPtr<FJsonValue> Value;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value);
		return Value;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		const TSharedPtr<FJsonValue> Value = Parse(Text);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	FString Request(int32 Id, const FString& Method, const FString& ParamsJson = FString())
	{
		return ParamsJson.IsEmpty()
			? FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\"}"), Id, *Method)
			: FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}"), Id, *Method, *ParamsJson);
	}

	FString Initialize(int32 Id)
	{
		return Request(Id, TEXT("initialize"),
			TEXT("{\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-core-spec\",\"version\":\"1.0\"}}"));
	}

	FString LongCall(int32 Id, const FString& MetaJson)
	{
		return FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{}%s}}"),
			Id, LongToolName, *MetaJson);
	}
}

/**
 * The transport-neutral MCP protocol core (FN2CMcpProtocol, card 0108) driven through Handle with an in-memory
 * responder: no listener, no socket. NodeToCode.MCP.Protocol covers the JSON-RPC answers in detail; this spec covers
 * what the core adds over them: answering through a responder, sessions, the long-running branch (held reply and
 * stream), and server notifications flowing from the core to its sink. Review ids: next-1, imp-mcp-transport-1.
 */
BEGIN_DEFINE_SPEC(FN2CMcpProtocolCoreSpec, "NodeToCode.MCP.Core", N2C_TEST_FLAGS)

	TSharedPtr<N2CMcpProtocolCoreSpec::FEchoTool> EchoTool;
	TSharedPtr<N2CMcpProtocolCoreSpec::FLongTool> LongTool;
	bool bRegisteredEcho = false;
	bool bRegisteredLong = false;

	/** Handles Body on Core and returns the responder that recorded the answer */
	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(FN2CMcpProtocol& Core, const FString& Body, bool bCanStream = false,
		const FString& Accept = TEXT("application/json, text/event-stream"), const FString& SessionId = FString())
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(bCanStream);
		Core.Handle(N2CTest::MakeMcpInbound(Body, Accept, SessionId), Responder);
		return Responder;
	}

	/** The result object of the one reply a responder got */
	TSharedPtr<FJsonObject> ResultOf(const N2CTest::FN2CMcpMemoryResponder& Responder)
	{
		if (!TestEqual(TEXT("one reply"), Responder.ReplyCount, 1))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Message = N2CMcpProtocolCoreSpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (!TestTrue(FString::Printf(TEXT("reply is a success response: %s"), *Responder.LastReply().Body.Left(200)),
			Message.IsValid() && Message->TryGetObjectField(TEXT("result"), Result)))
		{
			return nullptr;
		}
		return *Result;
	}

	/** The text of the first content item of a CallToolResult message */
	FString CallToolText(const TSharedPtr<FJsonObject>& Message, int32 ExpectedId)
	{
		const TSharedPtr<FJsonObject>* Result = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		double Id = 0.0;
		if (!TestTrue(TEXT("message is a response"), Message.IsValid() && Message->TryGetObjectField(TEXT("result"), Result))
			|| !TestTrue(TEXT("response id"), Message->TryGetNumberField(TEXT("id"), Id) && static_cast<int32>(Id) == ExpectedId)
			|| !TestTrue(TEXT("result has content"), (*Result)->TryGetArrayField(TEXT("content"), Content) && Content->Num() > 0))
		{
			return FString();
		}
		return (*Content)[0]->AsObject()->GetStringField(TEXT("text"));
	}

END_DEFINE_SPEC(FN2CMcpProtocolCoreSpec)

void FN2CMcpProtocolCoreSpec::Define()
{
	using namespace N2CMcpProtocolCoreSpec;

	BeforeEach([this]()
	{
		EchoTool = MakeShared<FEchoTool>();
		LongTool = MakeShared<FLongTool>();
		bRegisteredEcho = FN2CMcpToolManager::Get().RegisterTool(EchoTool);
		bRegisteredLong = FN2CMcpToolManager::Get().RegisterTool(LongTool);
		TestTrue(TEXT("spec tools registered"), bRegisteredEcho && bRegisteredLong);
	});

	AfterEach([this]()
	{
		if (LongTool.IsValid())
		{
			LongTool->Finish(TEXT("spec cleanup"));
		}
		if (bRegisteredEcho)
		{
			FN2CMcpToolManager::Get().UnregisterTool(EchoToolName);
		}
		if (bRegisteredLong)
		{
			FN2CMcpToolManager::Get().UnregisterTool(LongToolName);
		}
		bRegisteredEcho = bRegisteredLong = false;
		EchoTool.Reset();
		LongTool.Reset();
	});

	Describe(TEXT("Handle"), [this]()
	{
		It(TEXT("answers initialize with 200, a protocolVersion and a new Mcp-Session-Id it records the version for"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Initialize(1));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			const FString SessionId = Responder->LastReply().SessionId;
			TestFalse(TEXT("a session id"), SessionId.IsEmpty());
			TestEqual(TEXT("the core's current session"), Core.GetCurrentSessionId(), SessionId);
			if (const TSharedPtr<FJsonObject> Result = ResultOf(*Responder))
			{
				TestEqual(TEXT("protocolVersion"), Result->GetStringField(TEXT("protocolVersion")), FString(TEXT("2025-03-26")));
			}
			TestEqual(TEXT("negotiated version recorded for the session"), Core.GetSessionProtocolVersion(SessionId), FString(TEXT("2025-03-26")));
		});

		It(TEXT("answers tools/list with the registered tools and echoes the session"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Send(Core, Initialize(1))->LastReply().SessionId;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Request(2, TEXT("tools/list")), false,
				TEXT("application/json, text/event-stream"), SessionId);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			TestEqual(TEXT("session echoed"), Responder->LastReply().SessionId, SessionId);
			const TSharedPtr<FJsonObject> Result = ResultOf(*Responder);
			const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
			if (Result.IsValid() && TestTrue(TEXT("tools array"), Result->TryGetArrayField(TEXT("tools"), Tools)))
			{
				bool bEcho = false;
				bool bLong = false;
				for (const TSharedPtr<FJsonValue>& Tool : *Tools)
				{
					const FString Name = Tool->AsObject()->GetStringField(TEXT("name"));
					bEcho |= Name == EchoToolName;
					bLong |= Name == LongToolName;
				}
				TestTrue(TEXT("echo tool listed"), bEcho);
				TestTrue(TEXT("long-running tool listed"), bLong);
			}
		});

		It(TEXT("answers a tools/call of a quick tool with its CallToolResult"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core,
				Request(3, TEXT("tools/call"), FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{\"text\":\"hi\"}}"), EchoToolName)));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			TestEqual(TEXT("nothing streamed"), Responder->Streamed.Num(), 0);
			TestEqual(TEXT("tool text"), CallToolText(ParseObject(Responder->LastReply().Body), 3), FString(TEXT("echo: hi")));
		});

		It(TEXT("answers a notification with 202 and no body"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"));
			TestEqual(TEXT("one reply"), Responder->ReplyCount, 1);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 202);
			TestTrue(TEXT("empty body"), Responder->LastReply().Body.IsEmpty());
		});

		It(TEXT("answers a batch with one array of responses in order"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core,
				FString::Printf(TEXT("[%s,%s]"), *Request(4, TEXT("ping")), *Request(5, TEXT("tools/list"))));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			const TSharedPtr<FJsonValue> Body = Parse(Responder->LastReply().Body);
			if (TestTrue(TEXT("body is an array"), Body.IsValid() && Body->Type == EJson::Array)
				&& TestEqual(TEXT("two responses"), Body->AsArray().Num(), 2))
			{
				TestEqual(TEXT("first id"), Body->AsArray()[0]->AsObject()->GetNumberField(TEXT("id")), 4.0);
				TestEqual(TEXT("second id"), Body->AsArray()[1]->AsObject()->GetNumberField(TEXT("id")), 5.0);
			}
		});

		It(TEXT("refuses a verb other than POST with 405"), [this]()
		{
			FN2CMcpProtocol Core;
			FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(Request(6, TEXT("ping")));
			Inbound.Verb = TEXT("GET");
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
			Core.Handle(Inbound, Responder);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 405);
		});

		It(TEXT("refuses a tools/call with a null id without starting the tool"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core,
				FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{}}}"), LongToolName));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 400);
			const TSharedPtr<FJsonObject> Message = ParseObject(Responder->LastReply().Body);
			const TSharedPtr<FJsonObject>* Error = nullptr;
			TestTrue(TEXT("-32600"), Message.IsValid() && Message->TryGetObjectField(TEXT("error"), Error)
				&& static_cast<int32>((*Error)->GetNumberField(TEXT("code"))) == JsonRpcErrorCodes::InvalidRequest);
			TestEqual(TEXT("tool not invoked"), LongTool->Invocations, 0);
		});

		It(TEXT("keeps sessions per core instance, not in a global"), [this]()
		{
			FN2CMcpProtocol First;
			FN2CMcpProtocol Second;
			const FString FirstSession = Send(First, Initialize(1))->LastReply().SessionId;
			TestFalse(TEXT("first core has a session"), FirstSession.IsEmpty());
			TestTrue(TEXT("second core has none"), Second.GetCurrentSessionId().IsEmpty());
			TestTrue(TEXT("second core answers ping without a session"), Send(Second, Request(2, TEXT("ping")))->LastReply().SessionId.IsEmpty());
		});
	});

	Describe(TEXT("long-running tools/call"), [this]()
	{
		It(TEXT("holds the request and replies once with the CallToolResult when the call finishes"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, LongCall(7, TEXT(",\"_meta\":{\"progressToken\":7}")),
				/*bCanStream=*/false);
			TestEqual(TEXT("tool invoked"), LongTool->Invocations, 1);
			TestFalse(TEXT("nothing answered while the call runs"), Responder->IsAnswered());
			LongTool->Progress(0.5f);
			TestEqual(TEXT("progress is not streamed to a responder that cannot stream"), Responder->Streamed.Num(), 0);
			LongTool->Finish(TEXT("long done"));
			TestEqual(TEXT("one reply"), Responder->ReplyCount, 1);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			TestEqual(TEXT("result text"), CallToolText(ParseObject(Responder->LastReply().Body), 7), FString(TEXT("long done")));
		});

		It(TEXT("streams progress and then the response, and closes, when the responder can stream"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, LongCall(8, TEXT(",\"_meta\":{\"progressToken\":\"tok-8\"}")),
				/*bCanStream=*/true);
			TestEqual(TEXT("the first progress is streamed at once"), Responder->Streamed.Num(), 1);
			LongTool->Progress(0.5f);
			TestEqual(TEXT("still open"), Responder->CloseCount, 0);
			LongTool->Finish(TEXT("streamed done"));
			TestEqual(TEXT("no single reply"), Responder->ReplyCount, 0);
			TestEqual(TEXT("closed once"), Responder->CloseCount, 1);
			if (TestEqual(TEXT("two progress notifications and the response"), Responder->Streamed.Num(), 3))
			{
				for (int32 Index = 0; Index < 2; ++Index)
				{
					const TSharedPtr<FJsonObject> Notification = ParseObject(Responder->Streamed[Index]);
					const TSharedPtr<FJsonObject>* Params = nullptr;
					FString Token;
					TestTrue(TEXT("notifications/progress"), Notification.IsValid()
						&& Notification->GetStringField(TEXT("method")) == TEXT("notifications/progress")
						&& Notification->TryGetObjectField(TEXT("params"), Params)
						&& (*Params)->TryGetStringField(TEXT("progressToken"), Token) && Token == TEXT("tok-8"));
				}
				TestEqual(TEXT("response text"), CallToolText(ParseObject(Responder->Streamed[2]), 8), FString(TEXT("streamed done")));
			}
		});

		It(TEXT("replies once instead of streaming when the client does not accept text/event-stream"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, LongCall(9, TEXT(",\"_meta\":{\"progressToken\":9}")),
				/*bCanStream=*/true, TEXT("application/json"));
			LongTool->Finish(TEXT("json done"));
			TestEqual(TEXT("nothing streamed"), Responder->Streamed.Num(), 0);
			TestEqual(TEXT("one reply"), Responder->ReplyCount, 1);
			TestEqual(TEXT("result text"), CallToolText(ParseObject(Responder->LastReply().Body), 9), FString(TEXT("json done")));
		});
	});

	Describe(TEXT("notifications"), [this]()
	{
		It(TEXT("sends notifications/tools/list_changed to the core's sink when the tool list changes"), [this]()
		{
			FN2CMcpProtocol Core;
			TArray<FString> Sent;
			Core.SetBroadcastSink([&Sent](const FString& NotificationJson) { Sent.Add(NotificationJson); });
			FN2CMcpToolManager::Get().OnToolListChanged().Broadcast();
			if (TestEqual(TEXT("one notification"), Sent.Num(), 1))
			{
				const TSharedPtr<FJsonObject> Notification = ParseObject(Sent[0]);
				TestTrue(TEXT("tools/list_changed"), Notification.IsValid()
					&& Notification->GetStringField(TEXT("method")) == TEXT("notifications/tools/list_changed"));
				TestFalse(TEXT("a notification has no id"), Notification.IsValid() && Notification->HasField(TEXT("id")));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
