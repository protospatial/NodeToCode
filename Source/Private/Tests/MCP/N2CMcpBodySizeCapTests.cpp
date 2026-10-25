// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpBodySizeCapTest
{
	/**
	 * A JSON-RPC request whose UTF-8 body is exactly Bytes long: ParamsHead (the params members before the padding,
	 * empty or ending in a comma) then a "pad" string member that fills the rest
	 */
	TArray<uint8> SizedBody(const FString& Method, int32 Bytes, const FString& ParamsHead = FString())
	{
		const FString Head = FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":{%s\"pad\":\""), *Method, *ParamsHead);
		const FTCHARToUTF8 HeadUtf8(*Head);
		const int32 PadLen = FMath::Max(0, Bytes - HeadUtf8.Length() - 3);
		TArray<uint8> Body;
		Body.Reserve(HeadUtf8.Length() + PadLen + 3);
		Body.Append(reinterpret_cast<const uint8*>(HeadUtf8.Get()), HeadUtf8.Length());
		Body.AddUninitialized(PadLen);
		FMemory::Memset(Body.GetData() + HeadUtf8.Length(), 'x', PadLen);
		Body.Append({ uint8('"'), uint8('}'), uint8('}') });
		return Body;
	}

	/** The params of a valid initialize, before the padding */
	const TCHAR* InitializeParams = TEXT("\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-body-cap\",\"version\":\"1.0\"},");

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	/** The JSON-RPC error code of a message with a null id, or 0 when it is not one */
	int32 NullIdErrorCode(const FString& Json)
	{
		const TSharedPtr<FJsonObject> Message = ParseObject(Json);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		if (!Message.IsValid() || !Message->HasTypedField<EJson::Null>(TEXT("id")) || !Message->TryGetObjectField(TEXT("error"), Error))
		{
			return 0;
		}
		return static_cast<int32>((*Error)->GetNumberField(TEXT("code")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CMcpBodySizeCapTest, "NodeToCode.MCP.Transport.BodySizeCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CMcpBodySizeCapTest::RunTest(const FString& Parameters)
{
	using namespace N2CMcpBodySizeCapTest;
	const int32 Limit = FN2CMcpProtocol::MaxRequestBodyBytes;
	TestEqual(TEXT("the documented limit is 8 MiB"), Limit, 8 * 1024 * 1024);

	// Protocol core: one byte over the limit is 413 with a JSON-RPC -32600 error and a null id, before the session
	// check (no Mcp-Session-Id is sent) and before parsing; an initialize that large opens no session
	{
		FN2CMcpProtocol Core;
		FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(FString());
		Inbound.Body = SizedBody(TEXT("initialize"), Limit + 1, InitializeParams);
		TestEqual(TEXT("core: body is one byte over"), Inbound.Body.Num(), Limit + 1);
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
		Core.Handle(Inbound, Responder);
		TestEqual(TEXT("core: one reply"), Responder->ReplyCount, 1);
		TestEqual(TEXT("core: 413"), Responder->LastReply().Status, 413);
		TestEqual(TEXT("core: -32600 with a null id"), NullIdErrorCode(Responder->LastReply().Body), JsonRpcErrorCodes::InvalidRequest);
		TestTrue(TEXT("core: message names the limit"), Responder->LastReply().Body.Contains(FString::FromInt(Limit)));
		TestEqual(TEXT("core: no session opened"), Core.GetSessionCount(), 0);
	}

	// Protocol core: a body of exactly the limit is processed (here an initialize, answered 200 with a session)
	{
		FN2CMcpProtocol Core;
		FN2CMcpInbound Inbound = N2CTest::MakeMcpInbound(FString());
		Inbound.Body = SizedBody(TEXT("initialize"), Limit, InitializeParams);
		TestEqual(TEXT("at limit: body is exactly the limit"), Inbound.Body.Num(), Limit);
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>();
		Core.Handle(Inbound, Responder);
		TestEqual(TEXT("at limit: 200"), Responder->LastReply().Status, 200);
		TestEqual(TEXT("at limit: session opened"), Core.GetSessionCount(), 1);
	}

	// HTTP transport: FN2CMcpHttpServerManager::HandleMcpRequest answers 413 (application/json, -32600, null id) for
	// a body over the limit without handing it to the core
	{
		FHttpServerRequest Request;
		Request.Verb = EHttpServerRequestVerbs::VERB_POST;
		Request.Headers.Add(TEXT("Content-Type"), { TEXT("application/json") });
		Request.Headers.Add(TEXT("Accept"), { TEXT("application/json, text/event-stream") });
		Request.Body = SizedBody(TEXT("ping"), Limit + 1);

		TArray<TUniquePtr<FHttpServerResponse>> Responses;
		const FHttpResultCallback OnComplete = [&Responses](TUniquePtr<FHttpServerResponse>&& Response)
		{
			Responses.Add(MoveTemp(Response));
		};
		TestTrue(TEXT("http: handled"), FN2CMcpHttpServerManager::Get().HandleMcpRequest(Request, OnComplete));
		if (TestEqual(TEXT("http: one response"), Responses.Num(), 1))
		{
			TestEqual(TEXT("http: 413"), static_cast<int32>(Responses[0]->Code), 413);
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Responses[0]->Body.GetData()), Responses[0]->Body.Num());
			TestEqual(TEXT("http: -32600 with a null id"), NullIdErrorCode(FString(Text.Length(), Text.Get())), JsonRpcErrorCodes::InvalidRequest);
			const TArray<FString>* ContentType = Responses[0]->Headers.Find(TEXT("content-type"));
			TestTrue(TEXT("http: application/json"), ContentType && ContentType->Num() > 0 && (*ContentType)[0].StartsWith(TEXT("application/json")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
