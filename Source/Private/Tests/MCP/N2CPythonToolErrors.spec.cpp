// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "IPythonScriptPlugin.h"

/**
 * run-python and the script-library tools report a failed script or script function as a tool execution failure
 * (CallToolResult isError: true, MCP 2025-11-25) and keep the JSON body; a success has no isError. The script tools
 * return the nodetocode function's own {success, data, error} instead of wrapping it as the data of a success.
 * Calls the tools' Execute directly on the game thread (the per-user Allow MCP Python Execution setting gates only
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
			// The Python plugin logs the compile error on LogPython, one line at a time: the message, the source line
			// and the caret under it
			AddExpectedErrorPlain(TEXT("SyntaxError"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedErrorPlain(TEXT("def broken(:"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("^(LogPython:)?\\s*\\^+\\s*$"), EAutomationExpectedErrorFlags::Contains, 0);
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			if (!CallJson(TEXT("run-python"), { { TEXT("code"), TEXT("def broken(:\n    pass") } }, Result, Body)) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			TestTrue(TEXT("error names the syntax error"), Body->GetStringField(TEXT("error")).Contains(TEXT("SyntaxError")));
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

	Describe(TEXT("the script-library tools"), [this]()
	{
		It(TEXT("report a missing script with isError and a top-level success false"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			if (!CallJson(TEXT("get-python-script"), { { TEXT("name"), TEXT("n2c_test_missing_script_0119") } }, Result, Body)) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			TestTrue(TEXT("error says not found"), Body->GetStringField(TEXT("error")).Contains(TEXT("not found")));
		});

		It(TEXT("report a refused save with isError and write nothing"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			// A blank description is refused by save_script before anything is written
			if (!CallJson(TEXT("save-python-script"), {
				{ TEXT("name"), TEXT("n2c_test_refused_0119") }, { TEXT("code"), TEXT("x = 1") }, { TEXT("description"), TEXT("   ") } },
				Result, Body)) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			TestTrue(TEXT("error names the description"), Body->GetStringField(TEXT("error")).Contains(TEXT("description")));
		});

		It(TEXT("return a list without isError and without nesting a second success in data"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			TSharedPtr<FJsonObject> Body;
			if (!CallJson(TEXT("list-python-scripts"), {}, Result, Body)) { return; }
			TestFalse(TEXT("isError"), Result.bIsError);
			TestTrue(TEXT("success"), Body->GetBoolField(TEXT("success")));
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (TestTrue(TEXT("data is an object"), Body->TryGetObjectField(TEXT("data"), Data)))
			{
				TestFalse(TEXT("data has no nested success"), (*Data)->HasField(TEXT("success")));
				TestTrue(TEXT("data.scripts is present"), (*Data)->HasTypedField<EJson::Array>(TEXT("scripts")));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
