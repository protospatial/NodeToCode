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
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "HAL/PlatformProcess.h"
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
			// Every call keeps its own promise until Finish: a promise destroyed unset asserts
			const TSharedRef<TPromise<FMcpToolCallResult>> Promise = MakeShared<TPromise<FMcpToolCallResult>>();
			Promises.Add(Promise);
			LastContext = Context;
			++Invocations;
			TFuture<FMcpToolCallResult> Future = Promise->GetFuture();
			Context->ReportProgress(0.1f, TEXT("started"));
			return Future;
		}

		void Progress(float Value) { if (LastContext.IsValid()) { LastContext->ReportProgress(Value, TEXT("working")); } }

		/** Finishes every call still running, oldest first */
		void Finish(const FString& Text)
		{
			TArray<TSharedPtr<TPromise<FMcpToolCallResult>>> Pending = MoveTemp(Promises);
			Promises.Reset();
			for (const TSharedPtr<TPromise<FMcpToolCallResult>>& Promise : Pending)
			{
				Promise->SetValue(FMcpToolCallResult::CreateTextResult(Text));
			}
		}

		TArray<TSharedPtr<TPromise<FMcpToolCallResult>>> Promises;
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

	FString Initialize(int32 Id, const FString& Version = TEXT("2025-03-26"), const FString& ClientName = TEXT("n2c-core-spec"))
	{
		return Request(Id, TEXT("initialize"), FString::Printf(
			TEXT("{\"protocolVersion\":\"%s\",\"capabilities\":{\"roots\":{}},\"clientInfo\":{\"name\":\"%s\",\"version\":\"1.0\"}}"),
			*Version, *ClientName));
	}

	/** The first value of a reply header (case-insensitive), or empty */
	FString ReplyHeader(const FN2CMcpReply& Reply, const FString& Name)
	{
		for (const TPair<FString, FString>& Header : Reply.Headers)
		{
			if (Header.Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return Header.Value;
			}
		}
		return FString();
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
 * what the core adds over them: answering through a responder, per-client sessions and protocol versions (card 0109:
 * 400/404/405, DELETE, MCP-Protocol-Version, batches per version), the long-running branch (held reply and stream),
 * and server notifications flowing from the core to its sink. Review ids: next-1, mcp-transport-16, mcp-transport-18,
 * mcp-transport-19, imp-mcp-transport-1.
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

	/** Initializes a session on Core at Version and returns its Mcp-Session-Id (empty, with a test error, on failure) */
	FString Open(FN2CMcpProtocol& Core, const FString& Version = TEXT("2025-11-25"), const FString& ClientName = TEXT("n2c-core-spec"))
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, N2CMcpProtocolCoreSpec::Initialize(1, Version, ClientName));
		TestEqual(TEXT("initialize: status"), Responder->LastReply().Status, 200);
		const FString SessionId = Responder->LastReply().SessionId;
		TestFalse(TEXT("initialize: an Mcp-Session-Id"), SessionId.IsEmpty());
		return SessionId;
	}

	/** Sends Body in SessionId's session */
	TSharedRef<N2CTest::FN2CMcpMemoryResponder> SendIn(FN2CMcpProtocol& Core, const FString& SessionId, const FString& Body,
		bool bCanStream = false, const FString& Accept = TEXT("application/json, text/event-stream"))
	{
		return Send(Core, Body, bCanStream, Accept, SessionId);
	}

	/** The JSON-RPC error code of the last reply a responder got (0 when it has none) */
	int32 ErrorCodeOf(const N2CTest::FN2CMcpMemoryResponder& Responder)
	{
		const TSharedPtr<FJsonObject> Message = N2CMcpProtocolCoreSpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		return Message.IsValid() && Message->TryGetObjectField(TEXT("error"), Error) ? static_cast<int32>((*Error)->GetNumberField(TEXT("code"))) : 0;
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
			TestNotNull(TEXT("the core knows the session"), Core.FindSession(SessionId));
			if (const TSharedPtr<FJsonObject> Result = ResultOf(*Responder))
			{
				TestEqual(TEXT("protocolVersion"), Result->GetStringField(TEXT("protocolVersion")), FString(TEXT("2025-03-26")));
			}
			TestEqual(TEXT("negotiated version recorded for the session"), Core.GetSessionProtocolVersion(SessionId), FString(TEXT("2025-03-26")));
		});

		It(TEXT("answers tools/list with the registered tools and echoes the session"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId, Request(2, TEXT("tools/list")));
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
			const FString SessionId = Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId,
				Request(3, TEXT("tools/call"), FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{\"text\":\"hi\"}}"), EchoToolName)));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			TestEqual(TEXT("nothing streamed"), Responder->Streamed.Num(), 0);
			TestEqual(TEXT("tool text"), CallToolText(ParseObject(Responder->LastReply().Body), 3), FString(TEXT("echo: hi")));
		});

		It(TEXT("answers a notification with 202 and no body"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			TestFalse(TEXT("not initialized before the notification"), Core.FindSession(SessionId) && Core.FindSession(SessionId)->bInitialized);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId, TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"));
			TestEqual(TEXT("one reply"), Responder->ReplyCount, 1);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 202);
			TestTrue(TEXT("empty body"), Responder->LastReply().Body.IsEmpty());
			TestTrue(TEXT("session marked initialized"), Core.FindSession(SessionId) && Core.FindSession(SessionId)->bInitialized);
		});

		// Transports, sending messages to the server: "If the input is a JSON-RPC response or notification ... the
		// server MUST return HTTP status code 202 Accepted with no body" (finding mcp-transport-19)
		It(TEXT("answers a client result or error response with 202 and no body"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			for (const TCHAR* Body : { TEXT("{\"jsonrpc\":\"2.0\",\"id\":70,\"result\":{}}"),
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":71,\"error\":{\"code\":-1,\"message\":\"no\"}}") })
			{
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId, Body);
				TestEqual(FString::Printf(TEXT("%s: status"), Body), Responder->LastReply().Status, 202);
				TestTrue(FString::Printf(TEXT("%s: empty body"), Body), Responder->LastReply().Body.IsEmpty());
			}
		});

		It(TEXT("refuses a tools/call with a null id without starting the tool"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId,
				FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{}}}"), LongToolName));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 400);
			TestEqual(TEXT("-32600"), ErrorCodeOf(*Responder), static_cast<int32>(JsonRpcErrorCodes::InvalidRequest));
			TestEqual(TEXT("tool not invoked"), LongTool->Invocations, 0);
		});

		It(TEXT("keeps sessions per core instance, not in a global"), [this]()
		{
			FN2CMcpProtocol First;
			FN2CMcpProtocol Second;
			const FString FirstSession = Open(First);
			TestEqual(TEXT("first core has one session"), First.GetSessionCount(), 1);
			TestEqual(TEXT("second core has none"), Second.GetSessionCount(), 0);
			TestEqual(TEXT("second core does not know the first core's session"),
				SendIn(Second, FirstSession, Request(2, TEXT("ping")))->LastReply().Status, 404);
		});
	});

	Describe(TEXT("sessions"), [this]()
	{
		// Lifecycle, version negotiation: "If the server supports the requested protocol version, it MUST respond with
		// the same version. Otherwise, the server MUST respond with another protocol version it supports. This SHOULD
		// be the latest version supported by the server." (finding next-1)
		It(TEXT("negotiates 2025-11-25, 2025-06-18, 2025-03-26 and 2024-11-05, and answers others with 2025-11-25"), [this]()
		{
			FN2CMcpProtocol Core;
			const TArray<TPair<FString, FString>> Cases = {
				{ TEXT("2025-11-25"), TEXT("2025-11-25") }, { TEXT("2025-06-18"), TEXT("2025-06-18") },
				{ TEXT("2025-03-26"), TEXT("2025-03-26") }, { TEXT("2024-11-05"), TEXT("2024-11-05") },
				{ TEXT("2026-07-28"), TEXT("2025-11-25") }, { TEXT("1999-01-01"), TEXT("2025-11-25") } };
			for (const TPair<FString, FString>& Case : Cases)
			{
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Initialize(1, Case.Key));
				const TSharedPtr<FJsonObject> Result = ResultOf(*Responder);
				TestEqual(FString::Printf(TEXT("%s: answered"), *Case.Key), Result.IsValid() ? Result->GetStringField(TEXT("protocolVersion")) : FString(), Case.Value);
				TestEqual(FString::Printf(TEXT("%s: recorded for its session"), *Case.Key), Core.GetSessionProtocolVersion(Responder->LastReply().SessionId), Case.Value);
			}
			TestEqual(TEXT("newest first"), FN2CMcpProtocol::GetSupportedProtocolVersions()[0], FString(TEXT("2025-11-25")));
		});

		It(TEXT("give each initialize its own random session with its own version and client info"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString A = Open(Core, TEXT("2025-11-25"), TEXT("client-a"));
			const FString B = Open(Core, TEXT("2025-06-18"), TEXT("client-b"));
			TestNotEqual(TEXT("different ids"), A, B);
			TestEqual(TEXT("two sessions"), Core.GetSessionCount(), 2);
			const FN2CMcpSession* SessionA = Core.FindSession(A);
			const FN2CMcpSession* SessionB = Core.FindSession(B);
			if (TestNotNull(TEXT("session A"), SessionA) && TestNotNull(TEXT("session B"), SessionB))
			{
				TestEqual(TEXT("A version"), SessionA->ProtocolVersion, FString(TEXT("2025-11-25")));
				TestEqual(TEXT("B version"), SessionB->ProtocolVersion, FString(TEXT("2025-06-18")));
				TestTrue(TEXT("A client info"), SessionA->ClientInfo.IsValid() && SessionA->ClientInfo->GetStringField(TEXT("name")) == TEXT("client-a"));
				TestTrue(TEXT("B client info"), SessionB->ClientInfo.IsValid() && SessionB->ClientInfo->GetStringField(TEXT("name")) == TEXT("client-b"));
				TestTrue(TEXT("A capabilities"), SessionA->ClientCapabilities.IsValid() && SessionA->ClientCapabilities->HasField(TEXT("roots")));
			}
			for (const FString& Id : { A, B })
			{
				bool bVisibleAscii = Id.Len() > 0;
				for (const TCHAR Ch : Id)
				{
					bVisibleAscii &= Ch >= 0x21 && Ch <= 0x7E;
				}
				TestTrue(TEXT("session id is visible ASCII"), bVisibleAscii);
			}
			// Each client keeps answering in its own session
			TestEqual(TEXT("A answered in A"), SendIn(Core, A, Request(2, TEXT("ping")))->LastReply().SessionId, A);
			TestEqual(TEXT("B answered in B"), SendIn(Core, B, Request(2, TEXT("ping")))->LastReply().SessionId, B);
		});

		It(TEXT("create no session for an initialize that fails"), [this]()
		{
			FN2CMcpProtocol Core;
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Request(1, TEXT("initialize"), TEXT("{\"capabilities\":{}}")));
			TestEqual(TEXT("-32602"), ErrorCodeOf(*Responder), static_cast<int32>(JsonRpcErrorCodes::InvalidParams));
			TestTrue(TEXT("no Mcp-Session-Id"), Responder->LastReply().SessionId.IsEmpty());
			TestEqual(TEXT("no session"), Core.GetSessionCount(), 0);
		});

		// Transports, session management: "Servers that require a session ID SHOULD respond to requests without an
		// MCP-Session-Id header (other than initialization) with HTTP 400 Bad Request."
		It(TEXT("refuse a request, notification or response without Mcp-Session-Id with 400"), [this]()
		{
			FN2CMcpProtocol Core;
			Open(Core);
			for (const FString& Body : { Request(2, TEXT("tools/list")), FString(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}")),
				FString(TEXT("{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":{}}")), FString(TEXT("not json")),
				FString::Printf(TEXT("[%s]"), *Request(3, TEXT("ping"))) })
			{
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Body);
				TestEqual(FString::Printf(TEXT("%s: status"), *Body), Responder->LastReply().Status, 400);
				TestEqual(FString::Printf(TEXT("%s: -32600"), *Body), ErrorCodeOf(*Responder), static_cast<int32>(JsonRpcErrorCodes::InvalidRequest));
			}
			TestEqual(TEXT("long-running call not started"), Send(Core, LongCall(4, FString()))->LastReply().Status, 400);
			TestEqual(TEXT("tool not invoked"), LongTool->Invocations, 0);
		});

		// Transports, session management: "The server MAY terminate the session at any time, after which it MUST
		// respond to requests containing that session ID with HTTP 404 Not Found."
		It(TEXT("refuse an unknown session with 404"), [this]()
		{
			FN2CMcpProtocol Core;
			Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, TEXT("not-a-session"), Request(2, TEXT("ping")));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 404);
			TestTrue(TEXT("a JSON-RPC error body"), ErrorCodeOf(*Responder) != 0);
		});

		It(TEXT("start a new session for an initialize that names an old one"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString Old = Open(Core);
			const FString New = Send(Core, Initialize(1, TEXT("2025-11-25")), false, TEXT("application/json, text/event-stream"), Old)->LastReply().SessionId;
			TestFalse(TEXT("a session id"), New.IsEmpty());
			TestNotEqual(TEXT("a new one"), New, Old);
		});

		// Transports, protocol version header: "If the server receives a request with an invalid or unsupported
		// MCP-Protocol-Version, it MUST respond with 400 Bad Request." A missing header is accepted: "the server SHOULD
		// assume protocol version 2025-03-26" when it has no other way to tell, and here the session's version applies.
		It(TEXT("refuse an unsupported MCP-Protocol-Version with 400 and accept a supported or missing one"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			auto Ping = [this, &Core, &SessionId](const TCHAR* Version)
			{
				FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(Request(2, TEXT("ping")), TEXT("application/json, text/event-stream"), SessionId);
				if (Version)
				{
					Inbound.Headers.Add(TEXT("MCP-Protocol-Version"), { Version });
				}
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
				Core.Handle(Inbound, Responder);
				return Responder->LastReply().Status;
			};
			TestEqual(TEXT("2025-11-25"), Ping(TEXT("2025-11-25")), 200);
			TestEqual(TEXT("2025-03-26"), Ping(TEXT("2025-03-26")), 200);
			TestEqual(TEXT("missing"), Ping(nullptr), 200);
			TestEqual(TEXT("2026-07-28"), Ping(TEXT("2026-07-28")), 400);
			TestEqual(TEXT("garbage"), Ping(TEXT("latest")), 400);
		});

		// Claude Code first probes for the stateless 2026-07-28 protocol: server/discover with MCP-Protocol-Version
		// 2026-07-28 and no session. The unsupported version is a 400, and the client falls back to initialize.
		It(TEXT("refuse a 2026-07-28 server/discover probe without a session with 400 and create no session"), [this]()
		{
			FN2CMcpProtocol Core;
			FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"server-discover-probe-1\",\"method\":\"server/discover\",\"params\":{}}"));
			Inbound.Headers.Add(TEXT("MCP-Protocol-Version"), { TEXT("2026-07-28") });
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
			Core.Handle(Inbound, Responder);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 400);
			TestTrue(TEXT("names the supported versions"), Responder->LastReply().Body.Contains(TEXT("2025-11-25")));
			TestEqual(TEXT("no session"), Core.GetSessionCount(), 0);
		});

		It(TEXT("drop the least recently used session when the table is full"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString First = Open(Core);
			const FString Second = Open(Core);
			FPlatformProcess::Sleep(0.002f);
			SendIn(Core, First, Request(2, TEXT("ping")));  // First is now newer than Second
			for (int32 Index = 2; Index < FN2CMcpProtocol::MaxSessions + 1; ++Index)
			{
				Open(Core);
			}
			TestEqual(TEXT("bounded"), Core.GetSessionCount(), FN2CMcpProtocol::MaxSessions);
			TestNotNull(TEXT("recently used session kept"), Core.FindSession(First));
			TestNull(TEXT("least recently used session dropped"), Core.FindSession(Second));
		});
	});

	Describe(TEXT("verbs"), [this]()
	{
		// Transports, listening for messages from the server: the server "MUST either return Content-Type:
		// text/event-stream in response to this HTTP GET, or else return HTTP 405 Method Not Allowed" (finding
		// mcp-transport-18)
		It(TEXT("answer GET and other verbs with 405 and Allow: POST, DELETE"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			for (const TCHAR* Verb : { TEXT("GET"), TEXT("PUT"), TEXT("PATCH") })
			{
				FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(FString(), TEXT("text/event-stream"), SessionId);
				Inbound.Verb = Verb;
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
				Core.Handle(Inbound, Responder);
				TestEqual(FString::Printf(TEXT("%s: status"), Verb), Responder->LastReply().Status, 405);
				TestEqual(FString::Printf(TEXT("%s: Allow"), Verb), ReplyHeader(Responder->LastReply(), TEXT("Allow")), FString(TEXT("POST, DELETE")));
			}
			TestNotNull(TEXT("session still live"), Core.FindSession(SessionId));
		});

		// Transports, session management: clients "SHOULD send an HTTP DELETE to the MCP endpoint with the
		// MCP-Session-Id header, to explicitly terminate the session"
		It(TEXT("end a session on DELETE: 204, then 404 for it, and leave other sessions alone"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString Ended = Open(Core);
			const FString Other = Open(Core);
			auto Delete = [&Core](const FString& SessionId)
			{
				FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(FString(), TEXT("application/json, text/event-stream"), SessionId);
				Inbound.Verb = TEXT("DELETE");
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
				Core.Handle(Inbound, Responder);
				return Responder->LastReply();
			};
			const FN2CMcpReply First = Delete(Ended);
			TestEqual(TEXT("DELETE: 204"), First.Status, 204);
			TestTrue(TEXT("DELETE: no body"), First.Body.IsEmpty());
			TestNull(TEXT("session gone"), Core.FindSession(Ended));
			TestEqual(TEXT("request on the ended session: 404"), SendIn(Core, Ended, Request(2, TEXT("tools/list")))->LastReply().Status, 404);
			TestEqual(TEXT("DELETE again: 404"), Delete(Ended).Status, 404);
			TestEqual(TEXT("DELETE without a session: 400"), Delete(FString()).Status, 400);
			TestEqual(TEXT("other session still answers"), SendIn(Core, Other, Request(3, TEXT("ping")))->LastReply().Status, 200);
		});

		It(TEXT("cancel the ended session's running calls on DELETE, not other sessions' calls"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString Kept = Open(Core);
			const FString Ended = Open(Core);
			SendIn(Core, Kept, LongCall(11, FString()));
			const FN2CToolCallContextPtr KeptCall = LongTool->LastContext;
			SendIn(Core, Ended, LongCall(12, FString()));
			const FN2CToolCallContextPtr EndedCall = LongTool->LastContext;
			if (!TestTrue(TEXT("two calls running"), KeptCall.IsValid() && EndedCall.IsValid() && KeptCall != EndedCall))
			{
				return;
			}
			const TSharedPtr<FJsonValue> EndedRequestId = MakeShared<FJsonValueNumber>(12);
			TestTrue(TEXT("the call is filed under its session"),
				FN2CToolAsyncTaskManager::Get().FindTaskByRequestId(Ended, EndedRequestId).IsValid());

			FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(FString(), TEXT("application/json"), Ended);
			Inbound.Verb = TEXT("DELETE");
			Core.Handle(Inbound, MakeShared<N2CTest::FN2CMcpMemoryResponder>());
			TestTrue(TEXT("ended session's call cancelled"), EndedCall->IsCancelled());
			TestFalse(TEXT("other session's call still running"), KeptCall->IsCancelled());
		});
	});

	// JSON-RPC batching was added in 2025-03-26 and removed in 2025-06-18 (changelog, PR #416); 2025-11-25, basic
	// transports: "The body of the POST request MUST be a single JSON-RPC request, notification, or response."
	Describe(TEXT("batches"), [this]()
	{
		It(TEXT("answer a batch with one array of responses in order on a 2025-03-26 session"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core, TEXT("2025-03-26"));
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId,
				FString::Printf(TEXT("[%s,%s,{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}]"), *Request(4, TEXT("ping")), *Request(5, TEXT("tools/list"))));
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			const TSharedPtr<FJsonValue> Body = Parse(Responder->LastReply().Body);
			if (TestTrue(TEXT("body is an array"), Body.IsValid() && Body->Type == EJson::Array)
				&& TestEqual(TEXT("two responses (a client response item gets none)"), Body->AsArray().Num(), 2))
			{
				TestEqual(TEXT("first id"), Body->AsArray()[0]->AsObject()->GetNumberField(TEXT("id")), 4.0);
				TestEqual(TEXT("second id"), Body->AsArray()[1]->AsObject()->GetNumberField(TEXT("id")), 5.0);
			}
		});

		It(TEXT("refuse a batch with 400 and -32600 on 2025-06-18, 2025-11-25 and 2024-11-05 sessions"), [this]()
		{
			FN2CMcpProtocol Core;
			for (const TCHAR* Version : { TEXT("2025-06-18"), TEXT("2025-11-25"), TEXT("2024-11-05") })
			{
				const FString SessionId = Open(Core, Version);
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId,
					FString::Printf(TEXT("[%s,%s]"), *Request(4, TEXT("ping")), *Request(5, TEXT("ping"))));
				TestEqual(FString::Printf(TEXT("%s: status"), Version), Responder->LastReply().Status, 400);
				TestEqual(FString::Printf(TEXT("%s: -32600"), Version), ErrorCodeOf(*Responder), static_cast<int32>(JsonRpcErrorCodes::InvalidRequest));
				TestNotNull(FString::Printf(TEXT("%s: session still live"), Version), Core.FindSession(SessionId));
			}
		});
	});

	Describe(TEXT("long-running tools/call"), [this]()
	{
		It(TEXT("holds the request and replies once with the CallToolResult when the call finishes"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId, LongCall(7, TEXT(",\"_meta\":{\"progressToken\":7}")),
				/*bCanStream=*/false);
			TestEqual(TEXT("tool invoked"), LongTool->Invocations, 1);
			TestFalse(TEXT("nothing answered while the call runs"), Responder->IsAnswered());
			LongTool->Progress(0.5f);
			TestEqual(TEXT("progress is not streamed to a responder that cannot stream"), Responder->Streamed.Num(), 0);
			LongTool->Finish(TEXT("long done"));
			TestEqual(TEXT("one reply"), Responder->ReplyCount, 1);
			TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
			TestEqual(TEXT("session echoed"), Responder->LastReply().SessionId, SessionId);
			TestEqual(TEXT("result text"), CallToolText(ParseObject(Responder->LastReply().Body), 7), FString(TEXT("long done")));
		});

		It(TEXT("streams progress and then the response, and closes, when the responder can stream"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId, LongCall(8, TEXT(",\"_meta\":{\"progressToken\":\"tok-8\"}")),
				/*bCanStream=*/true);
			TestEqual(TEXT("the first progress is streamed at once"), Responder->Streamed.Num(), 1);
			TestEqual(TEXT("stream carries the session"), Responder->StreamSessionId, SessionId);
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
			const FString SessionId = Open(Core);
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = SendIn(Core, SessionId, LongCall(9, TEXT(",\"_meta\":{\"progressToken\":9}")),
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
