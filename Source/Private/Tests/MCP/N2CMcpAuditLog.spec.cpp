// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/Implementations/Python/N2CMcpRunPythonTool.h"
#include "MCP/Utils/N2CMcpAuditLog.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace N2CMcpAuditLogSpec
{
	const TCHAR* ProbeName = TEXT("n2c-test-audit-run-python");

	/**
	 * run-python under another name: the spec's own tool manager may serve it without the per-user Allow MCP Python
	 * Execution setting, which gates only the name run-python. Execute is run-python's, audit line included.
	 */
	class FRunPythonProbe : public FN2CMcpRunPythonTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition = FN2CMcpRunPythonTool::GetDefinition();
			Definition.Name = ProbeName;
			return Definition;
		}
	};
}

/**
 * Card 0424 (epic 0001): every run-python call appends one JSON line to <Project>/Saved/NodeToCode/Logs/mcp-audit.jsonl
 * with the time, the session and the client that initialize named, the tool, the full code, success, isError,
 * timed_out, seconds and the result cut at 4000 characters. Drives run-python (as FRunPythonProbe) through
 * FN2CMcpProtocol and the memory responder, with the audit file pointed at a scratch folder; and read-file may never
 * open the file, since a script's output can carry a secret.
 */
BEGIN_DEFINE_SPEC(FN2CMcpAuditLogSpec, "NodeToCode.MCP.AuditLog", N2C_TEST_FLAGS)

	TUniquePtr<FN2CMcpToolManager> Tools;
	TUniquePtr<FN2CMcpProtocol> Core;
	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString SessionId;
	int32 NextId = 10;

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

	FString AuditPath() const
	{
		return Scratch->Path / TEXT("mcp-audit.jsonl");
	}

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(const FString& Body, const FString& InSessionId)
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
		Core->Handle(N2CTest::MakeMcpInbound(Body, TEXT("application/json, text/event-stream"), InSessionId), Responder);
		return Responder;
	}

	/** tools/call of the probe with Code through the session */
	void CallRunPython(const FString& Code)
	{
		const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("code"), Code);
		const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), N2CMcpAuditLogSpec::ProbeName);
		Params->SetObjectField(TEXT("arguments"), Arguments);
		const TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Request->SetNumberField(TEXT("id"), NextId++);
		Request->SetStringField(TEXT("method"), TEXT("tools/call"));
		Request->SetObjectField(TEXT("params"), Params);
		FString Body;
		FJsonSerializer::Serialize(Request, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Body, SessionId);
		TestTrue(TEXT("tools/call is answered"), Responder->IsAnswered());
	}

	/** Every line of the audit file, parsed (a line that is not a JSON object fails the test) */
	TArray<TSharedPtr<FJsonObject>> Lines()
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		TArray<FString> Raw;
		FFileHelper::LoadFileToStringArray(Raw, *AuditPath());
		for (const FString& Line : Raw)
		{
			if (Line.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(Line);
			TestTrue(TEXT("each line is one JSON object: ") + Line.Left(200), Parsed.IsValid());
			Out.Add(Parsed);
		}
		return Out;
	}

END_DEFINE_SPEC(FN2CMcpAuditLogSpec)

void FN2CMcpAuditLogSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CAuditLog"));
		N2CMcpAuditLog::SetFilePathForTests(AuditPath());

		Tools = MakeUnique<FN2CMcpToolManager>();
		FN2CMcpToolConfig Config;
		Config.Toolsets = FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {});
		Tools->Configure(Config);
		TestTrue(TEXT("the probe registers"), Tools->RegisterTool(MakeShared<N2CMcpAuditLogSpec::FRunPythonProbe>()));
		FN2CMcpCatalogs Catalogs = FN2CMcpCatalogs::Default();
		Catalogs.Tools = Tools.Get();
		Core = MakeUnique<FN2CMcpProtocol>(Catalogs);

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Init = Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":")
			TEXT("{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-audit-spec\",\"version\":\"2.5\"}}}"), FString());
		SessionId = Init->LastReply().SessionId;
		TestFalse(TEXT("initialize: a session"), SessionId.IsEmpty());
		Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"), SessionId);
	});

	AfterEach([this]()
	{
		N2CMcpAuditLog::SetFilePathForTests(FString());
		Core.Reset();
		Tools.Reset();
		Scratch.Reset();
	});

	It(TEXT("appends one line per run-python call with the session, the client, the full code and the outcome"), [this]()
	{
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()))
		{
			return;
		}
		const FString Code = TEXT("import os\nresult = \"it's \" + 'done'");
		CallRunPython(Code);
		CallRunPython(TEXT("raise ValueError('nope')"));
		const TArray<TSharedPtr<FJsonObject>> Read = Lines();
		if (!TestEqual(TEXT("two lines"), Read.Num(), 2))
		{
			return;
		}
		const TSharedPtr<FJsonObject>& First = Read[0];
		TestEqual(TEXT("session"), First->GetStringField(TEXT("session")), SessionId);
		const TSharedPtr<FJsonObject>* Client = nullptr;
		TestTrue(TEXT("client name and version from initialize"), First->TryGetObjectField(TEXT("client"), Client)
			&& (*Client)->GetStringField(TEXT("name")) == TEXT("n2c-audit-spec") && (*Client)->GetStringField(TEXT("version")) == TEXT("2.5"));
		TestEqual(TEXT("tool"), First->GetStringField(TEXT("tool")), FString(TEXT("run-python")));
		TestEqual(TEXT("the full code"), First->GetStringField(TEXT("code")), Code);
		TestTrue(TEXT("success"), First->GetBoolField(TEXT("success")));
		TestFalse(TEXT("isError"), First->GetBoolField(TEXT("isError")));
		TestFalse(TEXT("timed_out"), First->GetBoolField(TEXT("timed_out")));
		TestTrue(TEXT("the result: ") + First->GetStringField(TEXT("result")), First->GetStringField(TEXT("result")).Contains(TEXT("done")));
		FDateTime Time;
		TestTrue(TEXT("an ISO 8601 time"), FDateTime::ParseIso8601(*First->GetStringField(TEXT("time")), Time));
		TestTrue(TEXT("seconds"), First->HasTypedField<EJson::Number>(TEXT("seconds")));
		TestEqual(TEXT("timeout (the default)"), First->GetNumberField(TEXT("timeout")), 60.0);

		const TSharedPtr<FJsonObject>& Second = Read[1];
		TestFalse(TEXT("the failure: success false"), Second->GetBoolField(TEXT("success")));
		TestTrue(TEXT("the failure: isError"), Second->GetBoolField(TEXT("isError")));
		TestTrue(TEXT("the failure: its error in the result"), Second->GetStringField(TEXT("result")).Contains(TEXT("nope")));
	});

	It(TEXT("cuts a long result at 4000 characters and logs a call made outside any MCP session"), [this]()
	{
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()))
		{
			return;
		}
		CallRunPython(TEXT("result = 'x' * 10000"));
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("code"), TEXT("result = 1"));
		N2CTest::RunToolUnmarked(TEXT("run-python"), Args);
		const TArray<TSharedPtr<FJsonObject>> Read = Lines();
		if (!TestEqual(TEXT("two lines"), Read.Num(), 2))
		{
			return;
		}
		const FString Result = Read[0]->GetStringField(TEXT("result"));
		TestTrue(FString::Printf(TEXT("the result is cut (%d characters)"), Result.Len()),
			Result.Len() < N2CMcpAuditLog::MaxResultChars + 64 && Result.Contains(TEXT("more characters]")));
		TestEqual(TEXT("outside a session: no session"), Read[1]->GetStringField(TEXT("session")), FString());
	});

	It(TEXT("keeps read-file and read-path away from the audit file and its rotated copy"), [this]()
	{
		N2CMcpAuditLog::SetFilePathForTests(FString());
		const FString Default = N2CMcpAuditLog::FilePath();
		TestTrue(TEXT("under Saved/NodeToCode/Logs"), Default.EndsWith(TEXT("Saved/NodeToCode/Logs/mcp-audit.jsonl")));
		TestTrue(TEXT("the audit file is denied"), FN2CPathUtils::IsDeniedForAgentAccess(Default));
		TestTrue(TEXT("its rotated copy is denied"), FN2CPathUtils::IsDeniedForAgentAccess(
			FPaths::Combine(FPaths::GetPath(Default), TEXT("mcp-audit.1.jsonl"))));
		TestFalse(TEXT("another log there is not"), FN2CPathUtils::IsDeniedForAgentAccess(
			FPaths::Combine(FPaths::GetPath(Default), TEXT("other.log"))));
		N2CMcpAuditLog::SetFilePathForTests(AuditPath());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
