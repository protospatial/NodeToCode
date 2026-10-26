// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRunPythonTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpRunPythonTool)

FMcpToolDefinition FN2CMcpRunPythonTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("run-python"),
		TEXT("Execute Python code in Unreal Engine's Python environment. "
			 "WORKFLOW: 1) Search existing scripts first (search-python-scripts), "
			 "2) Use Context7 MCP to look up UE Python APIs (resolve-library-id with 'unreal-python-stubhub', then query-docs) - never guess signatures, "
			 "3) Import saved scripts: 'from scripts.<category>.<name> import func', "
			 "4) Use 'import nodetocode as n2c' for Blueprint utilities (get_focused_blueprint, compile_blueprint, save_blueprint, tag_graph), "
			 "5) Set 'result' variable to return structured data."),
		N2CMcpToolsets::Python
	);

	// Build input schema with code (required) and timeout (optional)
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// code property - required string
	TSharedPtr<FJsonObject> CodeProp = MakeShareable(new FJsonObject);
	CodeProp->SetStringField(TEXT("type"), TEXT("string"));
	CodeProp->SetStringField(TEXT("description"),
		TEXT("Python code to execute. Use 'import unreal' for UE APIs (verify signatures with Context7). "
			 "Use 'import nodetocode as n2c' for Blueprint utilities. "
			 "Import saved scripts: 'from scripts.<category>.<script_name> import function_name'. "
			 "Set 'result = {...}' to return structured data."));
	Properties->SetObjectField(TEXT("code"), CodeProp);

	// timeout property - optional number
	TSharedPtr<FJsonObject> TimeoutProp = MakeShareable(new FJsonObject);
	TimeoutProp->SetStringField(TEXT("type"), TEXT("number"));
	TimeoutProp->SetStringField(TEXT("description"),
		TEXT("Timeout in seconds (default: 60, max: 300)"));
	Properties->SetObjectField(TEXT("timeout"), TimeoutProp);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("code"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);

	Definition.InputSchema = Schema;

	return Definition;
}

FMcpToolCallResult FN2CMcpRunPythonTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Parse arguments
	FN2CMcpArgumentParser Parser(Arguments);

	FString Code;
	FString ErrorMsg;
	if (!Parser.TryGetRequiredString(TEXT("code"), Code, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}

	float TimeoutSeconds = static_cast<float>(
		Parser.GetOptionalNumber(TEXT("timeout"), DefaultTimeoutSeconds));
	TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 1.0f, MaxTimeoutSeconds);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Executing run-python tool with timeout: %.1fs, code length: %d"),
			TimeoutSeconds, Code.Len()),
		EN2CLogSeverity::Debug
	);

	// Execute on Game Thread with extended timeout
	return ExecuteOnGameThread([this, Code, TimeoutSeconds]() -> FMcpToolCallResult
	{
		// Check if Python is available
		IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
		if (!PythonPlugin)
		{
			return FMcpToolCallResult::CreateErrorResult(
				TEXT("PythonScriptPlugin module not available. Ensure it is enabled in your project settings."));
		}

		if (!PythonPlugin->IsPythonAvailable())
		{
			return FMcpToolCallResult::CreateErrorResult(
				TEXT("Python is not available. Check Python plugin configuration in project settings."));
		}

		// Wrap the user script with result capture
		FString WrappedScript = WrapScriptWithResultCapture(Code);

		// Execute using ExecPythonCommandEx for captured output
		FPythonCommandEx PythonCommand;
		PythonCommand.Command = WrappedScript;
		PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
		PythonCommand.Flags = EPythonCommandFlags::None;

		bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

		// Collect stdout from log output
		FString StdoutContent;
		FString StderrContent;
		for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
		{
			if (Entry.Type == EPythonLogOutputType::Info)
			{
				StdoutContent += Entry.Output + TEXT("\n");
			}
			else if (Entry.Type == EPythonLogOutputType::Error ||
					 Entry.Type == EPythonLogOutputType::Warning)
			{
				StderrContent += Entry.Output + TEXT("\n");
			}
		}

		// Build response JSON
		TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);

		// Try to extract the __n2c_result__ JSON from output
		TSharedPtr<FJsonObject> ResultData;
		bool bHasStructuredResult = ExtractResultFromOutput(StdoutContent, ResultData);

		if (bHasStructuredResult && ResultData.IsValid())
		{
			// Use the structured result
			bool bScriptSuccess = false;
			ResultData->TryGetBoolField(TEXT("success"), bScriptSuccess);

			ResponseJson->SetBoolField(TEXT("success"), bScriptSuccess);

			if (ResultData->HasField(TEXT("data")))
			{
				ResponseJson->SetField(TEXT("data"), ResultData->TryGetField(TEXT("data")));
			}

			if (ResultData->HasField(TEXT("error")))
			{
				FString ScriptError;
				ResultData->TryGetStringField(TEXT("error"), ScriptError);
				if (!ScriptError.IsEmpty())
				{
					ResponseJson->SetStringField(TEXT("error"), ScriptError);
				}
			}

			if (ResultData->HasField(TEXT("traceback")))
			{
				FString Traceback;
				ResultData->TryGetStringField(TEXT("traceback"), Traceback);
				if (!Traceback.IsEmpty())
				{
					ResponseJson->SetStringField(TEXT("traceback"), Traceback);
				}
			}
		}
		else
		{
			// No structured result - use execution status
			ResponseJson->SetBoolField(TEXT("success"), bSuccess);

			if (!bSuccess && !PythonCommand.CommandResult.IsEmpty())
			{
				ResponseJson->SetStringField(TEXT("error"), PythonCommand.CommandResult);
			}
		}

		// Clean stdout - remove the __n2c_result__ JSON line for cleaner output
		FString CleanOutput = StdoutContent;
		int32 JsonMarkerStart = CleanOutput.Find(TEXT("{\"__n2c_marker__\":"));
		if (JsonMarkerStart != INDEX_NONE)
		{
			int32 JsonMarkerEnd = CleanOutput.Find(TEXT("\n"), ESearchCase::IgnoreCase,
				ESearchDir::FromStart, JsonMarkerStart);
			if (JsonMarkerEnd != INDEX_NONE)
			{
				CleanOutput = CleanOutput.Left(JsonMarkerStart) +
					CleanOutput.Mid(JsonMarkerEnd + 1);
			}
			else
			{
				CleanOutput = CleanOutput.Left(JsonMarkerStart);
			}
		}
		CleanOutput.TrimStartAndEndInline();

		// Include cleaned output if present
		if (!CleanOutput.IsEmpty())
		{
			ResponseJson->SetStringField(TEXT("output"), CleanOutput);
		}

		// Include stderr if present
		if (!StderrContent.IsEmpty())
		{
			StderrContent.TrimStartAndEndInline();
			if (!StderrContent.IsEmpty())
			{
				ResponseJson->SetStringField(TEXT("stderr"), StderrContent);
			}
		}

		// Add hint about ue5_compat.py when there's an error
		bool bResponseSuccess = false;
		ResponseJson->TryGetBoolField(TEXT("success"), bResponseSuccess);
		if (!bResponseSuccess)
		{
			ResponseJson->SetStringField(TEXT("hint"),
				TEXT("If this error relates to UE5 Python API limitations (e.g., missing methods, protected properties), ")
				TEXT("check 'from scripts.core.ue5_compat import ...' for documented workarounds."));
		}

		// Serialize response as compact JSON (no pretty printing)
		FString OutputString;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);

		FN2CLogger::Get().Log(
			FString::Printf(TEXT("run-python completed. Success: %s"),
				bResponseSuccess ? TEXT("true") : TEXT("false")),
			EN2CLogSeverity::Info
		);

		// A script that raised, failed to compile or did not run is a tool execution failure: report it with isError
		// so clients that branch on it see the failure, and keep the JSON body (success:false, error, traceback, output).
		return FMcpToolCallResult::CreateTextResult(OutputString, !bResponseSuccess);
	}, TimeoutSeconds + 5.0f); // Add buffer to Game Thread timeout
}

FString FN2CMcpRunPythonTool::WrapScriptWithResultCapture(const FString& UserScript) const
{
	// Indent user script by 4 spaces for proper Python indentation inside try block
	FString IndentedScript;
	TArray<FString> Lines;
	UserScript.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		IndentedScript += TEXT("    ") + Line + TEXT("\n");
	}

	// Wrapper that captures the 'result' variable and any exceptions
	// Uses a unique marker to identify our JSON output in stdout
	return FString::Printf(TEXT(
		"import json\n"
		"import traceback\n"
		"\n"
		"__n2c_result__ = {\"__n2c_marker__\": True, \"success\": False, \"error\": None, \"data\": None}\n"
		"\n"
		"try:\n"
		"%s"
		"    # Check if user set a result variable\n"
		"    if 'result' in dir():\n"
		"        __n2c_result__[\"data\"] = result\n"
		"    __n2c_result__[\"success\"] = True\n"
		"except Exception as __n2c_e__:\n"
		"    __n2c_result__[\"error\"] = str(__n2c_e__)\n"
		"    __n2c_result__[\"traceback\"] = traceback.format_exc()\n"
		"finally:\n"
		"    # Print the result JSON with marker for extraction\n"
		"    print(json.dumps(__n2c_result__))\n"
	), *IndentedScript);
}

bool FN2CMcpRunPythonTool::ExtractResultFromOutput(const FString& CapturedOutput,
	TSharedPtr<FJsonObject>& OutResultJson) const
{
	// Look for our marker in the output
	const FString Marker = TEXT("{\"__n2c_marker__\":");

	int32 StartIndex = CapturedOutput.Find(Marker);
	if (StartIndex == INDEX_NONE)
	{
		return false;
	}

	// Find the end of the JSON line
	int32 EndIndex = CapturedOutput.Find(TEXT("\n"), ESearchCase::IgnoreCase,
		ESearchDir::FromStart, StartIndex);
	if (EndIndex == INDEX_NONE)
	{
		EndIndex = CapturedOutput.Len();
	}

	FString JsonStr = CapturedOutput.Mid(StartIndex, EndIndex - StartIndex);
	JsonStr.TrimStartAndEndInline();

	// Parse the JSON
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, OutResultJson) || !OutResultJson.IsValid())
	{
		FN2CLogger::Get().LogWarning(
			FString::Printf(TEXT("Failed to parse result JSON: %s"), *JsonStr));
		return false;
	}

	return true;
}
