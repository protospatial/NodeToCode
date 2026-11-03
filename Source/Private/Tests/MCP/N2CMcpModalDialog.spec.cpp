// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpModalSpec
{
	const TCHAR* EchoToolName = TEXT("n2c-test-modal-echo");
	const TCHAR* LongToolName = TEXT("n2c-test-modal-long");

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
			FMcpToolDefinition Definition(EchoToolName, TEXT("Spec tool registered by NodeToCode.MCP.ModalDialog"));
			Definition.InputSchema = ObjectSchema();
			return Definition;
		}
		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>&) override
		{
			++Calls;
			return FMcpToolCallResult::CreateTextResult(TEXT("echoed"));
		}
		int32 Calls = 0;
	};

	class FLongTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(LongToolName, TEXT("Long-running spec tool registered by NodeToCode.MCP.ModalDialog"));
			Definition.InputSchema = ObjectSchema();
			Definition.bIsLongRunning = true;
			return Definition;
		}
		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef&, const TSharedPtr<FJsonObject>&) override
		{
			++Calls;
			return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateTextResult(TEXT("long"))).GetFuture();
		}
		int32 Calls = 0;
	};

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString Initialize()
	{
		return TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},")
			TEXT("\"clientInfo\":{\"name\":\"n2c-modal-spec\",\"version\":\"1\"}}}");
	}

	FString Call(int32 Id, const FString& Tool, const FString& Arguments = TEXT("{}"))
	{
		return FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}"),
			Id, *Tool, *Arguments);
	}

	/** The first text of a tools/call reply's CallToolResult, and whether it is an error */
	FString CallText(const FString& Body, bool& bOutError)
	{
		bOutError = false;
		const TSharedPtr<FJsonObject> Message = ParseObject(Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (!Message.IsValid() || !Message->TryGetObjectField(TEXT("result"), Result))
		{
			return FString();
		}
		(*Result)->TryGetBoolField(TEXT("isError"), bOutError);
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		return (*Result)->TryGetArrayField(TEXT("content"), Content) && Content->Num() > 0
			? (*Content)[0]->AsObject()->GetStringField(TEXT("text")) : FString();
	}
}

/**
 * Card 0209: a modal dialog pauses the core ticker, and with it HTTPServer. The server ticks HTTPServer from
 * FSlateApplication's modal loop instead (TickDuringModalLoop), and while a modal is open requests that need the editor
 * answer at once with MODAL_DIALOG_OPEN (tools/call as an isError result, resources/read and prompts/get as a JSON-RPC
 * error); initialize, ping, the lists, the editor-free tools and /mcp/health are served. The protocol cases use the
 * IsModalDialogOpen override; the transport case starts the real server and pumps only the modal tick, as the
 * engine's modal loop does.
 */
BEGIN_DEFINE_SPEC(FN2CMcpModalDialogSpec, "NodeToCode.MCP.ModalDialog", N2C_TEST_FLAGS)

	TSharedPtr<N2CMcpModalSpec::FEchoTool> EchoTool;
	TSharedPtr<N2CMcpModalSpec::FLongTool> LongTool;
	bool bRegisteredEcho = false;
	bool bRegisteredLong = false;

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(FN2CMcpProtocol& Core, const FString& Body, const FString& SessionId = FString())
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
		Core.Handle(N2CTest::MakeMcpInbound(Body, TEXT("application/json, text/event-stream"), SessionId), Responder);
		return Responder;
	}

END_DEFINE_SPEC(FN2CMcpModalDialogSpec)

void FN2CMcpModalDialogSpec::Define()
{
	using namespace N2CMcpModalSpec;

	BeforeEach([this]()
	{
		EchoTool = MakeShared<FEchoTool>();
		LongTool = MakeShared<FLongTool>();
		bRegisteredEcho = FN2CMcpToolManager::Get().RegisterTool(EchoTool);
		bRegisteredLong = FN2CMcpToolManager::Get().RegisterTool(LongTool);
	});

	AfterEach([this]()
	{
		FN2CMcpProtocol::SetModalDialogOpenForTests({});
		if (bRegisteredEcho)
		{
			FN2CMcpToolManager::Get().UnregisterTool(EchoToolName);
		}
		if (bRegisteredLong)
		{
			FN2CMcpToolManager::Get().UnregisterTool(LongToolName);
		}
		EchoTool.Reset();
		LongTool.Reset();
	});

	It(TEXT("answers what needs no editor and refuses editor work with MODAL_DIALOG_OPEN while a modal is open"), [this]()
	{
		FN2CMcpProtocol Core;
		FN2CMcpProtocol::SetModalDialogOpenForTests(true);

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Init = Send(Core, Initialize());
		const FString Session = Init->LastReply().SessionId;
		TestFalse(TEXT("initialize opens a session"), Session.IsEmpty());
		const TSharedPtr<FJsonObject> Ping = ParseObject(Send(Core, TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}"), Session)->LastReply().Body);
		TestTrue(TEXT("ping answered"), Ping.IsValid() && Ping->HasField(TEXT("result")));
		const TSharedPtr<FJsonObject> List = ParseObject(Send(Core, TEXT("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}"), Session)->LastReply().Body);
		TestTrue(TEXT("tools/list answered"), List.IsValid() && List->HasField(TEXT("result")));

		bool bError = false;
		const FString Text = CallText(Send(Core, Call(4, EchoToolName), Session)->LastReply().Body, bError);
		TestTrue(TEXT("a tool call is an error result"), bError);
		TestTrue(TEXT("with MODAL_DIALOG_OPEN: ") + Text, Text.StartsWith(TEXT("MODAL_DIALOG_OPEN")));
		TestEqual(TEXT("the tool did not run"), EchoTool->Calls, 0);

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Long = Send(Core, Call(5, LongToolName), Session);
		const FString LongText = CallText(Long->LastReply().Body, bError);
		TestTrue(TEXT("a long-running call is answered at once with MODAL_DIALOG_OPEN"), Long->ReplyCount == 1 && bError && LongText.StartsWith(TEXT("MODAL_DIALOG_OPEN")));
		TestEqual(TEXT("and not started"), LongTool->Calls, 0);

		const FString ReadFile = CallText(Send(Core, Call(6, TEXT("read-file"), TEXT("{\"path\":\"N2CNoSuchFile.txt\"}")), Session)->LastReply().Body, bError);
		TestFalse(TEXT("read-file needs no editor: not refused (") + ReadFile + TEXT(")"), ReadFile.StartsWith(TEXT("MODAL_DIALOG_OPEN")));

		const TSharedPtr<FJsonObject> Read = ParseObject(Send(Core,
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"resources/read\",\"params\":{\"uri\":\"nodetocode://blueprint/current\"}}"), Session)->LastReply().Body);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		TestTrue(TEXT("resources/read is a JSON-RPC error naming the dialog"), Read.IsValid() && Read->TryGetObjectField(TEXT("error"), Error)
			&& (*Error)->GetStringField(TEXT("message")).StartsWith(TEXT("MODAL_DIALOG_OPEN")));

		// The dialog closed: the same call runs
		FN2CMcpProtocol::SetModalDialogOpenForTests(false);
		const FString After = CallText(Send(Core, Call(8, EchoToolName), Session)->LastReply().Body, bError);
		TestTrue(TEXT("after the dialog the tool runs"), !bError && After == TEXT("echoed") && EchoTool->Calls == 1);
	});

	It(TEXT("keeps the HTTP server answering from the modal loop tick, with the core ticker paused"), [this]()
	{
		FN2CMcpHttpServerManager& Server = FN2CMcpHttpServerManager::Get();
		if (Server.IsServerRunning())
		{
			AddInfo(TEXT("The MCP server is running in this editor (-N2CMcp); skipped so its clients are not disturbed"));
			return;
		}
		N2CTest::FScratchDir Scratch(TEXT("N2CMcpModalDialog"));
		Server.SetDiscoveryPathsForTesting(Scratch.Path / TEXT("mcp-server.json"), Scratch.Path / TEXT("servers"));
		Server.SetNotificationHandlerForTesting([](const FString&, bool) {});
		ON_SCOPE_EXIT
		{
			FN2CMcpHttpServerManager::Get().StopServer();
			FN2CMcpHttpServerManager::Get().SetNotificationHandlerForTesting(nullptr);
			FN2CMcpHttpServerManager::Get().SetDiscoveryPathsForTesting(FString(), FString());
		};
		if (!TestTrue(TEXT("server started"), Server.StartServer(31000 + FMath::RandRange(0, 8000), 10)))
		{
			return;
		}
		FN2CMcpProtocol::SetModalDialogOpenForTests(true);
		const FString Base = FString::Printf(TEXT("http://127.0.0.1:%d"), Server.GetServerPort());

		// One request, pumped only by the modal loop tick (HTTPServer) and the HTTP client, as the engine's modal loop
		// pumps the platform; returns {status, body, seconds}
		struct FAnswer { int32 Status = -1; FString Body; FString SessionId; double Seconds = 0.0; };
		auto Ask = [&Server](const FString& Verb, const FString& Url, const FString& Body, const FString& SessionId)
		{
			const TSharedRef<FAnswer> Answer = MakeShared<FAnswer>();
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
			Http->SetVerb(Verb);
			Http->SetURL(Url);
			Http->SetTimeout(10.0f);
			Http->SetHeader(TEXT("Accept"), TEXT("application/json, text/event-stream"));
			if (!Body.IsEmpty())
			{
				Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				Http->SetContentAsString(Body);
			}
			if (!SessionId.IsEmpty())
			{
				Http->SetHeader(TEXT("Mcp-Session-Id"), SessionId);
			}
			Http->OnProcessRequestComplete().BindLambda([Answer](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
			{
				Answer->Status = bOk && Response.IsValid() ? Response->GetResponseCode() : 0;
				Answer->Body = Response.IsValid() ? Response->GetContentAsString() : FString();
				Answer->SessionId = Response.IsValid() ? Response->GetHeader(TEXT("Mcp-Session-Id")) : FString();
			});
			const double Start = FPlatformTime::Seconds();
			Http->ProcessRequest();
			double Last = Start;
			FHttpManager& Manager = FHttpModule::Get().GetHttpManager();
			while (Answer->Status < 0 && FPlatformTime::Seconds() - Start < 5.0)
			{
				FPlatformProcess::Sleep(1.0f / 60.0f);
				const double Now = FPlatformTime::Seconds();
				const float Delta = static_cast<float>(Now - Last);
				Last = Now;
				Server.TickDuringModalLoop(Delta);
				Manager.Tick(Delta);
			}
			Answer->Seconds = FPlatformTime::Seconds() - Start;
			if (Answer->Status < 0)
			{
				Http->CancelRequest();
			}
			return Answer;
		};

		const int32 TicksBefore = Server.GetModalTickCount();
		const TSharedRef<FAnswer> Health = Ask(TEXT("GET"), Base + TEXT("/mcp/health"), FString(), FString());
		TestEqual(TEXT("/mcp/health answered during the modal"), Health->Status, 200);
		TestTrue(FString::Printf(TEXT("within a second (%.2f s)"), Health->Seconds), Health->Seconds < 1.0);
		TestTrue(TEXT("by the modal loop tick"), Server.GetModalTickCount() > TicksBefore);

		const TSharedRef<FAnswer> Init = Ask(TEXT("POST"), Base + TEXT("/mcp"), Initialize(), FString());
		TestEqual(TEXT("initialize answered"), Init->Status, 200);
		const TSharedRef<FAnswer> Ping = Ask(TEXT("POST"), Base + TEXT("/mcp"), TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}"), Init->SessionId);
		TestTrue(FString::Printf(TEXT("ping answered within a second (%.2f s)"), Ping->Seconds), Ping->Status == 200 && Ping->Seconds < 1.0);
		const TSharedRef<FAnswer> Busy = Ask(TEXT("POST"), Base + TEXT("/mcp"), Call(3, EchoToolName), Init->SessionId);
		bool bError = false;
		const FString Text = CallText(Busy->Body, bError);
		TestTrue(FString::Printf(TEXT("a tool call answers MODAL_DIALOG_OPEN within two seconds (%.2f s): %s"), Busy->Seconds, *Text),
			Busy->Status == 200 && bError && Text.StartsWith(TEXT("MODAL_DIALOG_OPEN")) && Busy->Seconds < 2.0);
		TestEqual(TEXT("handlers left the stack"), Server.GetHandlerDepth(), 0);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
