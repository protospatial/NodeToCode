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
#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

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
			 "4) Set 'result' variable to return structured data. "
			 "Each call is one undo step, 'MCP: run-python' (Ctrl+Z reverts it): edits through set_editor_property, editor "
			 "subsystems, asset tools and NodeToCode's tools are recorded, and so are the actors selected in the level; for "
			 "any other object a plain setter changes (set_actor_location on an unselected actor), call obj.modify() first. "
			 "A call that changes nothing adds no undo step. "
			 "The timeout is checked between Python bytecodes: a loop of Python code is stopped with a timeout error, but a "
			 "native engine call on the game thread (an asset scan, a level load, time.sleep) cannot be interrupted and "
			 "runs to its end first; the editor is busy for as long as the script runs."),
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
		TEXT("Seconds the script may run (default: 60, max: 300). Past it the script is stopped at its next Python "
			"bytecode and the call fails with timed_out true; a native engine call in progress runs to its end first."));
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
	return ExecuteOnGameThread([Code, TimeoutSeconds]() -> FMcpToolCallResult
	{
		// One undo step per call (card 0300). The editor drops a transaction whose records changed nothing
		// (FTransaction::IsTransient), so a read-only script adds none. The selected level actors and their components
		// are recorded up front: a script that moves them with a plain setter (set_actor_location), which calls no
		// Modify(), is still undone by one Ctrl+Z. The transaction closes when this lambda returns, before the tool
		// call scope compiles what the script edited, so the compile is not part of the undo step.
		FScopedTransaction Transaction(NSLOCTEXT("NodeToCode", "McpRunPython", "MCP: run-python"));
		// Recorded without marking their package dirty (Modify(false)): a script that leaves them alone dirties nothing
		TArray<TPair<TWeakObjectPtr<AActor>, FTransform>> Selected;
		if (GEditor && GUndo)
		{
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					Actor->Modify(false);
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (Component)
						{
							Component->Modify(false);
						}
					}
					Selected.Emplace(Actor, Actor->GetActorTransform());
				}
			}
		}

		// The code runs unchanged in a fresh namespace through nodetocode._mcp_runner, which hands back strict JSON
		// (card 0299): no wrapper lines, no shared console globals, no marker line scraped from stdout. Past the
		// timeout the runner raises inside the code between bytecodes (card 0300)
		const N2CMcpPythonRunner::FRun Run = N2CMcpPythonRunner::RunCode(Code, TimeoutSeconds);

		// A selected actor the script moved with a plain setter: its level is dirty now, as the editor's own move does
		for (const TPair<TWeakObjectPtr<AActor>, FTransform>& Entry : Selected)
		{
			AActor* Actor = Entry.Key.Get();
			if (Actor && !Actor->GetActorTransform().Equals(Entry.Value))
			{
				Actor->MarkPackageDirty();
			}
		}
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
		bool bTimedOut = false;
		if (Answer->TryGetBoolField(TEXT("timed_out"), bTimedOut) && bTimedOut)
		{
			ResponseJson->SetBoolField(TEXT("timed_out"), true);
		}
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

		// A hint on an error (the ue5_compat helpers it named went with the script library, card 0306)
		if (!bResponseSuccess && !bTimedOut)
		{
			ResponseJson->SetStringField(TEXT("hint"),
				TEXT("If this error relates to the UE Python API (a missing method, a protected property), look the API up ")
				TEXT("(Context7, unreal-python-stubhub), or do the step with a NodeToCode tool: nodetocode.invoke_tool(name, arguments)."));
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
