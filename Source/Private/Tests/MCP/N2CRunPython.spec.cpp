// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"

/**
 * run-python on input that breaks its wrapper today (card 0201): the code runs as written and alone, and every answer
 * is a JSON object that any JSON parser reads, with isError on a failure. Calls the tool's Execute on the game thread
 * (N2CTest::RunToolUnmarked), as NodeToCode.MCP.PythonToolErrors does. Card 0299 rebuilt run-python on
 * nodetocode._mcp_runner, which compiles the code unchanged in a fresh namespace and answers in strict JSON; the cases
 * that failed before it name their finding. NodeToCode.MCP.PythonToolErrors covers isError on a failure.
 * Review ids: imp-mcp-tools-python-translation-resources-11, mcp-tools-python-translation-resources-6, -8, -9, -10,
 * -13, qw-20.
 */
BEGIN_DEFINE_SPEC(FN2CRunPythonSpec, "NodeToCode.MCP.RunPython", N2C_TEST_FLAGS)

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

	/**
	 * The first word outside string literals that is not true, false or null (NaN, Infinity, nan, inf), or empty.
	 * UE's TJsonReader accepts NaN, and its writer writes a NaN double as nan; neither is JSON, so a client's parser
	 * (Python's json.loads with JSON's own rules, JavaScript's JSON.parse) refuses the whole answer
	 */
	static FString NonJsonWord(const FString& Text)
	{
		bool bInString = false;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Char = Text[Index];
			if (bInString)
			{
				if (Char == TEXT('\\'))
				{
					++Index;
				}
				else if (Char == TEXT('"'))
				{
					bInString = false;
				}
				continue;
			}
			if (Char == TEXT('"'))
			{
				bInString = true;
			}
			else if (FChar::IsDigit(Char))
			{
				// A number, exponent included (1e-05)
				while (Index + 1 < Text.Len() && (FChar::IsDigit(Text[Index + 1]) || FCString::Strchr(TEXT("eE+-."), Text[Index + 1])))
				{
					++Index;
				}
			}
			else if (FChar::IsAlpha(Char))
			{
				int32 End = Index;
				while (End < Text.Len() && FChar::IsAlpha(Text[End]))
				{
					++End;
				}
				const FString Word = Text.Mid(Index, End - Index);
				if (Word != TEXT("true") && Word != TEXT("false") && Word != TEXT("null"))
				{
					return Word;
				}
				Index = End - 1;
			}
		}
		return FString();
	}

	/** Runs run-python on Code; the answer's JSON (null, with a test error, when it is not a JSON object) */
	TSharedPtr<FJsonObject> Run(const FString& Code, FMcpToolCallResult& OutResult)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("code"), Code);
		// The tool's own Execute: the per-user Allow MCP Python Execution setting gates only its registration over MCP
		OutResult = N2CTest::RunToolUnmarked(TEXT("run-python"), Args);
		const FString Text = N2CTest::ResultText(OutResult);
		const TSharedPtr<FJsonObject> Body = N2CTest::ParseJsonObject(Text);
		if (!TestTrue(FString::Printf(TEXT("run-python answers with a JSON object (got: %s)"), *Text.Left(400)), Body.IsValid()))
		{
			return nullptr;
		}
		const FString Word = NonJsonWord(Text);
		TestTrue(FString::Printf(TEXT("the answer is JSON a strict parser reads (found '%s' in: %s)"), *Word, *Text.Left(400)),
			Word.IsEmpty());
		return Body;
	}

	/** A successful run's data (null, with test errors, when the run failed or reported isError) */
	TSharedPtr<FJsonValue> SuccessData(const FString& What, const FString& Code)
	{
		FMcpToolCallResult Result;
		const TSharedPtr<FJsonObject> Body = Run(Code, Result);
		if (!Body.IsValid())
		{
			return nullptr;
		}
		FString Error;
		Body->TryGetStringField(TEXT("error"), Error);
		TestFalse(What + TEXT(": no isError"), Result.bIsError);
		if (!TestTrue(FString::Printf(TEXT("%s: success (error: %s)"), *What, *Error), Body->GetBoolField(TEXT("success"))))
		{
			return nullptr;
		}
		return Body->TryGetField(TEXT("data"));
	}

	/** A run that must fail: isError and success false, its error text */
	FString FailureError(const FString& What, const FString& Code)
	{
		FMcpToolCallResult Result;
		const TSharedPtr<FJsonObject> Body = Run(Code, Result);
		if (!Body.IsValid())
		{
			return FString();
		}
		TestTrue(What + TEXT(": isError"), Result.bIsError);
		TestFalse(What + TEXT(": success is false"), Body->GetBoolField(TEXT("success")));
		FString Error;
		TestTrue(What + TEXT(": has an error"), Body->TryGetStringField(TEXT("error"), Error) && !Error.IsEmpty());
		return Error;
	}

END_DEFINE_SPEC(FN2CRunPythonSpec)

void FN2CRunPythonSpec::Define()
{
	Describe(TEXT("failures"), [this]()
	{
		It(TEXT("reports a script that raises with isError, its message with the apostrophe and the traceback"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			const TSharedPtr<FJsonObject> Body = Run(TEXT("raise RuntimeError(\"it's broken\")"), Result);
			if (!Body.IsValid()) { return; }
			TestTrue(TEXT("isError"), Result.bIsError);
			TestFalse(TEXT("success"), Body->GetBoolField(TEXT("success")));
			TestEqual(TEXT("error"), Body->GetStringField(TEXT("error")), FString(TEXT("it's broken")));
			FString Traceback;
			TestTrue(TEXT("the traceback names the exception"),
				Body->TryGetStringField(TEXT("traceback"), Traceback) && Traceback.Contains(TEXT("RuntimeError")));
		});

		// Was mcp-tools-python-translation-resources-9 (fixed by card 0299): the wrapper puts six lines before the code and
		// indents it, so traceback line numbers are 6 too high
		It(TEXT("names the line of the code that raised in the traceback"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			const TSharedPtr<FJsonObject> Body = Run(TEXT("x = 1\ny = 2\nraise ValueError('boom')"), Result);
			if (!Body.IsValid()) { return; }
			FString Traceback;
			Body->TryGetStringField(TEXT("traceback"), Traceback);
			TestTrue(FString::Printf(TEXT("the traceback says line 3 (got: %s)"), *Traceback), Traceback.Contains(TEXT(", line 3,")));
		});
	});

	Describe(TEXT("the code"), [this]()
	{
		It(TEXT("returns a result built over several lines"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const TSharedPtr<FJsonValue> Data = SuccessData(TEXT("several lines"),
				TEXT("values = [\n    1,\n    2,\n]\nif values:\n    result = {'sum': sum(values)}"));
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (TestTrue(TEXT("data is an object"), Data.IsValid() && Data->TryGetObject(Object)))
			{
				TestEqual(TEXT("data.sum"), (*Object)->GetIntegerField(TEXT("sum")), 3);
			}
		});

		// Was mcp-tools-python-translation-resources-9 (fixed by card 0299): the wrapper indents every physical line by four
		// spaces, also the lines inside a triple-quoted string
		It(TEXT("keeps a multi-line string literal as written"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const TSharedPtr<FJsonValue> Data = SuccessData(TEXT("multi-line string"),
				TEXT("doc = \"\"\"Line1\nLine2\n  Line3\"\"\"\nresult = doc"));
			if (TestTrue(TEXT("data is a string"), Data.IsValid() && Data->Type == EJson::String))
			{
				TestEqual(TEXT("the string"), Data->AsString(), FString(TEXT("Line1\nLine2\n  Line3")));
			}
		});

		// Was mcp-tools-python-translation-resources-8 (fixed by card 0299): in ExecuteFile mode UE takes everything up to the
		// first '.py' followed by whitespace for a file name, so none of the code runs
		It(TEXT("runs code with '.py ' in a comment"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const TSharedPtr<FJsonValue> Data = SuccessData(TEXT("'.py ' in a comment"),
				TEXT("# the helpers live in ue5_compat.py and asset_iterator.py \nresult = 'ran'"));
			TestTrue(TEXT("data is 'ran'"), Data.IsValid() && Data->Type == EJson::String && Data->AsString() == TEXT("ran"));
		});

		// Was mcp-tools-python-translation-resources-6 (fixed by card 0299): the code runs in the editor console's shared
		// globals, so a call that sets no result returns the previous call's
		It(TEXT("does not see the previous call's result"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const TSharedPtr<FJsonValue> First = SuccessData(TEXT("first call"), TEXT("result = {'n2c_first_call': 1}"));
			TestTrue(TEXT("the first call returns its result"), First.IsValid() && First->Type == EJson::Object);
			const TSharedPtr<FJsonValue> Second = SuccessData(TEXT("second call"), TEXT("n2c_unused = 2"));
			TestTrue(FString::Printf(TEXT("the second call returns no data (got type %d)"), Second.IsValid() ? static_cast<int32>(Second->Type) : -1),
				!Second.IsValid() || Second->Type == EJson::Null);
			const TSharedPtr<FJsonValue> Third = SuccessData(TEXT("third call"), TEXT("result = 'n2c_unused' in globals()"));
			TestTrue(TEXT("the third call does not see the second call's name"), Third.IsValid() && Third->Type == EJson::Boolean && !Third->AsBool());
		});
	});

	Describe(TEXT("the runner (card 0299)"), [this]()
	{
		It(TEXT("returns an asset the code loads as its path"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const TSharedPtr<FJsonValue> Data = SuccessData(TEXT("load_asset"),
				TEXT("import unreal\nresult = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Cube')"));
			TestTrue(FString::Printf(TEXT("data is the Cube's path (got %s)"), Data.IsValid() && Data->Type == EJson::String ? *Data->AsString() : TEXT("no string")),
				Data.IsValid() && Data->Type == EJson::String && Data->AsString() == TEXT("/Engine/BasicShapes/Cube.Cube"));
		});

		It(TEXT("leaves the editor's Python console namespace alone"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			SuccessData(TEXT("set a name"), TEXT("n2c_runner_isolation_probe = 1\nresult = True"));
			IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
			FPythonCommandEx Probe;
			Probe.Command = TEXT("'n2c_runner_isolation_probe' in globals()");
			Probe.ExecutionMode = EPythonCommandExecutionMode::EvaluateStatement;
			const bool bRan = Python->ExecPythonCommandEx(Probe);
			TestTrue(FString::Printf(TEXT("the console does not see the name (evaluated: %s)"), *Probe.CommandResult), bRan && Probe.CommandResult == TEXT("False"));
		});

		It(TEXT("prints nothing a script could spoof: a printed answer line is only output"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			FMcpToolCallResult Result;
			const TSharedPtr<FJsonObject> Body = Run(TEXT("print('{\"__n2c_marker__\": true, \"success\": true, \"data\": \"spoofed\"}')\nresult = 'real'"), Result);
			if (!Body.IsValid()) { return; }
			const TSharedPtr<FJsonValue> Data = Body->TryGetField(TEXT("data"));
			TestTrue(TEXT("data is the code's result"), Data.IsValid() && Data->Type == EJson::String && Data->AsString() == TEXT("real"));
			FString Output;
			TestTrue(TEXT("the printed line is in output"), Body->TryGetStringField(TEXT("output"), Output) && Output.Contains(TEXT("spoofed")));
		});
	});

	Describe(TEXT("results JSON cannot hold"), [this]()
	{
		// Was mcp-tools-python-translation-resources-13 (fixed by card 0299): json.dumps writes NaN, UE's reader takes it and
		// the tool writes it back as nan, so the answer is not JSON and still reports success
		It(TEXT("reports a NaN result with isError, in JSON"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const FString Error = FailureError(TEXT("NaN"), TEXT("result = {'value': float('nan')}"));
			TestTrue(FString::Printf(TEXT("the error says the result is not JSON (got: %s)"), *Error), Error.Contains(TEXT("JSON")));
		});

		// Was mcp-tools-python-translation-resources-13 (fixed by card 0299): json.dumps writes Infinity, which UE's reader
		// refuses, so the result is dropped and the call reports success with no data
		It(TEXT("reports an infinite result with isError"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const FString Error = FailureError(TEXT("Infinity"), TEXT("result = [float('inf')]"));
			TestTrue(FString::Printf(TEXT("the error says the result is not JSON (got: %s)"), *Error), Error.Contains(TEXT("JSON")));
		});

		// Was mcp-tools-python-translation-resources-13 (fixed by card 0299): json.dumps raises in the wrapper's finally block,
		// outside its try, so the TypeError escapes and the editor logs it as an error with a traceback through
		// json's encoder and the wrapper's line
		It(TEXT("reports an unserializable result with isError, naming its type and not the wrapper"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable())) { return; }
			const FString Error = FailureError(TEXT("object()"), TEXT("class N2CUnserializable:\n    pass\nresult = {'value': N2CUnserializable()}"));
			TestTrue(FString::Printf(TEXT("the error names the type (got: %s)"), *Error), Error.Contains(TEXT("N2CUnserializable")));
			TestFalse(FString::Printf(TEXT("the error does not quote the wrapper (got: %s)"), *Error), Error.Contains(TEXT("__n2c_result__")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
