// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Python/N2CMcpPythonRunner.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpPythonRunner
{
	static FRun Run(const TCHAR* Entry, const FString& Text)
	{
		check(IsInGameThread());
		FRun Out;
		IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
		if (!PythonPlugin)
		{
			Out.Error = TEXT("PythonScriptPlugin module not available. Ensure it is enabled in your project settings.");
			return Out;
		}
		if (!PythonPlugin->IsPythonAvailable())
		{
			Out.Error = TEXT("Python is not available. Check Python plugin configuration in project settings.");
			return Out;
		}

		// Base64 of the UTF-8 text: no quoting, no line breaks, no '.py' the engine could take for a file name
		const FTCHARToUTF8 Utf8(*Text);
		const FString Encoded = FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		const FString CallId = FGuid::NewGuid().ToString(EGuidFormats::Digits);

		FPythonCommandEx Command;
		Command.Command = FString::Printf(TEXT("__import__('nodetocode._mcp_runner', fromlist=['%s']).%s('%s', '%s')"),
			Entry, Entry, *CallId, *Encoded);
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		Command.Flags = EPythonCommandFlags::Unattended;
		const bool bRan = PythonPlugin->ExecPythonCommandEx(Command);

		FString AnswerJson;
		if (!UN2CPythonBridge::TakeMcpResult(CallId, AnswerJson))
		{
			FString Errors;
			for (const FPythonLogOutputEntry& LogEntry : Command.LogOutput)
			{
				if (LogEntry.Type == EPythonLogOutputType::Error)
				{
					Errors += LogEntry.Output + TEXT("\n");
				}
			}
			Out.Error = FString::Printf(TEXT("The Python runner gave no answer (%s): %s"), bRan ? TEXT("the command ran") : TEXT("the command failed"),
				*(!Command.CommandResult.IsEmpty() ? Command.CommandResult : Errors.TrimStartAndEnd()));
			return Out;
		}

		TSharedPtr<FJsonObject> Answer;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AnswerJson);
		if (!FJsonSerializer::Deserialize(Reader, Answer) || !Answer.IsValid())
		{
			Out.Error = FString::Printf(TEXT("The Python runner's answer is not a JSON object: %s"), *AnswerJson.Left(200));
			return Out;
		}
		Out.Answer = Answer;
		return Out;
	}

	FRun RunCode(const FString& Source)
	{
		return Run(TEXT("run_code"), Source);
	}

	FRun RunCall(const FString& NodeToCodeCall)
	{
		return Run(TEXT("run_call"), TEXT("n2c.") + NodeToCodeCall);
	}
}
