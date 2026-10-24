// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpTranslateBlueprintTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h" // For REGISTER_MCP_TOOL
#include "Utils/N2CLogger.h"

REGISTER_MCP_TOOL(FN2CMcpTranslateBlueprintTool)

FMcpToolDefinition FN2CMcpTranslateBlueprintTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("translate-focused-blueprint"),
        TEXT("Translates the currently focused Blueprint graph using an LLM provider and returns the translation as this call's result. ")
        TEXT("It can take minutes: send params._meta.progressToken to receive notifications/progress while it runs."),
        TEXT("Translation")
    );

    Definition.bIsLongRunning = true; // Mark as long-running

    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

    TSharedPtr<FJsonObject> ProviderProp = MakeShareable(new FJsonObject);
    ProviderProp->SetStringField(TEXT("type"), TEXT("string"));
    ProviderProp->SetStringField(TEXT("description"), TEXT("Optional: LLM Provider ID (e.g., 'openai', 'anthropic', 'ollama'). Uses settings default if empty."));
    Properties->SetObjectField(TEXT("provider"), ProviderProp);

    TSharedPtr<FJsonObject> ModelProp = MakeShareable(new FJsonObject);
    ModelProp->SetStringField(TEXT("type"), TEXT("string"));
    ModelProp->SetStringField(TEXT("description"), TEXT("Optional: Specific model ID. Uses provider's default from settings if empty."));
    Properties->SetObjectField(TEXT("model"), ModelProp);

    TSharedPtr<FJsonObject> LanguageProp = MakeShareable(new FJsonObject);
    LanguageProp->SetStringField(TEXT("type"), TEXT("string"));
    LanguageProp->SetStringField(TEXT("description"), TEXT("Optional: Target language ID (e.g., 'cpp', 'python'). Uses settings default if empty."));
    Properties->SetObjectField(TEXT("language"), LanguageProp);
    
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    Schema->SetObjectField(TEXT("properties"), Properties);
    // No required fields, all are optional and will use defaults from settings.
    
    Definition.InputSchema = Schema;

    return Definition;
}

FMcpToolCallResult FN2CMcpTranslateBlueprintTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // The MCP server runs this tool as an async task on the call's own HTTP response (FN2CMcpHttpServerManager). It
    // reaches Execute() only when the call came inside a JSON-RPC batch, which is answered as a whole and cannot wait.
    FN2CLogger::Get().LogWarning(TEXT("translate-focused-blueprint was called inside a JSON-RPC batch; it only runs as a single request."));

    return FMcpToolCallResult::CreateErrorResult(
        TEXT("translate-focused-blueprint cannot run inside a JSON-RPC batch. Send it as a single tools/call request.")
    );
}
