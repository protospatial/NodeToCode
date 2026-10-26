// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetPythonScriptTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetPythonScriptTool)

FMcpToolDefinition FN2CMcpGetPythonScriptTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-python-script"),
		TEXT("Get a Python script's full code and metadata by name. "
			 "Returns the script code, description, tags, parameters, and usage stats. "
			 "Use this to understand implementation details before extending or modifying a script. "
			 "For just seeing importable functions (more token-efficient), use get-script-functions instead."),
		TEXT("Python")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// name property - required string
	TSharedPtr<FJsonObject> NameProp = MakeShareable(new FJsonObject);
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"),
		TEXT("The name of the script to retrieve. Use search-python-scripts or "
			 "list-python-scripts to discover available script names."));
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

FMcpToolCallResult FN2CMcpGetPythonScriptTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CMcpArgumentParser Parser(Arguments);

	FString Name;
	FString ErrorMsg;
	if (!Parser.TryGetRequiredString(TEXT("name"), Name, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}

	// Build Python function call
	FString FunctionCall = FString::Printf(TEXT("get_script(\"%s\")"),
		*EscapePythonString(Name));

	return ExecuteNodeToCodeFunction(FunctionCall);
}
