// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpPythonScriptToolBase.h"
#include "Utils/N2CLogger.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

FMcpToolCallResult FN2CMcpPythonScriptToolBase::ExecuteNodeToCodeFunction(const FString& FunctionCall)
{
	// Build Python script that imports nodetocode and calls the function. The nodetocode script functions already
	// return {success, data, error}: that dict is emitted as it is (not wrapped as the data of another success), so its
	// success:false reaches the client. Anything else is wrapped as the data of a success.
	FString Script = FString::Printf(TEXT(
		"import json\n"
		"import nodetocode as n2c\n"
		"\n"
		"try:\n"
		"    result = n2c.%s\n"
		"    payload = {\"__n2c_marker__\": True}\n"
		"    if isinstance(result, dict) and 'success' in result:\n"
		"        payload.update(result)\n"
		"        payload[\"__n2c_marker__\"] = True\n"
		"    else:\n"
		"        payload.update({\"success\": True, \"data\": result})\n"
		"    print(json.dumps(payload))\n"
		"except Exception as e:\n"
		"    import traceback\n"
		"    print(json.dumps({\"__n2c_marker__\": True, \"success\": False, \"error\": str(e), \"traceback\": traceback.format_exc()}))\n"
	), *FunctionCall);

	// Execute on Game Thread
	return ExecuteOnGameThread([Script]() -> FMcpToolCallResult
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

		// Execute the script
		FPythonCommandEx PythonCommand;
		PythonCommand.Command = Script;
		PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
		PythonCommand.Flags = EPythonCommandFlags::None;

		bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

		// Collect output
		FString StdoutContent;
		for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
		{
			if (Entry.Type == EPythonLogOutputType::Info)
			{
				StdoutContent += Entry.Output + TEXT("\n");
			}
		}

		// Look for our marker in the output
		const FString Marker = TEXT("{\"__n2c_marker__\":");
		int32 StartIndex = StdoutContent.Find(Marker);
		if (StartIndex == INDEX_NONE)
		{
			// No structured output found
			if (!bSuccess)
			{
				return FMcpToolCallResult::CreateErrorResult(
					FString::Printf(TEXT("Python execution failed: %s"), *PythonCommand.CommandResult));
			}
			return FMcpToolCallResult::CreateTextResult(
				TEXT("{\"success\": true, \"data\": null}"));
		}

		// Find the end of the JSON line
		int32 EndIndex = StdoutContent.Find(TEXT("\n"), ESearchCase::IgnoreCase,
			ESearchDir::FromStart, StartIndex);
		if (EndIndex == INDEX_NONE)
		{
			EndIndex = StdoutContent.Len();
		}

		FString JsonStr = StdoutContent.Mid(StartIndex, EndIndex - StartIndex);
		JsonStr.TrimStartAndEndInline();

		// Parse and validate the JSON
		TSharedPtr<FJsonObject> ResultJson;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, ResultJson) || !ResultJson.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(
				FString::Printf(TEXT("Failed to parse Python result JSON: %s"), *JsonStr));
		}

		// Remove the marker field and return clean JSON
		ResultJson->RemoveField(TEXT("__n2c_marker__"));

		// success:false (the function reported a failure, or it raised) is a tool execution failure: isError, with
		// the same JSON body. A body without a boolean success counts as a failure.
		bool bFunctionSuccess = false;
		ResultJson->TryGetBoolField(TEXT("success"), bFunctionSuccess);

		FString OutputString;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString, !bFunctionSuccess);
	}, DefaultTimeoutSeconds);
}

FString FN2CMcpPythonScriptToolBase::EscapePythonString(const FString& Value)
{
	// Escape backslashes, quotes, and newlines for Python string literals
	FString Escaped = Value;
	Escaped = Escaped.Replace(TEXT("\\"), TEXT("\\\\"));
	Escaped = Escaped.Replace(TEXT("\""), TEXT("\\\""));
	Escaped = Escaped.Replace(TEXT("\n"), TEXT("\\n"));
	Escaped = Escaped.Replace(TEXT("\r"), TEXT("\\r"));
	Escaped = Escaped.Replace(TEXT("\t"), TEXT("\\t"));
	return Escaped;
}

FString FN2CMcpPythonScriptToolBase::BuildPythonList(const TArray<FString>& Values)
{
	if (Values.Num() == 0)
	{
		return TEXT("[]");
	}

	FString Result = TEXT("[");
	for (int32 i = 0; i < Values.Num(); i++)
	{
		if (i > 0)
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("\"%s\""), *EscapePythonString(Values[i]));
	}
	Result += TEXT("]");
	return Result;
}
