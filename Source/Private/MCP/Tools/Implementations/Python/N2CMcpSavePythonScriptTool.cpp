// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSavePythonScriptTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpSavePythonScriptTool)

FMcpToolDefinition FN2CMcpSavePythonScriptTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("save-python-script"),
		TEXT("Save a Python script to the script library for reuse. "
			 "Stored in Content/Python/scripts/<category>/ and importable as 'from scripts.<category>.<name> import func'. "
			 "Write modular scripts with focused functions, clear docstrings, and parameterized inputs. "
			 "Every saved script grows the reusable toolkit for future tasks."),
		TEXT("Python")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// name property - required string
	TSharedPtr<FJsonObject> NameProp = MakeShareable(new FJsonObject);
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"),
		TEXT("Unique name for the script (alphanumeric and underscores only). "
			 "Will be used as the filename and for retrieval."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	// code property - required string
	TSharedPtr<FJsonObject> CodeProp = MakeShareable(new FJsonObject);
	CodeProp->SetStringField(TEXT("type"), TEXT("string"));
	CodeProp->SetStringField(TEXT("description"),
		TEXT("The Python code to save. Should be valid Python that can run in "
			 "Unreal Engine's Python environment. Structure with reusable functions (def), "
			 "docstrings, and parameters for flexibility. Avoid hardcoded values."));
	Properties->SetObjectField(TEXT("code"), CodeProp);

	// description property - required string
	TSharedPtr<FJsonObject> DescProp = MakeShareable(new FJsonObject);
	DescProp->SetStringField(TEXT("type"), TEXT("string"));
	DescProp->SetStringField(TEXT("description"),
		TEXT("Brief description of what the script does. Used for search."));
	Properties->SetObjectField(TEXT("description"), DescProp);

	// tags property - optional array
	TSharedPtr<FJsonObject> TagsProp = MakeShareable(new FJsonObject);
	TagsProp->SetStringField(TEXT("type"), TEXT("array"));
	TSharedPtr<FJsonObject> TagsItems = MakeShareable(new FJsonObject);
	TagsItems->SetStringField(TEXT("type"), TEXT("string"));
	TagsProp->SetObjectField(TEXT("items"), TagsItems);
	TagsProp->SetStringField(TEXT("description"),
		TEXT("Tags for categorizing and searching the script "
			 "(e.g., ['blueprint', 'variables', 'health'])."));
	Properties->SetObjectField(TEXT("tags"), TagsProp);

	// category property - optional string
	TSharedPtr<FJsonObject> CategoryProp = MakeShareable(new FJsonObject);
	CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	CategoryProp->SetStringField(TEXT("description"),
		TEXT("Category folder for organization (default: 'general'). "
			 "Examples: 'gameplay', 'ui', 'utilities', 'animation'."));
	Properties->SetObjectField(TEXT("category"), CategoryProp);

	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("name"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("code"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("description"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);

	Definition.InputSchema = Schema;

	return Definition;
}

FMcpToolCallResult FN2CMcpSavePythonScriptTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CMcpArgumentParser Parser(Arguments);

	FString Name, Code, Description;
	FString ErrorMsg;

	if (!Parser.TryGetRequiredString(TEXT("name"), Name, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}
	if (!Parser.TryGetRequiredString(TEXT("code"), Code, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}
	if (!Parser.TryGetRequiredString(TEXT("description"), Description, ErrorMsg))
	{
		return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
	}

	// Get optional parameters
	TArray<FString> Tags;
	const TArray<TSharedPtr<FJsonValue>>* TagsArray = Parser.GetOptionalArray(TEXT("tags"));
	if (TagsArray)
	{
		for (const auto& TagValue : *TagsArray)
		{
			FString TagStr;
			if (TagValue.IsValid() && TagValue->TryGetString(TagStr))
			{
				Tags.Add(TagStr);
			}
		}
	}
	FString Category = Parser.GetOptionalString(TEXT("category"), TEXT("general"));

	// Name and category become a file and a folder under the scripts directory: allow plain identifiers only,
	// never a reserved module name (init_unreal.py runs at every editor start).
	auto CheckIdentifier = [](const FString& Value, const TCHAR* What, FString& OutError) -> bool
	{
		bool bValid = Value.Len() > 0 && Value.Len() <= 64 && (FChar::IsAlpha(Value[0]) || Value[0] == TEXT('_'));
		for (TCHAR C : Value)
		{
			bValid &= (C < 128) && (FChar::IsAlnum(C) || C == TEXT('_'));
		}
		if (!bValid)
		{
			OutError = FString::Printf(TEXT("Invalid %s '%s': use letters, digits and underscores, starting with a letter or underscore (64 characters at most)"), What, *Value);
			return false;
		}
		if (Value.Equals(TEXT("init_unreal"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("__init__"), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("Invalid %s '%s': this name is reserved"), What, *Value);
			return false;
		}
		return true;
	};
	FString NameError;
	if (!CheckIdentifier(Name, TEXT("name"), NameError) || !CheckIdentifier(Category, TEXT("category"), NameError))
	{
		return FMcpToolCallResult::CreateErrorResult(NameError);
	}

	// Build Python function call
	// Note: Using triple-quoted string for code to handle multi-line scripts
	FString FunctionCall = FString::Printf(
		TEXT("save_script(\"%s\", \"\"\"%s\"\"\", \"%s\", tags=%s, category=\"%s\")"),
		*EscapePythonString(Name),
		*EscapePythonString(Code),
		*EscapePythonString(Description),
		*BuildPythonList(Tags),
		*EscapePythonString(Category)
	);

	return ExecuteNodeToCodeFunction(FunctionCall);
}
