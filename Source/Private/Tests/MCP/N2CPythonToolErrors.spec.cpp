// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "IPythonScriptPlugin.h"

/**
 * run-python reports a failed script as a tool execution failure (CallToolResult isError: true, MCP 2025-11-25) and
 * keeps the JSON body; a success has no isError. (The script-library tools this spec also covered were retired with
 * decision 0010, card 0305.) Calls the tool's Execute directly on the game thread (the per-user Allow MCP Python Execution setting gates only
 * registration over MCP). Review ids: qw-20, mcp-tools-python-translation-resources-10, -11 (card 0119).
 */
BEGIN_DEFINE_SPEC(FN2CPythonToolErrorsSpec, "NodeToCode.MCP.PythonToolErrors", N2C_TEST_FLAGS)

	/** Runs a tool with string arguments and parses its text as JSON; false (with a test error) when it is not JSON */
	bool CallJson(const FString& Tool, const TMap<FString, FString>& Args, FMcpToolCallResult& OutResult,
		TSharedPtr<FJsonObject>& OutBody)
	{
		OutResult = N2CTest::CallTool(Tool, Args);
		const FString Text = N2CTest::ResultText(OutResult);
		OutBody = N2CTest::ParseJsonObject(Text);
		return TestTrue(FString::Printf(TEXT("%s answers with a JSON object (got: %s)"), *Tool, *Text.Left(300)), OutBody.IsValid());
	}

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

END_DEFINE_SPEC(FN2CPythonToolErrorsSpec)

void FN2CPythonToolErrorsSpec::Define()
{
	Describe(TEXT("run-python"), [this]()
	{
		It(TEXT("reports a raised exception with isError and keeps success false, the error and the traceback"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			if (!CallJson(TEXT("run-python"), { { TEXT("code"), TEXT("raise ValueError('boom')") } }, Result, Body)) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			TestEqual(TEXT("error"), Body->GetStringField(TEXT("error")), FString(TEXT("boom")));
			TestTrue(TEXT("traceback names the exception"), Body->GetStringField(TEXT("traceback")).Contains(TEXT("ValueError")));
		});

		It(TEXT("reports a syntax error with isError"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			// nodetocode._mcp_runner compiles the code itself (card 0299): the SyntaxError is the answer's error and
			// traceback, and nothing is logged on LogPython
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			if (!CallJson(TEXT("run-python"), { { TEXT("code"), TEXT("def broken(:\n    pass") } }, Result, Body)) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			TestTrue(TEXT("error names the syntax error"), Body->GetStringField(TEXT("error")).Contains(TEXT("SyntaxError")));
			FString Traceback;
			TestTrue(TEXT("the traceback names the code's line 1"), Body->TryGetStringField(TEXT("traceback"), Traceback)
				&& Traceback.Contains(TEXT("<run-python>")) && Traceback.Contains(TEXT("line 1")));
		});

		It(TEXT("reports a script stopped at its timeout with isError and timed_out (card 0300)"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("code"), TEXT("while True:\n    pass"));
			Args->SetNumberField(TEXT("timeout"), 1.0);
			const FMcpToolCallResult Result = N2CTest::RunToolUnmarked(TEXT("run-python"), Args);
			const TSharedPtr<FJsonObject> Body = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
			if (!TestTrue(TEXT("run-python answers with a JSON object"), Body.IsValid())) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			bool bTimedOut = false;
			TestTrue(TEXT("timed_out"), Body->TryGetBoolField(TEXT("timed_out"), bTimedOut) && bTimedOut);
			TestTrue(TEXT("error says timed out"), Body->GetStringField(TEXT("error")).StartsWith(TEXT("timed out")));
		});

		It(TEXT("returns a success without isError and with the result as data"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			if (!CallJson(TEXT("run-python"), { { TEXT("code"), TEXT("result = {'a': 1}") } }, Result, Body)) { return; }
			TestFalse(TEXT("isError"), Result.bIsError);
			TestTrue(TEXT("success"), Body->GetBoolField(TEXT("success")));
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (TestTrue(TEXT("data is an object"), Body->TryGetObjectField(TEXT("data"), Data)))
			{
				TestEqual(TEXT("data.a"), (*Data)->GetIntegerField(TEXT("a")), 1);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
