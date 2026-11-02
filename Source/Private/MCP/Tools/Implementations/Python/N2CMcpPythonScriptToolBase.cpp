// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpPythonScriptToolBase.h"
#include "Utils/N2CLogger.h"
#include "MCP/Python/N2CMcpPythonRunner.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

FMcpToolCallResult FN2CMcpPythonScriptToolBase::ExecuteNodeToCodeFunction(const FString& FunctionCall)
{
	// The call runs through nodetocode._mcp_runner.run_call (card 0299): evaluated with n2c bound, unattended, in no
	// shared namespace, and answered as strict JSON. The nodetocode script functions already return {success, data,
	// error}: that dict is the answer as it is, so its success:false reaches the client; anything else is the data of
	// a success.
	return ExecuteOnGameThread([FunctionCall]() -> FMcpToolCallResult
	{
		const N2CMcpPythonRunner::FRun Run = N2CMcpPythonRunner::RunCall(FunctionCall);
		if (!Run.Answer.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Python execution failed: %s"), *Run.Error));
		}
		const TSharedPtr<FJsonObject> ResultJson = Run.Answer;

		// The runner's bookkeeping fields only when they say something
		for (const TCHAR* Field : { TEXT("traceback"), TEXT("output"), TEXT("stderr") })
		{
			FString Text;
			const TSharedPtr<FJsonValue> Value = ResultJson->TryGetField(Field);
			if (Value.IsValid() && (Value->IsNull() || (Value->TryGetString(Text) && Text.TrimStartAndEnd().IsEmpty())))
			{
				ResultJson->RemoveField(Field);
			}
		}

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
