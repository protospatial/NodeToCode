// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Prompts/Implementations/N2CMcpCodeGenerationPrompt.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Core/N2CTargetLanguage.h"
#include "MCP/Resources/N2CMcpResourceManager.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CLogger.h"

namespace
{
	const TCHAR* const BlueprintUri = TEXT("nodetocode://blueprint/current");

	FMcpPromptArgument MakeArgument(const TCHAR* Name, const TCHAR* Title, const FString& Description, bool bRequired = false)
	{
		FMcpPromptArgument Argument;
		Argument.Name = Name;
		Argument.Title = Title;
		Argument.Description = Description;
		Argument.bRequired = bRequired;
		return Argument;
	}

	FMcpPromptMessage MakeTextMessage(const FString& Text)
	{
		FMcpPromptMessage Message;
		Message.Role = TEXT("user");
		Message.Content.Type = TEXT("text");
		Message.Content.Text = Text;
		return Message;
	}
}

// ============================================================================
// Shared by the Blueprint prompts (card 0124)
// ============================================================================

const TArray<FN2CMcpPromptLanguage>& FN2CMcpBlueprintPrompts::Languages()
{
	// The target-language table's rows, with the ids the MCP tools use
	static const TArray<FN2CMcpPromptLanguage> All = []
	{
		TArray<FN2CMcpPromptLanguage> Rows;
		for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
		{
			Rows.Add({Language.GetMcpId(), Language.DisplayName, Language.Enum});
		}
		return Rows;
	}();
	return All;
}

FString FN2CMcpBlueprintPrompts::LanguageIdList()
{
	TArray<FString> Ids;
	for (const FN2CMcpPromptLanguage& Language : Languages())
	{
		Ids.Add(Language.Id);
	}
	return FString::Join(Ids, TEXT(", "));
}

bool FN2CMcpBlueprintPrompts::ResolveLanguage(const FMcpPromptArguments& Arguments, FN2CMcpPromptLanguage& OutLanguage,
	FMcpPromptResult& OutError)
{
	const FString Requested = Arguments.FindRef(TEXT("language")).TrimStartAndEnd();
	if (Requested.IsEmpty())
	{
		OutLanguage = Languages()[0];
		return true;
	}
	for (const FN2CMcpPromptLanguage& Language : Languages())
	{
		if (Language.Id.Equals(Requested, ESearchCase::IgnoreCase))
		{
			OutLanguage = Language;
			return true;
		}
	}
	// MCP 2025-11-25, server/prompts, error handling: invalid arguments are -32602 (Invalid params)
	OutError.ErrorCode = JsonRpcErrorCodes::InvalidParams;
	OutError.ErrorMessage = FString::Printf(TEXT("Unknown language '%s'. Use one of: %s"), *Requested, *LanguageIdList());
	return false;
}

FString FN2CMcpBlueprintPrompts::CodeGenInstructions(EN2CCodeLanguage Language)
{
	// The same assembly as the in-editor translation: CodeGen_<Language>.md with the shared N2C JSON specification
	// and response format (card 0132)
	const TStrongObjectPtr<UN2CSystemPromptManager> Manager(NewObject<UN2CSystemPromptManager>(GetTransientPackage()));
	Manager->Initialize(FN2CLLMConfig());
	return Manager->GetLanguageSpecificPrompt(N2C::BlueprintPromptFamily, Language);
}

void FN2CMcpBlueprintPrompts::AppendBlueprint(FMcpPromptResult& Result, const FMcpResourceContents& Blueprint)
{
	Result.Messages.Add(MakeTextMessage(FString::Printf(
		TEXT("%s It is the N2C JSON of the Blueprint focused in the editor (%s), in the next message."),
		FN2CMcpUntrustedContent::Notice, *Blueprint.Uri)));

	FMcpPromptMessage ResourceMsg;
	ResourceMsg.Role = TEXT("user");
	ResourceMsg.Content.Type = TEXT("resource");
	ResourceMsg.Content.Resource = Blueprint;
	// The resource marks its own text; marking here as well covers a resource read some other way (MarkJson adds one mark)
	ResourceMsg.Content.Resource.Text = FN2CMcpUntrustedContent::MarkJson(Blueprint.Text);
	ResourceMsg.Content.Annotations = FN2CMcpUntrustedContent::AssistantAudience();
	Result.Messages.Add(ResourceMsg);
}

// ============================================================================
// generate-code
// ============================================================================

FMcpPromptDefinition FN2CMcpCodeGenerationPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("generate-code");
	Definition.Title = TEXT("Generate Code from Blueprint");
	Definition.Description = TEXT("Translate the focused Blueprint to code with NodeToCode's own translation instructions for the language");
	Definition.Arguments.Add(MakeArgument(TEXT("language"), TEXT("Target language"),
		FString::Printf(TEXT("Target language, one of: %s. Default: cpp"), *FN2CMcpBlueprintPrompts::LanguageIdList())));
	Definition.Arguments.Add(MakeArgument(TEXT("style"), TEXT("Coding style"),
		TEXT("Coding style preference (verbose, concise, documented)")));
	Definition.Arguments.Add(MakeArgument(TEXT("optimization"), TEXT("Optimize for"),
		TEXT("Optimization level (readability, performance, size)")));
	return Definition;
}

FMcpPromptResult FN2CMcpCodeGenerationPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	FN2CMcpPromptLanguage Language;
	FMcpPromptResult Error;
	if (!FN2CMcpBlueprintPrompts::ResolveLanguage(Arguments, Language, Error))
	{
		return Error;
	}

	return ExecuteOnGameThread<FMcpPromptResult>([Arguments, Language]() -> FMcpPromptResult
	{
		FMcpPromptResult Result;

		FString Style = Arguments.FindRef(TEXT("style"));
		if (Style.IsEmpty())
		{
			Style = TEXT("clear and well-documented");
		}

		FString Optimization = Arguments.FindRef(TEXT("optimization"));
		if (Optimization.IsEmpty())
		{
			Optimization = TEXT("readability");
		}

		Result.Description = FString::Printf(
			TEXT("Generate %s code from the current Blueprint with %s style optimized for %s"),
			*Language.DisplayName, *Style, *Optimization);

		// NodeToCode's tuned translation instructions, the ones the in-editor translation sends as its system prompt
		Result.Messages.Add(MakeTextMessage(FString::Printf(
			TEXT("Translate the Unreal Engine Blueprint given at the end of this prompt to %s, following NodeToCode's ")
			TEXT("translation instructions below. Make the code %s and optimize for %s.\n\n")
			TEXT("<n2c_translation_instructions>\n%s\n</n2c_translation_instructions>"),
			*Language.DisplayName, *Style, *Optimization, *FN2CMcpBlueprintPrompts::CodeGenInstructions(Language.Language))));

		FN2CMcpBlueprintPrompts::AppendBlueprint(Result, FN2CMcpResourceManager::Get().ReadResource(BlueprintUri));
		return Result;
	});
}

// ============================================================================
// analyze-blueprint
// ============================================================================

FMcpPromptDefinition FN2CMcpBlueprintAnalysisPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("analyze-blueprint");
	Definition.Title = TEXT("Analyze Blueprint");
	Definition.Description = TEXT("Analyze a Blueprint's structure, complexity, and potential issues");
	Definition.Arguments.Add(MakeArgument(TEXT("focus"), TEXT("Focus"),
		TEXT("Analysis focus (complexity, performance, best-practices, all)")));
	Definition.Arguments.Add(MakeArgument(TEXT("detail"), TEXT("Detail level"),
		TEXT("Level of detail (summary, detailed, comprehensive)")));
	return Definition;
}

FMcpPromptResult FN2CMcpBlueprintAnalysisPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	return ExecuteOnGameThread<FMcpPromptResult>([Arguments]() -> FMcpPromptResult
	{
		FMcpPromptResult Result;

		FString Focus = Arguments.FindRef(TEXT("focus"));
		if (Focus.IsEmpty())
		{
			Focus = TEXT("all aspects");
		}

		FString Detail = Arguments.FindRef(TEXT("detail"));
		if (Detail.IsEmpty())
		{
			Detail = TEXT("detailed");
		}

		Result.Description = FString::Printf(TEXT("Analyze Blueprint focusing on %s with %s level of detail"), *Focus, *Detail);

		Result.Messages.Add(MakeTextMessage(FString::Printf(
			TEXT("Please analyze the Unreal Engine Blueprint given at the end of this prompt. ")
			TEXT("Focus on %s and provide a %s analysis. ")
			TEXT("Identify potential issues, suggest improvements, and highlight best practices. ")
			TEXT("Consider performance implications and maintainability."),
			*Focus, *Detail)));

		FN2CMcpBlueprintPrompts::AppendBlueprint(Result, FN2CMcpResourceManager::Get().ReadResource(BlueprintUri));
		return Result;
	});
}

// ============================================================================
// refactor-blueprint
// ============================================================================

FMcpPromptDefinition FN2CMcpRefactorPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("refactor-blueprint");
	Definition.Title = TEXT("Refactor Blueprint");
	Definition.Description = TEXT("Suggest refactoring improvements for Blueprint code");
	Definition.Arguments.Add(MakeArgument(TEXT("goal"), TEXT("Goal"),
		TEXT("Refactoring goal (simplify, optimize, modularize, clean)")));
	Definition.Arguments.Add(MakeArgument(TEXT("language"), TEXT("Target language"),
		FString::Printf(TEXT("Target language for refactored code, one of: %s. Default: cpp"), *FN2CMcpBlueprintPrompts::LanguageIdList())));
	return Definition;
}

FMcpPromptResult FN2CMcpRefactorPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	FN2CMcpPromptLanguage Language;
	FMcpPromptResult Error;
	if (!FN2CMcpBlueprintPrompts::ResolveLanguage(Arguments, Language, Error))
	{
		return Error;
	}

	return ExecuteOnGameThread<FMcpPromptResult>([Arguments, Language]() -> FMcpPromptResult
	{
		FMcpPromptResult Result;

		FString Goal = Arguments.FindRef(TEXT("goal"));
		if (Goal.IsEmpty())
		{
			Goal = TEXT("improve overall quality");
		}

		Result.Description = FString::Printf(TEXT("Refactor Blueprint to %s, targeting %s"), *Goal, *Language.DisplayName);

		Result.Messages.Add(MakeTextMessage(FString::Printf(
			TEXT("Please refactor the Unreal Engine Blueprint given at the end of this prompt to %s. ")
			TEXT("Target language is %s. ")
			TEXT("Provide specific refactoring suggestions with code examples. ")
			TEXT("Explain the benefits of each suggested change. ")
			TEXT("Maintain functionality while improving code quality."),
			*Goal, *Language.DisplayName)));

		FN2CMcpBlueprintPrompts::AppendBlueprint(Result, FN2CMcpResourceManager::Get().ReadResource(BlueprintUri));
		return Result;
	});
}

// ============================================================================
// Python Scripting Prompt - Context7 lookup, the nodetocode module, run-python
// ============================================================================

FMcpPromptDefinition FN2CMcpPythonScriptingPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("python-scripting");
	Definition.Title = TEXT("Write a UE Python Script");
	Definition.Description = TEXT("Write Unreal Engine Python scripts with Context7 documentation lookup, the nodetocode module "
		"and run-python. REQUIRES: Context7 MCP server for API documentation lookup via the radial-hks/unreal-python-stubhub "
		"library. Running scripts needs Allow MCP Python Execution.");
	Definition.Arguments.Add(MakeArgument(TEXT("task"), TEXT("Task"),
		TEXT("Description of what the Python script should accomplish (e.g., 'list every Blueprint without a description', 'rename the selected actors')"), true));
	return Definition;
}

FMcpPromptResult FN2CMcpPythonScriptingPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	FMcpPromptResult Result;

	// Get arguments
	FString Task = Arguments.FindRef(TEXT("task"));
	if (Task.IsEmpty())
	{
		Result.Description = TEXT("Error: 'task' argument is required");
		return Result;
	}

	// run-python exists only with the per-user Allow MCP Python Execution on (card 0423); without it the workflow
	// below cannot run anything, so the prompt says so (card 0124)
	const bool bPythonExecution = FN2CMcpToolManager::Get().IsToolRegistered(TEXT("run-python"));

	Result.Description = FString::Printf(TEXT("Write Python script for: %s%s"),
		*Task, bPythonExecution ? TEXT("") : TEXT(" - run-python is disabled"));

	const FString DisabledSection = bPythonExecution ? FString() : FString(TEXT(
		"## run-python is disabled on this server\n\n"
		"`run-python` is not available: the per-user setting **Allow MCP Python Execution** is off (the default). To "
		"enable it, the user turns it on in Editor Preferences > Plugins > Node to Code (User) > MCP Server and restarts "
		"the editor. It lets any connected MCP client run arbitrary Python in the editor, so leave that decision to the "
		"user.\n\n"
		"Until then you can still research the API and write the script. Give the finished script to the user to run "
		"(for example in the editor's Python console), and say that it has not been run or tested. Do not claim it "
		"ran.\n\n"
		"---\n\n"));
	const TCHAR* const ExecuteStep = bPythonExecution
		? TEXT("Use the `run-python` tool to execute your script and verify it works. Set `result` to return structured data.\n\n")
		: TEXT("`run-python` is disabled: give the script to the user to run, and say it has not been run or tested.\n\n");
	const TCHAR* const RunPythonState = bPythonExecution ? TEXT("") : TEXT(" (disabled: Allow MCP Python Execution is off)");

	FString Text;
	Text += FString::Printf(TEXT("# Unreal Engine Python Scripting Task\n\n**Task:** %s\n\n---\n\n"), *Task);
	Text += DisabledSection;
	Text += TEXT(
		"## WORKFLOW\n\n"
		"### Step 1: Use what already exists\n"
		"- If an Agent Skill you have loaded covers the task (NodeToCode ships Agent Skills with its Claude Code plugin), "
		"follow it and run its script.\n"
		"- A NodeToCode MCP tool may already do the job: call it directly, or from a script with "
		"`n2c.invoke_tool('<tool-name>', {...})`.\n"
		"- The `nodetocode` module wraps the common Blueprint operations (see below).\n\n"
		"### Step 2: Research the UE Python API (REQUIRED for new code)\n"
		"Use Context7 MCP to look up the correct API:\n\n"
		"1. **Resolve the library ID first:**\n"
		"   ```\n"
		"   resolve-library-id:\n"
		"     libraryName: \"unreal-python-stubhub\"\n"
		"     query: \"<your specific API question>\"\n"
		"   ```\n"
		"   This will return the Context7 library ID for `radial-hks/unreal-python-stubhub`\n\n"
		"2. **Query the documentation:**\n"
		"   ```\n"
		"   query-docs:\n"
		"     libraryId: \"/radial-hks/unreal-python-stubhub\"  (or version-specific ID)\n"
		"     query: \"<specific API method or class you need>\"\n"
		"   ```\n\n"
		"**DO NOT guess or assume API signatures.** Always verify with Context7.\n\n"
		"### Step 3: Write the Python Script\n"
		"**Standard Imports:**\n"
		"- `import unreal` - Unreal Python API\n"
		"- `import nodetocode as n2c` - NodeToCode utilities (Blueprint editing, compile, save, tags, any tool by name)\n\n"
		"**Guidelines:**\n"
		"- Define functions with clear names and docstrings, one job each, parameters instead of hardcoded values\n"
		"- Wrap edits in `with unreal.ScopedEditorTransaction('...'):` so one undo reverts them\n"
		"- Set a `result` variable at the end to return structured data\n"
		"- Handle errors with `except Exception as e:` and report them; never a bare `except:`\n\n"
		"### Step 4: Execute and Test\n");
	Text += ExecuteStep;
	Text += TEXT(
		"### Step 5: Keep it if it is reusable\n"
		"NodeToCode has no script library (2.0, decision 0010). If the script is worth keeping, suggest the user saves it "
		"as an Agent Skill: a folder with a `SKILL.md` (name, description, when to use it) and the script under its "
		"`scripts/`, which agents then run through `run-python`.\n\n");
	Text += FString::Printf(TEXT(
		"---\n\n"
		"## Available NodeToCode Tools\n\n"
		"**Execution:**\n"
		"- `run-python` - Execute Python code in UE%s\n\n"), RunPythonState);
	Text += TEXT(
		"**NodeToCode Module (in scripts):**\n"
		"- `n2c.invoke_tool(name, arguments)` - Run any NodeToCode MCP tool by name\n"
		"- `n2c.get_focused_blueprint()` - Get current Blueprint info\n"
		"- `n2c.search_blueprint_nodes()`, `n2c.add_node_to_graph()`, `n2c.connect_pins()` - Edit a graph\n"
		"- `n2c.compile_blueprint()` - Compile the Blueprint\n"
		"- `n2c.save_blueprint()` - Save to disk\n"
		"- `n2c.tag_graph()` - Tag the current graph\n"
		"- `n2c.get_llm_providers()` - Get available LLM providers\n\n"
		"---\n\n"
		"## Context7 Integration\n\n"
		"The `radial-hks/unreal-python-stubhub` library in Context7 contains:\n"
		"- Complete Unreal Engine Python API stubs\n"
		"- All `unreal` module classes, functions, and properties\n"
		"- Editor subsystem APIs\n"
		"- Asset manipulation APIs\n"
		"- Blueprint manipulation APIs\n\n"
		"**Always query Context7 for:**\n"
		"- Correct method signatures\n"
		"- Available parameters and their types\n"
		"- Return types and expected values\n"
		"- Related classes and utilities\n\n"
		"---\n\n"
		"Now, proceed with the task. Remember:\n"
		"1. Use a loaded Agent Skill, a NodeToCode tool or the nodetocode module when one fits\n"
		"2. Use Context7 to research APIs (MANDATORY - never assume)\n"
		"3. Run and verify the script before reporting success\n");

	Result.Messages.Add(MakeTextMessage(Text));
	return Result;
}
