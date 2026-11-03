// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRunPythonTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "MCP/Python/N2CMcpPythonRunner.h"
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
			 "WORKFLOW: 1) Use a loaded Agent Skill's script when one fits the task, "
			 "2) Use Context7 MCP to look up UE Python APIs (resolve-library-id with 'unreal-python-stubhub', then query-docs) - never guess signatures, "
			 "3) Use 'import nodetocode as n2c' for Blueprint utilities (get_focused_blueprint, compile_blueprint, save_blueprint, tag_graph) "
			 "and n2c.invoke_tool(name, arguments) for any other NodeToCode tool, "
			 "4) Set 'result' variable to return structured data."),
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

	// Arbitrary code the user opted in to: it is how an agent inspects the running game and ends PIE
	// (LevelEditorSubsystem), so it keeps working during Play In Editor; the guard covers tools whose effect it knows
	// (card 0170)
	Definition.bAllowedDuringPlaySession = true;

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
	return ExecuteOnGameThread([Code]() -> FMcpToolCallResult
	{
		// The code runs unchanged in a fresh namespace through nodetocode._mcp_runner, which hands back strict JSON
		// (card 0299): no wrapper lines, no shared console globals, no marker line scraped from stdout
		const N2CMcpPythonRunner::FRun Run = N2CMcpPythonRunner::RunCode(Code);
		if (!Run.Answer.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Run.Error);
		}
		const TSharedPtr<FJsonObject>& Answer = Run.Answer;

		// The answer as before: success, data, error, traceback, output, stderr (the empty ones left out), hint
		TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);
		bool bResponseSuccess = false;
		Answer->TryGetBoolField(TEXT("success"), bResponseSuccess);
		ResponseJson->SetBoolField(TEXT("success"), bResponseSuccess);
		if (const TSharedPtr<FJsonValue> Data = Answer->TryGetField(TEXT("data")))
		{
			ResponseJson->SetField(TEXT("data"), Data);
		}
		for (const TCHAR* Field : { TEXT("error"), TEXT("traceback"), TEXT("output"), TEXT("stderr") })
		{
			FString Text;
			if (Answer->TryGetStringField(Field, Text))
			{
				Text.TrimStartAndEndInline();
				if (!Text.IsEmpty())
				{
					ResponseJson->SetStringField(Field, Text);
				}
			}
		}

		// Add hint about ue5_compat.py when there's an error
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

		// A script that raised, failed to compile or returned what JSON cannot hold is a tool execution failure: report
		// it with isError so clients that branch on it see the failure, and keep the JSON body
		return FMcpToolCallResult::CreateTextResult(OutputString, !bResponseSuccess);
	}, TimeoutSeconds + 5.0f); // Add buffer to Game Thread timeout
}
