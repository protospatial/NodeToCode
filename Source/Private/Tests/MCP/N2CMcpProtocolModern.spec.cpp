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
#include "MCP/Tools/N2CToolCallContext.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpModernSpec
{
	const TCHAR* EchoToolName = TEXT("n2c-test-modern-echo");
	const TCHAR* LongToolName = TEXT("n2c-test-modern-long");
	const TCHAR* Version = TEXT("2026-07-28");

	TSharedPtr<FJsonObject> ObjectSchema()
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		return Schema;
	}

	class FEchoTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(EchoToolName, TEXT("Spec tool registered by NodeToCode.MCP.Modern"));
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

	/** A long-running tool that finishes with a cancelled result as soon as its context is cancelled */
	class FLongTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(LongToolName, TEXT("Long-running spec tool registered by NodeToCode.MCP.Modern"));
			Definition.InputSchema = ObjectSchema();
			Definition.bIsLongRunning = true;
			return Definition;
		}

		struct FPending
		{
			TPromise<FMcpToolCallResult> Promise;
			bool bSet = false;
			void Set(const FMcpToolCallResult& Result) { if (!bSet) { bSet = true; Promise.SetValue(Result); } }
		};

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>&) override
		{
			const TSharedRef<FPending> Pending = MakeShared<FPending>();
			Promises.Add(Pending);
			LastContext = Context;
			TFuture<FMcpToolCallResult> Future = Pending->Promise.GetFuture();
			const TWeakPtr<FPending> WeakPending = Pending;
			const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;
			Context->OnCancelled([WeakPending, WeakContext]()
			{
				const TSharedPtr<FPending> Pinned = WeakPending.Pin();
				const FN2CToolCallContextPtr PinnedContext = WeakContext.Pin();
				if (Pinned.IsValid() && PinnedContext.IsValid())
				{
					Pinned->Set(FMcpToolCallResult::CreateErrorResult(PinnedContext->GetCancelledMessage()));
				}
			});
			Context->ReportProgress(0.1f, TEXT("started"));
			return Future;
		}

		void Finish(const FString& Text)
		{
			TArray<TSharedPtr<FPending>> Pending = MoveTemp(Promises);
			Promises.Reset();
			for (const TSharedPtr<FPending>& Call : Pending)
			{
				Call->Set(FMcpToolCallResult::CreateTextResult(Text));
			}
		}

		TArray<TSharedPtr<FPending>> Promises;
		FN2CToolCallContextPtr LastContext;
	};

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	/** params._meta of a 2026-07-28 request */
	FString Meta(const FString& InVersion = Version, bool bCapabilities = true)
	{
		return FString::Printf(TEXT("\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"%s\",")
			TEXT("\"io.modelcontextprotocol/clientInfo\":{\"name\":\"n2c-modern-spec\",\"version\":\"1\"}%s}"), *InVersion,
			bCapabilities ? TEXT(",\"io.modelcontextprotocol/clientCapabilities\":{}") : TEXT(""));
	}

	/** A 2026-07-28 request body; ExtraParams are more params members (with a leading comma), or empty */
	FString Body(const FString& Id, const FString& Method, const FString& ExtraParams = FString(), const FString& MetaJson = Meta())
	{
		return FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%s,\"method\":\"%s\",\"params\":{%s%s}}"), *Id, *Method, *MetaJson, *ExtraParams);
	}

	/** An inbound POST with the 2026-07-28 headers for Method (Name: the Mcp-Name header, or none when empty) */
	FN2CMcpInbound Inbound(const FString& InBody, const FString& Method, const FString& Name = FString(),
		const FString& HeaderVersion = Version)
	{
		FN2CMcpInbound Out = N2CTest::MakeMcpInbound(InBody);
		if (!HeaderVersion.IsEmpty())
		{
			Out.Headers.Add(TEXT("MCP-Protocol-Version"), { HeaderVersion });
		}
		if (!Method.IsEmpty())
		{
			Out.Headers.Add(TEXT("Mcp-Method"), { Method });
		}
		if (!Name.IsEmpty())
		{
			Out.Headers.Add(TEXT("Mcp-Name"), { Name });
		}
		return Out;
	}

	FString CallParams(const FString& Tool, const FString& ArgumentsJson = TEXT("{}"))
	{
		return FString::Printf(TEXT(",\"name\":\"%s\",\"arguments\":%s"), *Tool, *ArgumentsJson);
	}
}

/**
 * Card 0210: the 2026-07-28 (stateless) revision served on the same endpoint as legacy initialize and sessions.
 * A request whose params._meta names io.modelcontextprotocol/protocolVersion is served without a session:
 * server/discover; the per-request _meta (-32602 when protocolVersion or clientCapabilities is missing);
 * MCP-Protocol-Version, Mcp-Method and Mcp-Name header validation (-32020, 400); UnsupportedProtocolVersionError
 * (-32022, 400); 404 with -32601 for unknown methods (ping is gone); resultType, serverInfo, ttlMs and cacheScope on
 * results; -32602 for an unknown resource; a long-running call that streams and is cancelled when its client closes
 * the stream; subscriptions/listen with its acknowledgement, tagged list-changed notifications and graceful closure.
 * Review ids: later-3, imp-mcp-transport-1 (step 4), rec-mcp-spec-clients-11.
 */
BEGIN_DEFINE_SPEC(FN2CMcpProtocolModernSpec, "NodeToCode.MCP.Modern", N2C_TEST_FLAGS)

	TSharedPtr<N2CMcpModernSpec::FEchoTool> EchoTool;
	TSharedPtr<N2CMcpModernSpec::FLongTool> LongTool;
	bool bRegistered = false;

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(FN2CMcpProtocol& Core, const FN2CMcpInbound& In, bool bCanStream = false)
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(bCanStream);
		Core.Handle(In, Responder);
		return Responder;
	}

	/** The result object of the one reply, after checking it is a 200 success with resultType complete and serverInfo */
	TSharedPtr<FJsonObject> ModernResult(const N2CTest::FN2CMcpMemoryResponder& Responder, const FString& What)
	{
		TestEqual(What + TEXT(": status"), Responder.LastReply().Status, 200);
		TestTrue(What + TEXT(": no Mcp-Session-Id"), Responder.LastReply().SessionId.IsEmpty());
		const TSharedPtr<FJsonObject> Message = N2CMcpModernSpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (!TestTrue(What + TEXT(": a result: ") + Responder.LastReply().Body.Left(300), Message.IsValid() && Message->TryGetObjectField(TEXT("result"), Result)))
		{
			return nullptr;
		}
		TestEqual(What + TEXT(": resultType"), (*Result)->GetStringField(TEXT("resultType")), FString(TEXT("complete")));
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		const TSharedPtr<FJsonObject>* ServerInfo = nullptr;
		TestTrue(What + TEXT(": _meta serverInfo"), (*Result)->TryGetObjectField(TEXT("_meta"), Meta)
			&& (*Meta)->TryGetObjectField(TEXT("io.modelcontextprotocol/serverInfo"), ServerInfo)
			&& (*ServerInfo)->GetStringField(TEXT("name")) == TEXT("NodeToCodeMCPServer"));
		return *Result;
	}

	/** Checks the reply is Status with JSON-RPC error Code, and returns the error object */
	TSharedPtr<FJsonObject> ExpectError(const N2CTest::FN2CMcpMemoryResponder& Responder, int32 Status, int32 Code, const FString& What)
	{
		TestEqual(What + TEXT(": status"), Responder.LastReply().Status, Status);
		const TSharedPtr<FJsonObject> Message = N2CMcpModernSpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		if (!TestTrue(What + TEXT(": an error: ") + Responder.LastReply().Body.Left(300), Message.IsValid() && Message->TryGetObjectField(TEXT("error"), Error)))
		{
			return nullptr;
		}
		TestEqual(What + TEXT(": code"), static_cast<int32>((*Error)->GetNumberField(TEXT("code"))), Code);
		return *Error;
	}

END_DEFINE_SPEC(FN2CMcpProtocolModernSpec)

void FN2CMcpProtocolModernSpec::Define()
{
	using namespace N2CMcpModernSpec;

	BeforeEach([this]()
	{
		EchoTool = MakeShared<FEchoTool>();
		LongTool = MakeShared<FLongTool>();
		bRegistered = FN2CMcpToolManager::Get().RegisterTool(EchoTool) && FN2CMcpToolManager::Get().RegisterTool(LongTool);
		TestTrue(TEXT("spec tools registered"), bRegistered);
	});

	AfterEach([this]()
	{
		LongTool->Finish(TEXT("spec cleanup"));
		FN2CMcpToolManager::Get().UnregisterTool(EchoToolName);
		FN2CMcpToolManager::Get().UnregisterTool(LongToolName);
		EchoTool.Reset();
		LongTool.Reset();
	});

	It(TEXT("serves a full exchange without a session: server/discover, tools/list, tools/call"), [this]()
	{
		FN2CMcpProtocol Core;

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Discover = Send(Core, Inbound(Body(TEXT("\"discover-1\""), TEXT("server/discover")), TEXT("server/discover")));
		if (const TSharedPtr<FJsonObject> Result = ModernResult(*Discover, TEXT("server/discover")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Versions = nullptr;
			TestTrue(TEXT("supportedVersions is [2026-07-28]"), Result->TryGetArrayField(TEXT("supportedVersions"), Versions)
				&& Versions->Num() == 1 && (*Versions)[0]->AsString() == Version);
			TestTrue(TEXT("capabilities.tools"), Result->HasField(TEXT("capabilities")) && Result->GetObjectField(TEXT("capabilities"))->HasField(TEXT("tools")));
			TestFalse(TEXT("instructions"), Result->GetStringField(TEXT("instructions")).IsEmpty());
			TestEqual(TEXT("cacheScope"), Result->GetStringField(TEXT("cacheScope")), FString(TEXT("private")));
			TestTrue(TEXT("id echoed"), N2CMcpModernSpec::ParseObject(Discover->LastReply().Body)->GetStringField(TEXT("id")) == TEXT("discover-1"));
		}

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> List = Send(Core, Inbound(Body(TEXT("2"), TEXT("tools/list")), TEXT("tools/list")));
		if (const TSharedPtr<FJsonObject> Result = ModernResult(*List, TEXT("tools/list")))
		{
			TestEqual(TEXT("ttlMs"), static_cast<int32>(Result->GetNumberField(TEXT("ttlMs"))), FN2CMcpProtocol::ListTtlMs);
			TestEqual(TEXT("cacheScope"), Result->GetStringField(TEXT("cacheScope")), FString(TEXT("private")));
			bool bEcho = false;
			for (const TSharedPtr<FJsonValue>& Tool : Result->GetArrayField(TEXT("tools")))
			{
				bEcho |= Tool->AsObject()->GetStringField(TEXT("name")) == EchoToolName;
			}
			TestTrue(TEXT("echo tool listed"), bEcho);
		}

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Call = Send(Core, Inbound(
			Body(TEXT("3"), TEXT("tools/call"), CallParams(EchoToolName, TEXT("{\"text\":\"hi\"}"))), TEXT("tools/call"), EchoToolName));
		if (const TSharedPtr<FJsonObject> Result = ModernResult(*Call, TEXT("tools/call")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
			TestTrue(TEXT("echo text"), Result->TryGetArrayField(TEXT("content"), Content) && Content->Num() > 0
				&& (*Content)[0]->AsObject()->GetStringField(TEXT("text")) == TEXT("echo: hi"));
			TestFalse(TEXT("no ttlMs on a tool result"), Result->HasField(TEXT("ttlMs")));
		}

		// A session id from an older client is ignored, not refused
		FN2CMcpInbound WithSession = Inbound(Body(TEXT("4"), TEXT("tools/list")), TEXT("tools/list"));
		WithSession.Headers.Add(TEXT("Mcp-Session-Id"), { TEXT("not-a-session") });
		ModernResult(*Send(Core, WithSession), TEXT("tools/list with a stray Mcp-Session-Id"));

		TestEqual(TEXT("no session was created"), Core.GetSessionCount(), 0);
	});

	It(TEXT("refuses a request without the required _meta fields with 400 and -32602"), [this]()
	{
		FN2CMcpProtocol Core;
		ExpectError(*Send(Core, Inbound(Body(TEXT("1"), TEXT("tools/list"), FString(), Meta(Version, false)), TEXT("tools/list"))),
			400, JsonRpcErrorCodes::InvalidParams, TEXT("no clientCapabilities"));
		// The header alone marks a modern request; no _meta at all is malformed (this is also what a stateless probe
		// without _meta gets: a legacy client is never sent here, it opens with initialize)
		ExpectError(*Send(Core, Inbound(TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"p\",\"method\":\"server/discover\",\"params\":{}}"), TEXT("server/discover"))),
			400, JsonRpcErrorCodes::InvalidParams, TEXT("no _meta"));
		TestEqual(TEXT("no session"), Core.GetSessionCount(), 0);
	});

	It(TEXT("validates MCP-Protocol-Version, Mcp-Method and Mcp-Name against the body with 400 and -32020"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Call = Body(TEXT("1"), TEXT("tools/call"), CallParams(EchoToolName));
		ExpectError(*Send(Core, Inbound(Call, TEXT("tools/call"), EchoToolName, FString())), 400, -32020, TEXT("no MCP-Protocol-Version"));
		ExpectError(*Send(Core, Inbound(Call, TEXT("tools/call"), EchoToolName, TEXT("2025-11-25"))), 400, -32020, TEXT("MCP-Protocol-Version differs"));
		ExpectError(*Send(Core, Inbound(Call, FString(), EchoToolName)), 400, -32020, TEXT("no Mcp-Method"));
		ExpectError(*Send(Core, Inbound(Call, TEXT("tools/list"), EchoToolName)), 400, -32020, TEXT("Mcp-Method differs"));
		ExpectError(*Send(Core, Inbound(Call, TEXT("tools/call"))), 400, -32020, TEXT("no Mcp-Name"));
		ExpectError(*Send(Core, Inbound(Call, TEXT("tools/call"), TEXT("other-tool"))), 400, -32020, TEXT("Mcp-Name differs"));
		ExpectError(*Send(Core, Inbound(Call, TEXT("tools/call"), TEXT("=?base64?***?="))), 400, -32020, TEXT("invalid Base64"));

		// The Base64 sentinel form of the right name is accepted
		const FString Encoded = FString::Printf(TEXT("=?base64?%s?="), *FBase64::Encode(FString(EchoToolName)));
		ModernResult(*Send(Core, Inbound(Call, TEXT("tools/call"), Encoded)), TEXT("Base64 Mcp-Name"));

		// resources/read names its uri
		const FString Read = Body(TEXT("2"), TEXT("resources/read"), TEXT(",\"uri\":\"nodetocode://no-such-resource\""));
		ExpectError(*Send(Core, Inbound(Read, TEXT("resources/read"), TEXT("nodetocode://other"))), 400, -32020, TEXT("resources/read Mcp-Name differs"));
	});

	It(TEXT("answers an unsupported version with -32022 naming the supported ones, and an unknown method with 404 -32601"), [this]()
	{
		FN2CMcpProtocol Core;
		const TSharedPtr<FJsonObject> Error = ExpectError(*Send(Core, Inbound(Body(TEXT("1"), TEXT("tools/list"), FString(), Meta(TEXT("2099-01-01"))),
			TEXT("tools/list"), FString(), TEXT("2099-01-01"))), 400, -32022, TEXT("unsupported version"));
		if (Error.IsValid())
		{
			const TSharedPtr<FJsonObject>* Data = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Supported = nullptr;
			TestTrue(TEXT("data.requested"), Error->TryGetObjectField(TEXT("data"), Data)
				&& (*Data)->GetStringField(TEXT("requested")) == TEXT("2099-01-01"));
			TestTrue(TEXT("data.supported"), Data && (*Data)->TryGetArrayField(TEXT("supported"), Supported)
				&& Supported->Num() == 1 && (*Supported)[0]->AsString() == Version);
		}
		// A legacy version sent per request is not a modern one either
		ExpectError(*Send(Core, Inbound(Body(TEXT("2"), TEXT("tools/list"), FString(), Meta(TEXT("2025-11-25"))),
			TEXT("tools/list"), FString(), TEXT("2025-11-25"))), 400, -32022, TEXT("legacy version in _meta"));

		ExpectError(*Send(Core, Inbound(Body(TEXT("3"), TEXT("ping")), TEXT("ping"))), 404, JsonRpcErrorCodes::MethodNotFound, TEXT("ping is gone"));
		ExpectError(*Send(Core, Inbound(Body(TEXT("4"), TEXT("logging/setLevel"), TEXT(",\"level\":\"info\"")), TEXT("logging/setLevel"))),
			404, JsonRpcErrorCodes::MethodNotFound, TEXT("logging/setLevel is gone"));
		ExpectError(*Send(Core, Inbound(Body(TEXT("5"), TEXT("nodetocode/cancelTask")), TEXT("nodetocode/cancelTask"))),
			404, JsonRpcErrorCodes::MethodNotFound, TEXT("the legacy custom method"));
	});

	It(TEXT("answers an unknown resource with -32602, a batch with 400, and accepts a notification with 202"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Uri = TEXT("nodetocode://no-such-resource");
		ExpectError(*Send(Core, Inbound(Body(TEXT("1"), TEXT("resources/read"), FString::Printf(TEXT(",\"uri\":\"%s\""), *Uri)),
			TEXT("resources/read"), Uri)), 200, JsonRpcErrorCodes::InvalidParams, TEXT("unknown resource"));

		ExpectError(*Send(Core, Inbound(FString::Printf(TEXT("[%s]"), *Body(TEXT("2"), TEXT("tools/list"))), TEXT("tools/list"))),
			400, JsonRpcErrorCodes::InvalidRequest, TEXT("batch"));

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Note = Send(Core, Inbound(FString::Printf(
			TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/whatever\",\"params\":{%s}}"), *Meta()), TEXT("notifications/whatever")));
		TestEqual(TEXT("notification: 202"), Note->LastReply().Status, 202);
		TestTrue(TEXT("notification: no body"), Note->LastReply().Body.IsEmpty());
	});

	It(TEXT("streams a long-running call with resultType on the response, or holds one decorated reply when it cannot stream"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Call = Body(TEXT("7"), TEXT("tools/call"), CallParams(LongToolName));

		// Streaming transport, no progressToken: still a stream, so closing it can cancel the call
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Streamed = Send(Core, Inbound(Call, TEXT("tools/call"), LongToolName), true);
		LongTool->Finish(TEXT("done"));
		TestEqual(TEXT("stream: closed once"), Streamed->CloseCount, 1);
		TestEqual(TEXT("stream: no single reply"), Streamed->ReplyCount, 0);
		const TSharedPtr<FJsonObject> Last = Streamed->Streamed.Num() > 0 ? ParseObject(Streamed->Streamed.Last()) : nullptr;
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (TestTrue(TEXT("stream: last message is the response"), Last.IsValid() && Last->TryGetObjectField(TEXT("result"), Result)))
		{
			TestEqual(TEXT("stream: resultType"), (*Result)->GetStringField(TEXT("resultType")), FString(TEXT("complete")));
			TestTrue(TEXT("stream: serverInfo"), (*Result)->HasField(TEXT("_meta")));
		}
		for (const FString& Message : Streamed->Streamed)
		{
			TestFalse(TEXT("stream: no progress without a progressToken"), Message.Contains(TEXT("notifications/progress")));
		}

		// No stream (UE 5.6/5.7): one held reply, decorated
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Held = Send(Core, Inbound(Call, TEXT("tools/call"), LongToolName), false);
		TestEqual(TEXT("held: nothing yet"), Held->ReplyCount, 0);
		LongTool->Finish(TEXT("done"));
		ModernResult(*Held, TEXT("held reply"));
	});

	It(TEXT("cancels a long-running call when its client closes the stream, and keeps an open one alive"), [this]()
	{
		FN2CMcpProtocol Core;
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Inbound(
			Body(TEXT("8"), TEXT("tools/call"), CallParams(LongToolName)), TEXT("tools/call"), LongToolName), true);
		if (!TestTrue(TEXT("the call runs"), LongTool->LastContext.IsValid() && !LongTool->LastContext->IsCancelled()))
		{
			return;
		}

		Core.TickStreamsForTesting();
		TestEqual(TEXT("a keep-alive on the open stream"), Responder->KeepAliveCount, 1);
		TestFalse(TEXT("still running"), LongTool->LastContext->IsCancelled());

		Responder->bClosedByClient = true;
		Core.TickStreamsForTesting();
		TestTrue(TEXT("closing the stream cancelled the call"), LongTool->LastContext->IsCancelled());
		TestEqual(TEXT("no keep-alive after the close"), Responder->KeepAliveCount, 1);
	});

	It(TEXT("opens subscriptions/listen with an acknowledgement, tags list-changed notifications and ends gracefully"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Listen = Body(TEXT("\"sub-1\""), TEXT("subscriptions/listen"),
			TEXT(",\"notifications\":{\"toolsListChanged\":true,\"promptsListChanged\":true,\"resourceSubscriptions\":[\"nodetocode://x\"]}"));

		// No stream: this engine cannot keep it open
		ExpectError(*Send(Core, Inbound(Listen, TEXT("subscriptions/listen")), false), 404, JsonRpcErrorCodes::MethodNotFound, TEXT("no stream"));
		TestEqual(TEXT("none open"), Core.GetSubscriptionCount(), 0);

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Inbound(Listen, TEXT("subscriptions/listen")), true);
		TestEqual(TEXT("one open"), Core.GetSubscriptionCount(), 1);
		const TSharedPtr<FJsonObject> Ack = Responder->Streamed.Num() > 0 ? ParseObject(Responder->Streamed[0]) : nullptr;
		if (TestTrue(TEXT("acknowledged first"), Ack.IsValid() && Ack->GetStringField(TEXT("method")) == TEXT("notifications/subscriptions/acknowledged")))
		{
			const TSharedPtr<FJsonObject> Params = Ack->GetObjectField(TEXT("params"));
			TestEqual(TEXT("ack subscriptionId"), Params->GetObjectField(TEXT("_meta"))->GetStringField(TEXT("io.modelcontextprotocol/subscriptionId")), FString(TEXT("sub-1")));
			const TSharedPtr<FJsonObject> Accepted = Params->GetObjectField(TEXT("notifications"));
			TestTrue(TEXT("toolsListChanged honoured"), Accepted->GetBoolField(TEXT("toolsListChanged")));
			TestFalse(TEXT("prompts left out (they never change)"), Accepted->HasField(TEXT("promptsListChanged")));
			TestFalse(TEXT("resource updates left out"), Accepted->HasField(TEXT("resourceSubscriptions")));
		}

		FN2CMcpToolManager::Get().OnToolListChanged().Broadcast();
		const TSharedPtr<FJsonObject> Changed = Responder->Streamed.Num() > 1 ? ParseObject(Responder->Streamed[1]) : nullptr;
		if (TestTrue(TEXT("list_changed delivered"), Changed.IsValid() && Changed->GetStringField(TEXT("method")) == TEXT("notifications/tools/list_changed")))
		{
			TestEqual(TEXT("tagged with the subscription"), Changed->GetObjectField(TEXT("params"))->GetObjectField(TEXT("_meta"))
				->GetStringField(TEXT("io.modelcontextprotocol/subscriptionId")), FString(TEXT("sub-1")));
		}
		TestEqual(TEXT("the stream stays open"), Responder->CloseCount, 0);

		Core.CloseSubscriptions();
		const TSharedPtr<FJsonObject> Final = Responder->Streamed.Num() > 0 ? ParseObject(Responder->Streamed.Last()) : nullptr;
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (TestTrue(TEXT("graceful end: the response to subscriptions/listen"), Final.IsValid() && Final->TryGetObjectField(TEXT("result"), Result)))
		{
			TestEqual(TEXT("its id"), Final->GetStringField(TEXT("id")), FString(TEXT("sub-1")));
			TestEqual(TEXT("resultType"), (*Result)->GetStringField(TEXT("resultType")), FString(TEXT("complete")));
		}
		TestEqual(TEXT("closed"), Responder->CloseCount, 1);
		TestEqual(TEXT("none open after the close"), Core.GetSubscriptionCount(), 0);

		// A client that closes its listen stream is dropped on the next beat
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Second = Send(Core, Inbound(Listen, TEXT("subscriptions/listen")), true);
		TestEqual(TEXT("second open"), Core.GetSubscriptionCount(), 1);
		Second->bClosedByClient = true;
		Core.TickStreamsForTesting();
		TestEqual(TEXT("dropped after the client closed it"), Core.GetSubscriptionCount(), 0);
	});

	It(TEXT("keeps legacy initialize traffic as it was: initialize opens a session even with a modern header"), [this]()
	{
		FN2CMcpProtocol Core;
		FN2CMcpInbound Init = N2CTest::MakeMcpInbound(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":")
			TEXT("{\"protocolVersion\":\"2026-07-28\",\"capabilities\":{},\"clientInfo\":{\"name\":\"x\",\"version\":\"1\"}}}"));
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Init);
		TestEqual(TEXT("status"), Responder->LastReply().Status, 200);
		TestFalse(TEXT("a session"), Responder->LastReply().SessionId.IsEmpty());
		TestTrue(TEXT("negotiated 2025-11-25"), Responder->LastReply().Body.Contains(TEXT("\"protocolVersion\":\"2025-11-25\"")));
		TestFalse(TEXT("no resultType in a legacy result"), Responder->LastReply().Body.Contains(TEXT("resultType")));
	});

	It(TEXT("decodes Mcp-Name values: plain, Base64 sentinel, and refuses unsafe ones"), [this]()
	{
		TestEqual(TEXT("plain"), FN2CMcpProtocol::DecodeHeaderValue(TEXT("get-focused-blueprint")).Get(FString()), FString(TEXT("get-focused-blueprint")));
		const FString Utf8Name = TEXT("Hello, 世界");
		FString Encoded;
		{
			const FTCHARToUTF8 Utf8(*Utf8Name);
			Encoded = FString::Printf(TEXT("=?base64?%s?="), *FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
		}
		TestEqual(TEXT("Base64 of UTF-8"), FN2CMcpProtocol::DecodeHeaderValue(Encoded).Get(FString()), Utf8Name);
		TestFalse(TEXT("a newline is refused"), FN2CMcpProtocol::DecodeHeaderValue(TEXT("a\nb")).IsSet());
		TestFalse(TEXT("non-ASCII unencoded is refused"), FN2CMcpProtocol::DecodeHeaderValue(Utf8Name).IsSet());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
