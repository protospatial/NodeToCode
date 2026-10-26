// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetScriptFunctionsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetScriptFunctionsTool)

FMcpToolDefinition FN2CMcpGetScriptFunctionsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-script-functions"),
		TEXT("Get function signatures from a Python script using AST parsing (~80% more token-efficient than get-python-script). "
			 "Returns function names, parameters, types, and docstrings WITHOUT full implementation code. "
			 "Use this after search-python-scripts finds a partial match to see what functions can be imported. "
			 "Then import in your code: 'from scripts.<category>.<script_name> import function_name'."),
		TEXT("Python")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// name property - required string
	TSharedPtr<FJsonObject> NameProp = MakeShareable(new FJsonObject);
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"),
		TEXT("The name of the script to analyze. Use search-python-scripts or "
			 "list-python-scripts to find available script names."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("name"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);

	Definition.InputSchema = Schema;

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	return Definition;
}

FMcpToolCallResult FN2CMcpGetScriptFunctionsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CMcpArgumentParser Parser(Arguments);

	FString Name;
	FString ErrorMsg;
	if (!Parser.TryGetRequiredString(TEXT("name"), Name, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}

	// Build Python function call
	FString FunctionCall = FString::Printf(TEXT("get_script_functions(\"%s\")"),
		*EscapePythonString(Name));

	return ExecuteNodeToCodeFunction(FunctionCall);
}
