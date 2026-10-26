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
// Python Scripting Prompt - Enforces Context7 and Script Management
// ============================================================================

FMcpPromptDefinition FN2CMcpPythonScriptingPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("python-scripting");
	Definition.Title = TEXT("Write a UE Python Script");
	Definition.Description = TEXT("Write Unreal Engine Python scripts with Context7 documentation lookup and script management. "
		"REQUIRES: Context7 MCP server for API documentation lookup via the radial-hks/unreal-python-stubhub library. "
		"Running and saving scripts needs Allow MCP Python Execution.");
	Definition.Arguments.Add(MakeArgument(TEXT("task"), TEXT("Task"),
		TEXT("Description of what the Python script should accomplish (e.g., 'create health variables', 'add debug nodes')"), true));
	Definition.Arguments.Add(MakeArgument(TEXT("save_script"), TEXT("Save script"),
		TEXT("Whether to save the script for future reuse (yes, no, ask). Default: ask")));
	Definition.Arguments.Add(MakeArgument(TEXT("category"), TEXT("Category"),
		TEXT("Category for saved scripts (gameplay, ui, utilities, animation, etc.). Default: general")));
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

	FString SaveScript = Arguments.FindRef(TEXT("save_script"));
	if (SaveScript.IsEmpty())
	{
		SaveScript = TEXT("ask");
	}

	FString Category = Arguments.FindRef(TEXT("category"));
	if (Category.IsEmpty())
	{
		Category = TEXT("general");
	}

	// run-python, save-python-script and delete-python-script exist only with the per-user Allow MCP Python Execution
	// on (card 0423); without them the workflow below cannot run or save anything, so the prompt says so (card 0124)
	const bool bPythonExecution = FN2CMcpToolManager::Get().IsToolRegistered(TEXT("run-python"));

	Result.Description = FString::Printf(
		TEXT("Write Python script for: %s (Category: %s, Save: %s)%s"),
		*Task, *Category, *SaveScript, bPythonExecution ? TEXT("") : TEXT(" - run-python is disabled"));

	const FString DisabledSection = bPythonExecution ? FString() : FString(TEXT(
		"## run-python is disabled on this server\n\n"
		"`run-python`, `save-python-script` and `delete-python-script` are not available: the per-user setting "
		"**Allow MCP Python Execution** is off (the default). To enable them, the user turns it on in Project Settings > "
		"Plugins > Node to Code (User) > MCP Server and restarts the editor. It lets any connected MCP client run "
		"arbitrary Python in the editor, so leave that decision to the user.\n\n"
		"Until then you can still search, list and read saved scripts, research the API and write the script. Give "
		"the finished script to the user to run (for example in the editor's Python console), and say that it has "
		"not been run or tested. Do not claim it ran.\n\n"
		"---\n\n"));
	const TCHAR* const ExactMatchAction = bPythonExecution
		? TEXT("Execute it with `run-python`")
		: TEXT("Give it to the user to run (`run-python` is disabled)");
	const TCHAR* const ExecuteStep = bPythonExecution
		? TEXT("Use the `run-python` tool to execute your script and verify it works.\n\n")
		: TEXT("`run-python` is disabled: give the script to the user to run, and say it has not been run or tested.\n\n");
	const FString SaveStep = bPythonExecution
		? FString::Printf(TEXT(
			"If the script is useful and reusable:\n"
			"1. Use `save-python-script` with:\n"
			"   - A descriptive name (snake_case)\n"
			"   - Clear description of what it does\n"
			"   - Relevant tags for searchability\n"
			"   - Category: '%s'\n\n"), *Category)
		: FString::Printf(TEXT(
			"`save-python-script` is disabled with `run-python`. If the script is reusable, suggest the user saves it as "
			"`Content/Python/scripts/%s/<name>.py` once Allow MCP Python Execution is on.\n\n"), *Category);
	const TCHAR* const WriteToolsState = bPythonExecution ? TEXT("") : TEXT(" (disabled: Allow MCP Python Execution is off)");

	FString Text;
	Text += FString::Printf(TEXT("# Unreal Engine Python Scripting Task\n\n**Task:** %s\n\n---\n\n"), *Task);
	Text += DisabledSection;
	Text += FString::Printf(TEXT(
		"## MANDATORY WORKFLOW\n\n"
		"You MUST follow this workflow when writing Unreal Engine Python scripts:\n\n"
		"### Step 1: Check for Existing Scripts\n"
		"Before writing ANY new code, use the NodeToCode script management tools:\n"
		"1. Call `search-python-scripts` with relevant keywords from the task\n"
		"2. Call `list-python-scripts` to see available scripts in the '%s' category\n"
		"3. Analyze results for:\n"
		"   - **Exact match**: A script that solves the entire task → %s\n"
		"   - **Partial matches**: Scripts with useful functions → Import and reuse them (see Step 3)\n"
		"   - **No matches**: Proceed to write a new script\n\n"), *Category, ExactMatchAction);
	Text += TEXT(
		"### Step 2: Research the UE Python API (REQUIRED for new/partial scripts)\n"
		"If writing new code or extending existing scripts, you MUST use Context7 MCP to look up the correct API:\n\n"
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
		"### Step 3: Write the Python Script (Modular & Compositional)\n"
		"After researching the API, write your script following these guidelines:\n\n"
		"**Standard Imports:**\n"
		"- `import unreal` - Unreal Python API\n"
		"- `import nodetocode as n2c` - NodeToCode utilities (Blueprint info, tagging, etc.)\n\n"
		"**Reusing Existing Scripts (IMPORTANT):**\n"
		"- Saved scripts are in `Content/Python/scripts/<category>/` and are importable as modules\n"
		"- Import saved scripts: `from scripts.<category>.<script_name> import function_name`\n"
		"- Example: `from scripts.core.asset_iterator import find_assets_by_type`\n"
		"- ALWAYS reuse existing functions instead of rewriting them\n"
		"- Build on top of existing scripts when they solve part of your task\n\n"
		"**Structuring Your Script for Reuse:**\n"
		"- Define functions with clear names and docstrings\n"
		"- Keep functions focused on single responsibilities\n"
		"- Use parameters for flexibility (don't hardcode values)\n"
		"- Return structured data (dicts, lists, objects)\n"
		"- Example structure:\n"
		"  ```python\n"
		"  def my_reusable_function(param1, param2):\n"
		"      '''Clear docstring explaining what this does.'''\n"
		"      # Implementation\n"
		"      return result\n"
		"  \n"
		"  # Main execution block (for when script is run directly)\n"
		"  if __name__ == '__main__':\n"
		"      result = my_reusable_function('value1', 'value2')\n"
		"  ```\n\n"
		"**Other Guidelines:**\n"
		"- Set a `result` variable at the end to return structured data\n"
		"- Handle errors gracefully with try/except blocks\n"
		"- Include comments explaining complex logic\n\n"
		"### Step 4: Execute and Test\n");
	Text += ExecuteStep;
	Text += FString::Printf(TEXT("### Step 5: Save for Reuse (%s)\n"), *SaveScript);
	Text += SaveStep;
	Text += FString::Printf(TEXT(
		"---\n\n"
		"## Available NodeToCode Tools\n\n"
		"**Script Management:**\n"
		"- `list-python-scripts` - List scripts by category\n"
		"- `search-python-scripts` - Search by name/description/tags\n"
		"- `get-python-script` - Get full script code\n"
		"- `save-python-script` - Save script to library%s\n"
		"- `delete-python-script` - Remove a script%s\n\n"
		"**Execution:**\n"
		"- `run-python` - Execute Python code in UE%s\n\n"), WriteToolsState, WriteToolsState, WriteToolsState);
	Text += TEXT(
		"**NodeToCode Module (in scripts):**\n"
		"- `n2c.get_focused_blueprint()` - Get current Blueprint info\n"
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
		"## Key Principles\n\n"
		"1. **DRY (Don't Repeat Yourself)**: Search and reuse existing scripts before writing new code\n"
		"2. **Composition over Duplication**: Import and combine existing functions instead of copying code\n"
		"3. **Modular Design**: Write functions that others (and future you) can import and reuse\n"
		"4. **Verify APIs**: Always use Context7 to look up correct API signatures - never guess\n"
		"5. **Build a Library**: Every script you save adds to a growing toolkit for future tasks\n\n"
		"Now, proceed with the task. Remember:\n"
		"1. Search existing scripts FIRST (exact match → execute; partial match → import & compose)\n"
		"2. Use Context7 to research APIs (MANDATORY - never assume)\n"
		"3. Import and reuse existing functions whenever possible\n"
		"4. Structure new scripts as reusable modules with clear functions\n"
		"5. Save useful scripts to grow the shared library\n");

	Result.Messages.Add(MakeTextMessage(Text));
	return Result;
}
