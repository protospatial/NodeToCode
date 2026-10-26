// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchPythonScriptsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpSearchPythonScriptsTool)

FMcpToolDefinition FN2CMcpSearchPythonScriptsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("search-python-scripts"),
		TEXT("CALL THIS FIRST before writing any Python code. Search for existing scripts to reuse. "
			 "Returns matching scripts sorted by relevance. "
			 "If exact match found: execute it with run-python. "
			 "If partial match found: use get-script-functions to see importable functions, then compose. "
			 "Only write new code if no relevant scripts exist."),
		TEXT("Python")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// query property - required string
	TSharedPtr<FJsonObject> QueryProp = MakeShareable(new FJsonObject);
	QueryProp->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetStringField(TEXT("description"),
		TEXT("Search query with keywords from the task. Matches against script names, descriptions, and tags. "
			 "Case-insensitive partial matching. Try multiple relevant keywords if first search yields no results."));
	Properties->SetObjectField(TEXT("query"), QueryProp);

	// limit property - optional number
	TSharedPtr<FJsonObject> LimitProp = MakeShareable(new FJsonObject);
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"),
		TEXT("Maximum number of results to return (default: 10, max: 50)"));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("query"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);

	Definition.InputSchema = Schema;

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	return Definition;
}

FMcpToolCallResult FN2CMcpSearchPythonScriptsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CMcpArgumentParser Parser(Arguments);

	FString Query;
	FString ErrorMsg;
	if (!Parser.TryGetRequiredString(TEXT("query"), Query, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}

	int32 Limit = static_cast<int32>(Parser.GetOptionalNumber(TEXT("limit"), 10.0));
	Limit = FMath::Clamp(Limit, 1, 50);

	// Build Python function call
	FString FunctionCall = FString::Printf(TEXT("search_scripts(\"%s\", limit=%d)"),
		*EscapePythonString(Query), Limit);

	return ExecuteNodeToCodeFunction(FunctionCall);
}
