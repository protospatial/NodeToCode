// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeletePythonScriptTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpDeletePythonScriptTool)

FMcpToolDefinition FN2CMcpDeletePythonScriptTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("delete-python-script"),
		TEXT("Delete a Python script from the script library. "
			 "This permanently removes both the script file and its registry entry. "
			 "Use with caution - this action cannot be undone."),
		N2CMcpToolsets::Python
	);

	// Build input schema
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// name property - required string
	TSharedPtr<FJsonObject> NameProp = MakeShareable(new FJsonObject);
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"),
		TEXT("The name of the script to delete. Use list-python-scripts or "
			 "search-python-scripts to find the exact script name."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("name"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);

	Definition.InputSchema = Schema;

	// Note: This is a destructive operation, so we don't add read-only annotation

	return Definition;
}

FMcpToolCallResult FN2CMcpDeletePythonScriptTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CMcpArgumentParser Parser(Arguments);

	FString Name;
	FString ErrorMsg;
	if (!Parser.TryGetRequiredString(TEXT("name"), Name, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}

	// Build Python function call
	FString FunctionCall = FString::Printf(TEXT("delete_script(\"%s\")"),
		*EscapePythonString(Name));

	return ExecuteNodeToCodeFunction(FunctionCall);
}
