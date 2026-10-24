// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Prompts/Implementations/N2CMcpCodeGenerationPrompt.h"
#include "MCP/Resources/N2CMcpResourceManager.h"
#include "Utils/N2CLogger.h"

FMcpPromptDefinition FN2CMcpCodeGenerationPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("generate-code");
	Definition.Description = TEXT("Generate code from the current Blueprint with customization options");
	
	// Language argument
	FMcpPromptArgument LangArg;
	LangArg.Name = TEXT("language");
	LangArg.Description = TEXT("Target programming language (cpp, python, csharp, javascript, swift, pseudocode)");
	LangArg.bRequired = false;
	Definition.Arguments.Add(LangArg);
	
	// Style argument
	FMcpPromptArgument StyleArg;
	StyleArg.Name = TEXT("style");
	StyleArg.Description = TEXT("Coding style preference (verbose, concise, documented)");
	StyleArg.bRequired = false;
	Definition.Arguments.Add(StyleArg);
	
	// Optimization argument
	FMcpPromptArgument OptimizationArg;
	OptimizationArg.Name = TEXT("optimization");
	OptimizationArg.Description = TEXT("Optimization level (readability, performance, size)");
	OptimizationArg.bRequired = false;
	Definition.Arguments.Add(OptimizationArg);
	
	return Definition;
}

FMcpPromptResult FN2CMcpCodeGenerationPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	return ExecuteOnGameThread<FMcpPromptResult>([Arguments]() -> FMcpPromptResult
	{
		FMcpPromptResult Result;
		
		// Get current Blueprint as resource
		FString BlueprintUri = TEXT("nodetocode://blueprint/current");
		FMcpResourceContents BlueprintResource = FN2CMcpResourceManager::Get().ReadResource(BlueprintUri);
		
		// Build the prompt with context
		FString Language = Arguments.FindRef(TEXT("language"));
		if (Language.IsEmpty())
		{
			Language = TEXT("C++"); // Default to C++
		}
		
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
		
		// Create the prompt description
		Result.Description = FString::Printf(
			TEXT("Generate %s code from the current Blueprint with %s style optimized for %s"),
			*Language, *Style, *Optimization
		);
		
		// Create the user message
		FMcpPromptMessage UserMsg;
		UserMsg.Role = TEXT("user");
		UserMsg.Content.Type = TEXT("text");
		UserMsg.Content.Text = FString::Printf(
			TEXT("Please translate this Unreal Engine Blueprint to %s code. ")
			TEXT("Make the code %s and optimize for %s. ")
			TEXT("Focus on correctness and Unreal Engine best practices. ")
			TEXT("Include appropriate comments and follow standard %s conventions."),
			*Language, *Style, *Optimization, *Language
		);
		Result.Messages.Add(UserMsg);
		
		// Add the Blueprint as a resource reference
		FMcpPromptMessage ResourceMsg;
		ResourceMsg.Role = TEXT("user");
		ResourceMsg.Content.Type = TEXT("resource");
		ResourceMsg.Content.Resource = BlueprintResource;
		Result.Messages.Add(ResourceMsg);
		
		return Result;
	});
}

FMcpPromptDefinition FN2CMcpBlueprintAnalysisPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("analyze-blueprint");
	Definition.Description = TEXT("Analyze a Blueprint's structure, complexity, and potential issues");
	
	// Focus argument
	FMcpPromptArgument FocusArg;
	FocusArg.Name = TEXT("focus");
	FocusArg.Description = TEXT("Analysis focus (complexity, performance, best-practices, all)");
	FocusArg.bRequired = false;
	Definition.Arguments.Add(FocusArg);
	
	// Detail level argument
	FMcpPromptArgument DetailArg;
	DetailArg.Name = TEXT("detail");
	DetailArg.Description = TEXT("Level of detail (summary, detailed, comprehensive)");
	DetailArg.bRequired = false;
	Definition.Arguments.Add(DetailArg);
	
	return Definition;
}

FMcpPromptResult FN2CMcpBlueprintAnalysisPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	return ExecuteOnGameThread<FMcpPromptResult>([Arguments]() -> FMcpPromptResult
	{
		FMcpPromptResult Result;
		
		// Get current Blueprint as resource
		FString BlueprintUri = TEXT("nodetocode://blueprint/current");
		FMcpResourceContents BlueprintResource = FN2CMcpResourceManager::Get().ReadResource(BlueprintUri);
		
		// Build the prompt with context
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
		
		// Create the prompt description
		Result.Description = FString::Printf(
			TEXT("Analyze Blueprint focusing on %s with %s level of detail"),
			*Focus, *Detail
		);
		
		// Create the user message
		FMcpPromptMessage UserMsg;
		UserMsg.Role = TEXT("user");
		UserMsg.Content.Type = TEXT("text");
		UserMsg.Content.Text = FString::Printf(
			TEXT("Please analyze this Unreal Engine Blueprint. ")
			TEXT("Focus on %s and provide a %s analysis. ")
			TEXT("Identify potential issues, suggest improvements, and highlight best practices. ")
			TEXT("Consider performance implications and maintainability."),
			*Focus, *Detail
		);
		Result.Messages.Add(UserMsg);
		
		// Add the Blueprint as a resource reference
		FMcpPromptMessage ResourceMsg;
		ResourceMsg.Role = TEXT("user");
		ResourceMsg.Content.Type = TEXT("resource");
		ResourceMsg.Content.Resource = BlueprintResource;
		Result.Messages.Add(ResourceMsg);
		
		return Result;
	});
}

FMcpPromptDefinition FN2CMcpRefactorPrompt::GetDefinition() const
{
	FMcpPromptDefinition Definition;
	Definition.Name = TEXT("refactor-blueprint");
	Definition.Description = TEXT("Suggest refactoring improvements for Blueprint code");
	
	// Goal argument
	FMcpPromptArgument GoalArg;
	GoalArg.Name = TEXT("goal");
	GoalArg.Description = TEXT("Refactoring goal (simplify, optimize, modularize, clean)");
	GoalArg.bRequired = false;
	Definition.Arguments.Add(GoalArg);
	
	// Language argument
	FMcpPromptArgument LangArg;
	LangArg.Name = TEXT("language");
	LangArg.Description = TEXT("Target language for refactored code (cpp, python, etc.)");
	LangArg.bRequired = false;
	Definition.Arguments.Add(LangArg);
	
	return Definition;
}

FMcpPromptResult FN2CMcpRefactorPrompt::GetPrompt(const FMcpPromptArguments& Arguments)
{
	return ExecuteOnGameThread<FMcpPromptResult>([Arguments]() -> FMcpPromptResult
	{
		FMcpPromptResult Result;
		
		// Get current Blueprint as resource
		FString BlueprintUri = TEXT("nodetocode://blueprint/current");
		FMcpResourceContents BlueprintResource = FN2CMcpResourceManager::Get().ReadResource(BlueprintUri);
		
		// Build the prompt with context
		FString Goal = Arguments.FindRef(TEXT("goal"));
		if (Goal.IsEmpty())
		{
			Goal = TEXT("improve overall quality");
		}
		
		FString Language = Arguments.FindRef(TEXT("language"));
		if (Language.IsEmpty())
		{
			Language = TEXT("C++");
		}
		
		// Create the prompt description
		Result.Description = FString::Printf(
			TEXT("Refactor Blueprint to %s, targeting %s"),
			*Goal, *Language
		);
		
		// Create the user message
		FMcpPromptMessage UserMsg;
		UserMsg.Role = TEXT("user");
		UserMsg.Content.Type = TEXT("text");
		UserMsg.Content.Text = FString::Printf(
			TEXT("Please refactor this Unreal Engine Blueprint to %s. ")
			TEXT("Target language is %s. ")
			TEXT("Provide specific refactoring suggestions with code examples. ")
			TEXT("Explain the benefits of each suggested change. ")
			TEXT("Maintain functionality while improving code quality."),
			*Goal, *Language
		);
		Result.Messages.Add(UserMsg);

		// Add the Blueprint as a resource reference
		FMcpPromptMessage ResourceMsg;
		ResourceMsg.Role = TEXT("user");
		ResourceMsg.Content.Type = TEXT("resource");
		ResourceMsg.Content.Resource = BlueprintResource;
		Result.Messages.Add(ResourceMsg);

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
	Definition.Description = TEXT("Write Unreal Engine Python scripts with Context7 documentation lookup and script management. "
		"REQUIRES: Context7 MCP server for API documentation lookup via the radial-hks/unreal-python-stubhub library.");

	// Task argument - what the user wants to accomplish
	FMcpPromptArgument TaskArg;
	TaskArg.Name = TEXT("task");
	TaskArg.Description = TEXT("Description of what the Python script should accomplish (e.g., 'create health variables', 'add debug nodes')");
	TaskArg.bRequired = true;
	Definition.Arguments.Add(TaskArg);

	// Save argument - whether to save the script for reuse
	FMcpPromptArgument SaveArg;
	SaveArg.Name = TEXT("save_script");
	SaveArg.Description = TEXT("Whether to save the script for future reuse (yes, no, ask). Default: ask");
	SaveArg.bRequired = false;
	Definition.Arguments.Add(SaveArg);

	// Category argument - for organizing saved scripts
	FMcpPromptArgument CategoryArg;
	CategoryArg.Name = TEXT("category");
	CategoryArg.Description = TEXT("Category for saved scripts (gameplay, ui, utilities, animation, etc.). Default: general");
	CategoryArg.bRequired = false;
	Definition.Arguments.Add(CategoryArg);

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

	// Create the prompt description
	Result.Description = FString::Printf(
		TEXT("Write Python script for: %s (Category: %s, Save: %s)"),
		*Task, *Category, *SaveScript
	);

	// Create the comprehensive instruction message
	FMcpPromptMessage InstructionMsg;
	InstructionMsg.Role = TEXT("user");
	InstructionMsg.Content.Type = TEXT("text");
	InstructionMsg.Content.Text = FString::Printf(TEXT(
		"# Unreal Engine Python Scripting Task\n\n"
		"**Task:** %s\n\n"
		"---\n\n"
		"## MANDATORY WORKFLOW\n\n"
		"You MUST follow this workflow when writing Unreal Engine Python scripts:\n\n"
		"### Step 1: Check for Existing Scripts\n"
		"Before writing ANY new code, use the NodeToCode script management tools:\n"
		"1. Call `search-python-scripts` with relevant keywords from the task\n"
		"2. Call `list-python-scripts` to see available scripts in the '%s' category\n"
		"3. Analyze results for:\n"
		"   - **Exact match**: A script that solves the entire task → Execute it with `run-python`\n"
		"   - **Partial matches**: Scripts with useful functions → Import and reuse them (see Step 3)\n"
		"   - **No matches**: Proceed to write a new script\n\n"
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
		"### Step 4: Execute and Test\n"
		"Use the `run-python` tool to execute your script and verify it works.\n\n"
		"### Step 5: Save for Reuse (%s)\n"
		"If the script is useful and reusable:\n"
		"1. Use `save-python-script` with:\n"
		"   - A descriptive name (snake_case)\n"
		"   - Clear description of what it does\n"
		"   - Relevant tags for searchability\n"
		"   - Category: '%s'\n\n"
		"---\n\n"
		"## Available NodeToCode Tools\n\n"
		"**Script Management:**\n"
		"- `list-python-scripts` - List scripts by category\n"
		"- `search-python-scripts` - Search by name/description/tags\n"
		"- `get-python-script` - Get full script code\n"
		"- `save-python-script` - Save script to library\n"
		"- `delete-python-script` - Remove a script\n\n"
		"**Execution:**\n"
		"- `run-python` - Execute Python code in UE\n\n"
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
		"5. Save useful scripts to grow the shared library\n"
	),
		*Task,
		*Category,
		*SaveScript,
		*Category
	);
	Result.Messages.Add(InstructionMsg);

	return Result;
}