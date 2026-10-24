// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpBodyParsingTest
{
	/** The bytes an HTTP client would send: UTF-8, no terminator, sized like Content-Length. */
	TArray<uint8> Utf8Body(const FString& Text)
	{
		const FTCHARToUTF8 Converter(*Text);
		return TArray<uint8>(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	}

	TSharedPtr<FJsonValue> ParseText(const FString& Text)
	{
		TSharedPtr<FJsonValue> Value;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Value);
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CMcpBodyParsingTest, "NodeToCode.MCP.Transport.BodyParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CMcpBodyParsingTest::RunTest(const FString& Parameters)
{
	using namespace N2CMcpBodyParsingTest;

	// A JSON-RPC batch of two pings parses to an array and gets an array of two responses on one line
	{
		const TArray<uint8> Body = Utf8Body(TEXT("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"},{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}]"));
		TSharedPtr<FJsonValue> Value;
		TestTrue(TEXT("batch parses"), FN2CMcpProtocol::ParseBody(Body, Value));
		if (TestTrue(TEXT("batch is an array"), Value.IsValid() && Value->Type == EJson::Array))
		{
			TestEqual(TEXT("batch keeps both items"), Value->AsArray().Num(), 2);
		}

		FString ResponseBody;
		int32 StatusCode = 0;
		bool bWasInitialize = false;
		FN2CMcpProtocol().ProcessMessage(Value, ResponseBody, StatusCode, bWasInitialize);
		TestEqual(TEXT("batch status is 200"), StatusCode, 200);
		TestFalse(TEXT("batch response has no newline"), ResponseBody.Contains(TEXT("\n")));

		const TSharedPtr<FJsonValue> Response = ParseText(ResponseBody);
		if (TestTrue(TEXT("batch response is an array"), Response.IsValid() && Response->Type == EJson::Array))
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = Response->AsArray();
			if (TestEqual(TEXT("batch response has two items"), Items.Num(), 2))
			{
				TestEqual(TEXT("first response id"), Items[0]->AsObject()->GetNumberField(TEXT("id")), 1.0);
				TestEqual(TEXT("second response id"), Items[1]->AsObject()->GetNumberField(TEXT("id")), 2.0);
			}
		}
	}

	// Trailing bytes after a single object are a parse error (-32700, HTTP 400), not silently ignored
	{
		const TArray<uint8> Body = Utf8Body(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}xyz"));
		TSharedPtr<FJsonValue> Value;
		TestFalse(TEXT("trailing garbage fails to parse"), FN2CMcpProtocol::ParseBody(Body, Value));
		TestFalse(TEXT("trailing garbage gives no value"), Value.IsValid());

		FString ResponseBody;
		int32 StatusCode = 0;
		bool bWasInitialize = false;
		FN2CMcpProtocol().ProcessMessage(Value, ResponseBody, StatusCode, bWasInitialize);
		TestEqual(TEXT("parse error status is 400"), StatusCode, 400);
		const TSharedPtr<FJsonValue> Response = ParseText(ResponseBody);
		const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
		if (TestTrue(TEXT("parse error response has an error object"), Response.IsValid() && Response->Type == EJson::Object
			&& Response->AsObject()->TryGetObjectField(TEXT("error"), ErrorObject)))
		{
			TestEqual(TEXT("parse error code is -32700"), (*ErrorObject)->GetNumberField(TEXT("code")), static_cast<double>(JsonRpcErrorCodes::ParseError));
		}
	}

	// Trailing whitespace is still a valid body, and non-ASCII UTF-8 is converted exactly
	{
		const TArray<uint8> Body = Utf8Body(FString(TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"caf")) + TCHAR(0x00E9) + TEXT("\",\"method\":\"ping\"}\r\n"));
		TSharedPtr<FJsonValue> Value;
		TestTrue(TEXT("trailing whitespace parses"), FN2CMcpProtocol::ParseBody(Body, Value));

		FString ResponseBody;
		int32 StatusCode = 0;
		bool bWasInitialize = false;
		FN2CMcpProtocol().ProcessMessage(Value, ResponseBody, StatusCode, bWasInitialize);
		TestEqual(TEXT("single ping status is 200"), StatusCode, 200);
		const TSharedPtr<FJsonValue> Response = ParseText(ResponseBody);
		if (TestTrue(TEXT("single response is an object"), Response.IsValid() && Response->Type == EJson::Object))
		{
			TestEqual(TEXT("non-ASCII id round-trips"), Response->AsObject()->GetStringField(TEXT("id")), FString(TEXT("caf")) + TCHAR(0x00E9));
		}
	}

	// An empty body is a parse error
	{
		TSharedPtr<FJsonValue> Value;
		TestFalse(TEXT("empty body fails to parse"), FN2CMcpProtocol::ParseBody(TArray<uint8>(), Value));
	}

	// Wire responses are condensed: a tools/list response is one line
	{
		TSharedPtr<FJsonValue> Value;
		FN2CMcpProtocol::ParseBody(Utf8Body(TEXT("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}")), Value);
		FString ResponseBody;
		int32 StatusCode = 0;
		bool bWasInitialize = false;
		FN2CMcpProtocol().ProcessMessage(Value, ResponseBody, StatusCode, bWasInitialize);
		TestEqual(TEXT("tools/list status is 200"), StatusCode, 200);
		TestTrue(TEXT("tools/list response is not empty"), ResponseBody.Len() > 0);
		TestFalse(TEXT("tools/list response has no newline"), ResponseBody.Contains(TEXT("\n")));
		TestFalse(TEXT("tools/list response has no tab"), ResponseBody.Contains(TEXT("\t")));

		FJsonRpcNotification ProgressNotification;
		ProgressNotification.Method = TEXT("notifications/progress");
		TSharedPtr<FJsonObject> ProgressParams = MakeShared<FJsonObject>();
		ProgressParams->SetStringField(TEXT("progressToken"), TEXT("t"));
		ProgressParams->SetNumberField(TEXT("progress"), 0.5);
		ProgressNotification.Params = MakeShared<FJsonValueObject>(ProgressParams);
		const FString Notification = FJsonRpcUtils::SerializeNotification(ProgressNotification);
		TestFalse(TEXT("notification has no newline"), Notification.Contains(TEXT("\n")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
