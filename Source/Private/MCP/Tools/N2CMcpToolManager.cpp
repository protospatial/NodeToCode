// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CUserSettings.h"

FN2CMcpToolManager& FN2CMcpToolManager::Get()
{
	static FN2CMcpToolManager Instance;
	return Instance;
}

bool FN2CMcpToolManager::RegisterTool(TSharedPtr<IN2CMcpTool> Tool)
{
	if (!Tool.IsValid())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register null tool"));
		return false;
	}

	const FString& ToolName = Tool->GetDefinition().Name;
	if (ToolName.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register tool with empty name"));
		return false;
	}

	// Python execution tools exist only when the user opted in (per-user setting, off by default).
	// Every registration path (startup, dynamic discovery) goes through here.
	if (UN2CUserSettings::IsPythonExecutionTool(ToolName) && !GetDefault<UN2CUserSettings>()->bAllowMcpPythonExecution)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Skipping MCP tool '%s': Allow MCP Python Execution is off"), *ToolName), EN2CLogSeverity::Debug);
		return false;
	}

	FScopeLock Lock(&ToolsLock);

	if (RegisteredTools.Contains(ToolName))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tool '%s' is already registered"), *ToolName));
		return false;
	}

	RegisteredTools.Add(ToolName, Tool);

	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered active MCP tool: %s"), *ToolName), EN2CLogSeverity::Debug);
	return true;
}

bool FN2CMcpToolManager::UnregisterTool(const FString& ToolName)
{
	FScopeLock Lock(&ToolsLock);

	if (RegisteredTools.Remove(ToolName) > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Unregistered MCP tool: %s"), *ToolName), EN2CLogSeverity::Info);
		return true;
	}

	return false;
}

const FMcpToolDefinition* FN2CMcpToolManager::GetToolDefinition(const FString& ToolName) const
{
	// This method can't return a pointer to a temporary, so we need to find a different approach
	// For now, return nullptr - callers should use GetAllToolDefinitions instead
	return nullptr;
}

TArray<FMcpToolDefinition> FN2CMcpToolManager::GetAllToolDefinitions() const
{
	FScopeLock Lock(&ToolsLock);

	TArray<FMcpToolDefinition> Definitions;
	Definitions.Reserve(RegisteredTools.Num());

	for (const auto& Pair : RegisteredTools)
	{
		Definitions.Add(Pair.Value->GetDefinition());
	}

	return Definitions;
}

FMcpToolCallResult FN2CMcpToolManager::ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
	FScopeLock Lock(&ToolsLock);

	const TSharedPtr<IN2CMcpTool>* ToolPtr = RegisteredTools.Find(ToolName);
	if (!ToolPtr || !ToolPtr->IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
	}

	// Execute the tool handler
	FN2CLogger::Get().Log(FString::Printf(TEXT("Executing MCP tool: %s"), *ToolName), EN2CLogSeverity::Debug);
	
	try
	{
		return (*ToolPtr)->Execute(Arguments);
	}
	catch (...)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Exception thrown while executing tool '%s'"), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Internal error executing tool '%s'"), *ToolName));
	}
}

bool FN2CMcpToolManager::IsToolRegistered(const FString& ToolName) const
{
	FScopeLock Lock(&ToolsLock);
	return RegisteredTools.Contains(ToolName);
}

void FN2CMcpToolManager::UpdateActiveTools(const TArray<FString>& Categories)
{
	FScopeLock Lock(&ToolsLock);

	RegisteredTools.Empty();
	const auto& AllTools = FN2CMcpToolRegistry::Get().GetTools();

	for (const auto& Tool : AllTools)
	{
		if (!Tool.IsValid()) continue;

		const auto& Def = Tool->GetDefinition();
		// Always register assess-needed-tools, or any tool whose category is in the requested list.
		if (Def.Name == TEXT("assess-needed-tools") || Categories.Contains(Def.Category))
		{
			RegisterTool(Tool);
		}
	}

	// Send notification that the tool list has changed
	FJsonRpcNotification Notification;
	Notification.Method = TEXT("notifications/tools/list_changed");
	Notification.Params = MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()); // Empty params
	FN2CMcpHttpServerManager::Get().BroadcastNotification(Notification);

	FN2CLogger::Get().Log(FString::Printf(TEXT("Updated active toolset. Now have %d tools."), RegisteredTools.Num()), EN2CLogSeverity::Info);
}

void FN2CMcpToolManager::SetDefaultToolSet()
{
	FScopeLock Lock(&ToolsLock);

	RegisteredTools.Empty();
	const auto& AllTools = FN2CMcpToolRegistry::Get().GetTools();

	for (const auto& Tool : AllTools)
	{
		if (Tool.IsValid() && Tool->GetDefinition().Name == TEXT("assess-needed-tools"))
		{
			RegisterTool(Tool);
			break; // Found it, we're done
		}
	}
	FN2CLogger::Get().Log(TEXT("Tool manager set to default toolset (assess-needed-tools only)."), EN2CLogSeverity::Info);
}

void FN2CMcpToolManager::RegisterAllToolsExceptAssess()
{
	FScopeLock Lock(&ToolsLock);

	RegisteredTools.Empty();
	const auto& AllTools = FN2CMcpToolRegistry::Get().GetTools();

	for (const auto& Tool : AllTools)
	{
		if (Tool.IsValid() && Tool->GetDefinition().Name != TEXT("assess-needed-tools"))
		{
			RegisterTool(Tool);
		}
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered all tools except assess-needed-tools. Total tools: %d"), RegisteredTools.Num()), EN2CLogSeverity::Info);
}

void FN2CMcpToolManager::RegisterToolsByName(const TArray<FString>& ToolNames)
{
	FScopeLock Lock(&ToolsLock);

	RegisteredTools.Empty();
	const auto& AllTools = FN2CMcpToolRegistry::Get().GetTools();

	int32 RegisteredCount = 0;
	for (const auto& Tool : AllTools)
	{
		if (Tool.IsValid())
		{
			const FString& ToolName = Tool->GetDefinition().Name;
			if (ToolNames.Contains(ToolName))
			{
				RegisterTool(Tool);
				RegisteredCount++;
			}
		}
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered %d/%d requested tools for Python script-only mode"),
		RegisteredCount, ToolNames.Num()), EN2CLogSeverity::Info);

	// Log any tools that weren't found
	for (const FString& RequestedName : ToolNames)
	{
		if (!RegisteredTools.Contains(RequestedName))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Requested tool '%s' not found in registry"), *RequestedName));
		}
	}
}

void FN2CMcpToolManager::ClearAllTools()
{
	FScopeLock Lock(&ToolsLock);
	RegisteredTools.Empty();
	FN2CLogger::Get().Log(TEXT("Cleared all registered MCP tools"), EN2CLogSeverity::Info);
}