// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Verify/N2CCodeVerifiers.h"

#include "Dom/JsonObject.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

namespace N2CPythonSyntaxVerifierPrivate
{
	/** Printed before the result line, so the result is found among anything else the run logs */
	const TCHAR* const ResultMarker = TEXT("__N2C_VERIFY_PY__");

	/**
	 * Compiles each item {"index", "file", "source"} (the list arrives base64-encoded UTF-8 JSON, so no code is ever
	 * pasted into the script) and prints one marked JSON list of {"index", "line", "column", "message"}, one per item
	 * that does not parse. The code is dedented first (a model may indent a whole function); the column is given back in
	 * the original text.
	 */
	const TCHAR* const Script =
		TEXT("import base64, json, textwrap\n")
		TEXT("__n2c_results = []\n")
		TEXT("for __n2c_item in json.loads(base64.b64decode('{{N2C_DATA}}').decode('utf-8')):\n")
		TEXT("    __n2c_source = __n2c_item['source']\n")
		TEXT("    __n2c_dedented = textwrap.dedent(__n2c_source)\n")
		TEXT("    try:\n")
		TEXT("        compile(__n2c_dedented, __n2c_item['file'], 'exec', dont_inherit=True)\n")
		TEXT("    except SyntaxError as __n2c_error:\n")
		TEXT("        __n2c_line = __n2c_error.lineno or 0\n")
		TEXT("        __n2c_column = __n2c_error.offset or 0\n")
		TEXT("        __n2c_original = __n2c_source.splitlines()\n")
		TEXT("        __n2c_new = __n2c_dedented.splitlines()\n")
		TEXT("        if __n2c_column and 0 < __n2c_line <= min(len(__n2c_original), len(__n2c_new)):\n")
		TEXT("            __n2c_column += len(__n2c_original[__n2c_line - 1]) - len(__n2c_new[__n2c_line - 1])\n")
		TEXT("        __n2c_results.append({'index': __n2c_item['index'], 'line': __n2c_line, 'column': __n2c_column, 'message': __n2c_error.msg or 'invalid syntax'})\n")
		TEXT("print('{{N2C_MARKER}}' + json.dumps(__n2c_results))\n");

	struct FItem
	{
		FString Graph;
		FString File;
		FString Source;
	};
}

FN2CVerifyReport FN2CPythonSyntaxVerifier::Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
	EN2CCodeLanguage Language) const
{
	using namespace N2CPythonSyntaxVerifierPrivate;
	check(IsInGameThread());

	FN2CVerifyReport Report;
	Report.Verifier = GetName();
	if (!SupportsLanguage(Language))
	{
		return Report;
	}

	// Each non-empty code of each graph is one item (Python translations put everything in the implementation)
	TArray<FItem> Items;
	for (const FN2CGraphTranslation& Graph : Translation.Graphs)
	{
		if (!Graph.Code.GraphDeclaration.TrimStartAndEnd().IsEmpty())
		{
			Items.Add({ Graph.GraphName, TEXT("declaration"), Graph.Code.GraphDeclaration });
		}
		if (!Graph.Code.GraphImplementation.TrimStartAndEnd().IsEmpty())
		{
			Items.Add({ Graph.GraphName, TEXT("implementation"), Graph.Code.GraphImplementation });
		}
	}
	if (Items.Num() == 0)
	{
		Report.bRan = true;
		return Report;
	}

	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (!Python || !Python->IsPythonAvailable())
	{
		FN2CVerifyDiagnostic& Unavailable = Report.Diagnostics.AddDefaulted_GetRef();
		Unavailable.Severity = EN2CDiagnosticSeverity::Warning;
		Unavailable.Code = N2CVerify::VerifierUnavailable;
		Unavailable.Message = TEXT("Python is not available in this editor (PythonScriptPlugin), so the Python code was not parsed");
		return Report;
	}

	TArray<TSharedPtr<FJsonValue>> Input;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("index"), Index);
		Item->SetStringField(TEXT("file"), FString::Printf(TEXT("%s.%s"), *Items[Index].Graph, *Items[Index].File));
		Item->SetStringField(TEXT("source"), Items[Index].Source);
		Input.Add(MakeShared<FJsonValueObject>(Item));
	}
	FString InputJson;
	FJsonSerializer::Serialize(Input, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&InputJson));
	const FTCHARToUTF8 Utf8(*InputJson);
	const FString Encoded = FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

	FPythonCommandEx Command;
	Command.Command = FString(Script).Replace(TEXT("{{N2C_DATA}}"), *Encoded).Replace(TEXT("{{N2C_MARKER}}"), ResultMarker);
	Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	Command.FileExecutionScope = EPythonFileExecutionScope::Private;
	Command.Flags = EPythonCommandFlags::Unattended;
	Python->ExecPythonCommandEx(Command);

	// The marked result line
	TArray<TSharedPtr<FJsonValue>> Results;
	bool bFound = false;
	for (const FPythonLogOutputEntry& Entry : Command.LogOutput)
	{
		const int32 At = Entry.Output.Find(ResultMarker, ESearchCase::CaseSensitive);
		if (At == INDEX_NONE)
		{
			continue;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Entry.Output.Mid(At + FCString::Strlen(ResultMarker)));
		bFound = FJsonSerializer::Deserialize(Reader, Results);
		break;
	}
	if (!bFound)
	{
		FN2CVerifyDiagnostic& Failed = Report.Diagnostics.AddDefaulted_GetRef();
		Failed.Severity = EN2CDiagnosticSeverity::Warning;
		Failed.Code = N2CVerify::VerifierUnavailable;
		Failed.Message = FString::Printf(TEXT("The Python syntax check did not report a result: %s"), *Command.CommandResult);
		FN2CLogger::Get().LogWarning(Failed.Message);
		return Report;
	}

	Report.bRan = true;
	for (const TSharedPtr<FJsonValue>& Value : Results)
	{
		const TSharedPtr<FJsonObject> Result = Value.IsValid() ? Value->AsObject() : nullptr;
		int32 Index = INDEX_NONE;
		if (!Result.IsValid() || !Result->TryGetNumberField(TEXT("index"), Index) || !Items.IsValidIndex(Index))
		{
			continue;
		}
		FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Graph = Items[Index].Graph;
		Diagnostic.File = Items[Index].File;
		Result->TryGetNumberField(TEXT("line"), Diagnostic.Line);
		Result->TryGetNumberField(TEXT("column"), Diagnostic.Column);
		Diagnostic.Severity = EN2CDiagnosticSeverity::Error;
		Diagnostic.Code = N2CVerify::SyntaxError;
		FString Message;
		Result->TryGetStringField(TEXT("message"), Message);
		Diagnostic.Message = FString::Printf(TEXT("%s, line %d, column %d: %s"), *Diagnostic.Graph, Diagnostic.Line, Diagnostic.Column, *Message);
	}
	return Report;
}
