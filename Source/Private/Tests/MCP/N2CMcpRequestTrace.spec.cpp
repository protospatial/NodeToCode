// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpRequestTrace.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Misc/ScopeExit.h"

namespace N2CMcpRequestTraceSpec
{
	const TCHAR* ToolName = TEXT("n2c-test-trace-tool");
	const TCHAR* LongToolName = TEXT("n2c-test-trace-long");
	const TCHAR* LinePrefix = TEXT("MCP request: method=");
	const TCHAR* Traceparent = TEXT("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");

	TSharedPtr<FJsonObject> ObjectSchema()
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		return Schema;
	}

	/** Answers "ok", or an isError result when called with {"fail": true} */
	class FTraceTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(ToolName, TEXT("Spec tool registered by NodeToCode.MCP.RequestTrace"));
			Definition.InputSchema = ObjectSchema();
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			bool bFail = false;
			Arguments->TryGetBoolField(TEXT("fail"), bFail);
			return bFail ? FMcpToolCallResult::CreateErrorResult(TEXT("asked to fail")) : FMcpToolCallResult::CreateTextResult(TEXT("ok"));
		}
	};

	/** A long-running tool the spec finishes by hand */
	class FLongTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(LongToolName, TEXT("Long-running spec tool registered by NodeToCode.MCP.RequestTrace"));
			Definition.InputSchema = ObjectSchema();
			Definition.bIsLongRunning = true;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef&, const TSharedPtr<FJsonObject>&) override
		{
			Promise = MakeShared<TPromise<FMcpToolCallResult>>();
			return Promise->GetFuture();
		}

		void Finish()
		{
			if (Promise.IsValid())
			{
				const TSharedPtr<TPromise<FMcpToolCallResult>> Pending = MoveTemp(Promise);
				Pending->SetValue(FMcpToolCallResult::CreateTextResult(TEXT("long done")));
			}
		}

		TSharedPtr<TPromise<FMcpToolCallResult>> Promise;
	};

	FString Request(int32 Id, const FString& Method, const FString& ParamsJson)
	{
		return FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}"), Id, *Method, *ParamsJson);
	}

	/** The trace lines logged since the logger was cleared, oldest first */
	TArray<FString> TraceLines()
	{
		TArray<FString> Lines;
		for (const FN2CError& Record : FN2CLogger::Get().GetErrors())
		{
			if (Record.Severity == EN2CLogSeverity::Info && Record.Message.StartsWith(LinePrefix))
			{
				Lines.Add(Record.Message);
			}
		}
		return Lines;
	}
}

/**
 * One Info line per MCP request (card 0193, imp-mcp-transport-11): FN2CMcpProtocol::Handle answers through
 * FN2CMcpTracingResponder, which logs method, tool, status, isError, duration in ms and params._meta.traceparent when
 * the answer is written, including a long-running call answered after Handle returned.
 */
BEGIN_DEFINE_SPEC(FN2CMcpRequestTraceSpec, "NodeToCode.MCP.RequestTrace", N2C_TEST_FLAGS)

	TSharedPtr<N2CMcpRequestTraceSpec::FTraceTool> Tool;
	TSharedPtr<N2CMcpRequestTraceSpec::FLongTool> LongTool;
	bool bRegistered = false;
	bool bRegisteredLong = false;
	EN2CLogSeverity SavedSeverity = EN2CLogSeverity::Info;

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(FN2CMcpProtocol& Core, const FString& Body, const FString& SessionId = FString())
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
		Core.Handle(N2CTest::MakeMcpInbound(Body, TEXT("application/json, text/event-stream"), SessionId), Responder);
		return Responder;
	}

	/** Initializes a session and returns its id, with the logger cleared afterwards */
	FString Open(FN2CMcpProtocol& Core)
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, N2CMcpRequestTraceSpec::Request(1, TEXT("initialize"),
			TEXT("{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-trace-spec\",\"version\":\"1.0\"}}")));
		TestEqual(TEXT("initialize: status"), Responder->LastReply().Status, 200);
		return Responder->LastReply().SessionId;
	}

	/** The one trace line logged since the logger was cleared (empty, with a test error, otherwise) */
	FString OnlyLine()
	{
		const TArray<FString> Lines = N2CMcpRequestTraceSpec::TraceLines();
		if (!TestEqual(FString::Printf(TEXT("one trace line (%s)"), *FString::Join(Lines, TEXT(" | "))), Lines.Num(), 1))
		{
			return FString();
		}
		return Lines[0];
	}

	void ExpectContains(const FString& Line, const TCHAR* Part)
	{
		TestTrue(FString::Printf(TEXT("'%s' contains '%s'"), *Line, Part), Line.Contains(Part, ESearchCase::CaseSensitive));
	}

END_DEFINE_SPEC(FN2CMcpRequestTraceSpec)

void FN2CMcpRequestTraceSpec::Define()
{
	using namespace N2CMcpRequestTraceSpec;

	BeforeEach([this]()
	{
		SavedSeverity = FN2CLogger::Get().GetMinSeverity();
		FN2CLogger::Get().SetMinSeverity(EN2CLogSeverity::Info);
		Tool = MakeShared<FTraceTool>();
		LongTool = MakeShared<FLongTool>();
		bRegistered = FN2CMcpToolManager::Get().RegisterTool(Tool);
		bRegisteredLong = FN2CMcpToolManager::Get().RegisterTool(LongTool);
		TestTrue(TEXT("spec tools registered"), bRegistered && bRegisteredLong);
		FN2CLogger::Get().ClearErrors();
	});

	AfterEach([this]()
	{
		if (LongTool.IsValid())
		{
			LongTool->Finish();
		}
		if (bRegistered)
		{
			FN2CMcpToolManager::Get().UnregisterTool(ToolName);
		}
		if (bRegisteredLong)
		{
			FN2CMcpToolManager::Get().UnregisterTool(LongToolName);
		}
		bRegistered = bRegisteredLong = false;
		Tool.Reset();
		LongTool.Reset();
		FN2CLogger::Get().SetMinSeverity(SavedSeverity);
		FN2CLogger::Get().ClearErrors();
	});

	Describe(TEXT("Handle"), [this]()
	{
		It(TEXT("logs one Info line for initialize with its method, status 200, isError false and a duration"), [this]()
		{
			FN2CMcpProtocol Core;
			Open(Core);
			const FString Line = OnlyLine();
			ExpectContains(Line, TEXT("method=initialize "));
			ExpectContains(Line, TEXT(" status=200 isError=false duration_ms="));
			TestFalse(TEXT("no tool and no traceparent"), Line.Contains(TEXT("tool=")) || Line.Contains(TEXT("traceparent=")));
		});

		It(TEXT("logs a tools/call with the tool name, isError false and the traceparent from params._meta"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			FN2CLogger::Get().ClearErrors();
			Send(Core, Request(2, TEXT("tools/call"), FString::Printf(
				TEXT("{\"name\":\"%s\",\"arguments\":{},\"_meta\":{\"traceparent\":\"%s\"}}"), ToolName, Traceparent)), SessionId);
			const FString Line = OnlyLine();
			ExpectContains(Line, TEXT("method=tools/call tool=n2c-test-trace-tool status=200 isError=false"));
			ExpectContains(Line, *FString::Printf(TEXT(" traceparent=%s"), Traceparent));
		});

		It(TEXT("logs isError true for a tool that returns an isError result"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			FN2CLogger::Get().ClearErrors();
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Request(3, TEXT("tools/call"),
				FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{\"fail\":true}}"), ToolName)), SessionId);
			TestTrue(TEXT("the reply is an isError result"), Responder->LastReply().Body.Contains(TEXT("\"isError\":true")));
			ExpectContains(OnlyLine(), TEXT("tool=n2c-test-trace-tool status=200 isError=true"));
		});

		It(TEXT("logs isError true for a JSON-RPC error (an unknown tool) and for a refused request"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			FN2CLogger::Get().ClearErrors();
			Send(Core, Request(4, TEXT("tools/call"), TEXT("{\"name\":\"n2c-no-such-tool\",\"arguments\":{}}")), SessionId);
			ExpectContains(OnlyLine(), TEXT("method=tools/call tool=n2c-no-such-tool status=200 isError=true"));

			FN2CLogger::Get().ClearErrors();
			Send(Core, Request(5, TEXT("tools/list"), TEXT("{}")), TEXT("no-such-session"));
			ExpectContains(OnlyLine(), TEXT("method=tools/list status=404 isError=true"));
		});

		It(TEXT("marks a traceparent that is not in W3C shape as invalid instead of echoing it"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			FN2CLogger::Get().ClearErrors();
			Send(Core, Request(6, TEXT("tools/list"), TEXT("{\"_meta\":{\"traceparent\":\"not a trace\\nINFO forged line\"}}")), SessionId);
			const FString Line = OnlyLine();
			ExpectContains(Line, TEXT("method=tools/list status=200 isError=false"));
			ExpectContains(Line, TEXT(" traceparent=(invalid)"));
			TestFalse(TEXT("the client's text is not in the line"), Line.Contains(TEXT("forged")));
		});

		It(TEXT("logs a long-running tools/call when it is answered, not when Handle returns"), [this]()
		{
			FN2CMcpProtocol Core;
			const FString SessionId = Open(Core);
			FN2CLogger::Get().ClearErrors();
			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, Request(7, TEXT("tools/call"),
				FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{}}"), LongToolName)), SessionId);
			TestFalse(TEXT("nothing answered while the call runs"), Responder->IsAnswered());
			TestEqual(TEXT("no trace line while the call runs"), TraceLines().Num(), 0);
			LongTool->Finish();
			TestTrue(TEXT("answered"), Responder->IsAnswered());
			ExpectContains(OnlyLine(), TEXT("method=tools/call tool=n2c-test-trace-long status=200 isError=false"));
		});
	});

	Describe(TEXT("FN2CMcpTracingResponder"), [this]()
	{
		It(TEXT("classifies responses by their start and end only"), [this]()
		{
			TestFalse(TEXT("a success"), FN2CMcpTracingResponder::IsErrorResponse(
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}]}}")));
			TestTrue(TEXT("a JSON-RPC error"), FN2CMcpTracingResponder::IsErrorResponse(
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"Unknown tool\"}}")));
			TestTrue(TEXT("a tool error"), FN2CMcpTracingResponder::IsErrorResponse(
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[],\"isError\":true}}")));
			TestFalse(TEXT("a result whose text mentions an error"), FN2CMcpTracingResponder::IsErrorResponse(
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"error\\\":1,\\\"isError\\\":true}\"}]}}")));
			TestFalse(TEXT("a result holding an object with an error member"), FN2CMcpTracingResponder::IsErrorResponse(
				TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"structuredContent\":{\"error\":\"none\"},\"content\":[]}}")));
			TestFalse(TEXT("a notification"), FN2CMcpTracingResponder::IsErrorResponse(
				TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{\"progress\":0.5}}")));
			TestFalse(TEXT("no body"), FN2CMcpTracingResponder::IsErrorResponse(FString()));
		});

		It(TEXT("accepts only a W3C traceparent"), [this]()
		{
			TestEqual(TEXT("valid"), FN2CMcpTracingResponder::ValidTraceparent(Traceparent), FString(Traceparent));
			TestTrue(TEXT("upper case"), FN2CMcpTracingResponder::ValidTraceparent(FString(Traceparent).ToUpper()).IsEmpty());
			TestTrue(TEXT("too short"), FN2CMcpTracingResponder::ValidTraceparent(TEXT("00-abc-def-01")).IsEmpty());
			TestTrue(TEXT("not hex"), FN2CMcpTracingResponder::ValidTraceparent(
				TEXT("00-4bf92f3577b34da6a3ce929d0e0e473z-00f067aa0ba902b7-01")).IsEmpty());
		});

		It(TEXT("logs a line for a request dropped unanswered"), [this]()
		{
			{
				N2CTest::FN2CMcpMemoryResponder* Raw = new N2CTest::FN2CMcpMemoryResponder(false);
				const TSharedRef<IN2CMcpResponder> Inner = MakeShareable(Raw);
				FN2CMcpTracingResponder Tracer(N2CTest::MakeMcpInbound(TEXT("{}")), Inner);
				Tracer.Describe(nullptr);
			}
			ExpectContains(OnlyLine(), TEXT("method=(invalid-json) status=unanswered isError=true"));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
