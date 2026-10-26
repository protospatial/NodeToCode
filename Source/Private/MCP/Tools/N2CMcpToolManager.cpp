// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CUserSettings.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"

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

	// The definition is built once, here, and served to tools/list and every tools/call from then on
	const TSharedRef<FMcpToolDefinition> Definition = MakeShared<FMcpToolDefinition>(Tool->GetDefinition());
	const FString& ToolName = Definition->Name;
	if (ToolName.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register tool with empty name"));
		return false;
	}

	// Python execution tools exist only when the user opted in (per-user setting, off by default; a committed
	// project config cannot turn it on, card 0423).
	// Every registration path (startup, dynamic discovery) goes through here.
	if (UN2CUserSettings::IsPythonExecutionTool(ToolName) && !UN2CUserSettings::IsMcpPythonExecutionAllowed())
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

	FRegisteredTool& Entry = RegisteredTools.Add(ToolName);
	Entry.Tool = Tool;
	Entry.Definition = Definition;

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

TArray<FMcpToolDefinition> FN2CMcpToolManager::GetAllToolDefinitions() const
{
	FScopeLock Lock(&ToolsLock);

	TArray<FMcpToolDefinition> Definitions;
	Definitions.Reserve(RegisteredTools.Num());

	for (const auto& Pair : RegisteredTools)
	{
		Definitions.Add(*Pair.Value.Definition);
	}

	// TMap order is not stable across registrations: sort by name (ordinal), so tools/list is deterministic
	Definitions.Sort([](const FMcpToolDefinition& A, const FMcpToolDefinition& B)
	{
		return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
	});

	return Definitions;
}

TSharedPtr<const FMcpToolDefinition> FN2CMcpToolManager::GetToolDefinition(const FString& ToolName) const
{
	FScopeLock Lock(&ToolsLock);
	const FRegisteredTool* Entry = RegisteredTools.Find(ToolName);
	return Entry ? Entry->Definition : nullptr;
}

FMcpToolCallResult FN2CMcpToolManager::ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
	FScopeLock Lock(&ToolsLock);

	const FRegisteredTool* Entry = RegisteredTools.Find(ToolName);
	if (!Entry || !Entry->Tool.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
	}

	// Copies of the shared pointers: assess-needed-tools replaces the active set (and this entry) while it runs
	const TSharedPtr<IN2CMcpTool> Tool = Entry->Tool;
	const TSharedPtr<const FMcpToolDefinition> Definition = Entry->Definition;

	// A long-running tool is answered on its own tools/call request (FN2CMcpProtocol); it reaches this
	// synchronous path only inside a JSON-RPC batch, which is answered as a whole and cannot wait for it.
	if (Definition->bIsLongRunning)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("%s was called inside a JSON-RPC batch; it only runs as a single request."), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(
			TEXT("%s cannot run inside a JSON-RPC batch. Send it as a single tools/call request."), *ToolName));
	}

	// Execute the tool handler
	FN2CLogger::Get().Log(FString::Printf(TEXT("Executing MCP tool: %s"), *ToolName), EN2CLogSeverity::Debug);

	try
	{
		FMcpToolCallResult Result = Tool->Execute(Arguments);
		if (Definition->bReturnsUntrustedContent)
		{
			// Blueprint text (comments, tooltips, string literals) is data for the agent, never instructions (card 0124)
			FN2CMcpUntrustedContent::MarkToolResult(Result);
		}
		return Result;
	}
	catch (...)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Exception thrown while executing tool '%s'"), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Internal error executing tool '%s'"), *ToolName));
	}
}

TSharedPtr<IN2CMcpTool> FN2CMcpToolManager::FindTool(const FString& ToolName) const
{
	FScopeLock Lock(&ToolsLock);
	const FRegisteredTool* Entry = RegisteredTools.Find(ToolName);
	return Entry ? Entry->Tool : nullptr;
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

	// The protocol core sends notifications/tools/list_changed to its clients
	ToolListChanged.Broadcast();

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