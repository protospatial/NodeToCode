// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpListPythonScriptsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpListPythonScriptsTool)

FMcpToolDefinition FN2CMcpListPythonScriptsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("list-python-scripts"),
		TEXT("List available Python scripts from the script library. "
			 "Returns script names with metadata (description, tags, usage count). "
			 "Use alongside search-python-scripts to discover reusable scripts before writing new code (DRY principle). "
			 "Browse by category to find related utilities that can be imported and composed."),
		N2CMcpToolsets::Python
	);

	// Build input schema
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// category property - optional string
	TSharedPtr<FJsonObject> CategoryProp = MakeShareable(new FJsonObject);
	CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	CategoryProp->SetStringField(TEXT("description"),
		TEXT("Filter scripts by category (e.g., 'gameplay', 'ui', 'utilities', 'animation'). "
			 "Leave empty for all categories. Categories organize scripts for easier discovery and import."));
	Properties->SetObjectField(TEXT("category"), CategoryProp);

	// limit property - optional number
	TSharedPtr<FJsonObject> LimitProp = MakeShareable(new FJsonObject);
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"),
		TEXT("Maximum number of scripts to return (default: 20, max: 100)"));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>());

	Definition.InputSchema = Schema;

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	return Definition;
}

FMcpToolCallResult FN2CMcpListPythonScriptsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CMcpArgumentParser Parser(Arguments);

	FString Category = Parser.GetOptionalString(TEXT("category"), TEXT(""));
	int32 Limit = static_cast<int32>(Parser.GetOptionalNumber(TEXT("limit"), 20.0));
	Limit = FMath::Clamp(Limit, 1, 100);

	// Build Python function call
	FString FunctionCall;
	if (Category.IsEmpty())
	{
		FunctionCall = FString::Printf(TEXT("list_scripts(limit=%d)"), Limit);
	}
	else
	{
		FunctionCall = FString::Printf(TEXT("list_scripts(category=\"%s\", limit=%d)"),
			*EscapePythonString(Category), Limit);
	}

	return ExecuteNodeToCodeFunction(FunctionCall);
}
