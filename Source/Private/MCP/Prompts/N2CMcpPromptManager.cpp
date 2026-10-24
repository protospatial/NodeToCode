// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Prompts/N2CMcpPromptManager.h"
#include "Utils/N2CLogger.h"
#include "Async/Async.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"

FN2CMcpPromptManager& FN2CMcpPromptManager::Get()
{
	static FN2CMcpPromptManager Instance;
	return Instance;
}

bool FN2CMcpPromptManager::RegisterPrompt(const FMcpPromptDefinition& Definition, 
                                         const FMcpPromptGetDelegate& Handler,
                                         bool bRequiresGameThread)
{
	if (Definition.Name.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register prompt with empty name"));
		return false;
	}

	if (!Handler.IsBound())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register prompt with unbound handler"));
		return false;
	}

	FScopeLock Lock(&PromptLock);

	if (Prompts.Contains(Definition.Name))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Prompt already registered: %s"), *Definition.Name));
		return false;
	}

	FMcpPromptEntry Entry;
	Entry.Definition = Definition;
	Entry.Handler = Handler;
	Entry.bRequiresGameThread = bRequiresGameThread;

	Prompts.Add(Definition.Name, Entry);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered prompt: %s"), *Definition.Name), EN2CLogSeverity::Info);
	
	return true;
}

bool FN2CMcpPromptManager::UnregisterPrompt(const FString& Name)
{
	FScopeLock Lock(&PromptLock);

	if (Prompts.Remove(Name) > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Unregistered prompt: %s"), *Name), EN2CLogSeverity::Info);
		return true;
	}

	return false;
}

TArray<FMcpPromptDefinition> FN2CMcpPromptManager::ListPrompts(const FString& Cursor) const
{
	FScopeLock Lock(&PromptLock);

	TArray<FMcpPromptDefinition> PromptList;
	
	for (const auto& Pair : Prompts)
	{
		PromptList.Add(Pair.Value.Definition);
	}
	
	// TODO: Implement cursor-based pagination if needed
	
	return PromptList;
}

FMcpPromptResult FN2CMcpPromptManager::GetPrompt(const FString& Name, const FMcpPromptArguments& Arguments)
{
	FMcpPromptResult Result;

	FMcpPromptEntry Entry;
	{
		FScopeLock Lock(&PromptLock);
		
		if (const FMcpPromptEntry* FoundEntry = Prompts.Find(Name))
		{
			Entry = *FoundEntry;
		}
		else
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Prompt not found: %s"), *Name));
			
			// MCP 2025-11-25, server/prompts, error handling: "Invalid prompt name: -32602 (Invalid params)"
			Result.ErrorCode = JsonRpcErrorCodes::InvalidParams;
			Result.ErrorMessage = FString::Printf(TEXT("Unknown prompt: %s"), *Name);
			return Result;
		}
	}

	// Validate arguments
	FString ValidationError;
	if (!ValidatePromptArguments(Name, Arguments, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Invalid arguments for prompt %s: %s"), *Name, *ValidationError));
		
		// MCP 2025-11-25, server/prompts, error handling: "Missing required arguments: -32602 (Invalid params)"
		Result.ErrorCode = JsonRpcErrorCodes::InvalidParams;
		Result.ErrorMessage = FString::Printf(TEXT("Invalid arguments for prompt '%s': %s"), *Name, *ValidationError);
		return Result;
	}

	// Execute handler
	if (Entry.Handler.IsBound())
	{
		if (Entry.bRequiresGameThread && !IsInGameThread())
		{
			// Execute on game thread and wait for result
			TPromise<FMcpPromptResult> Promise;
			TFuture<FMcpPromptResult> Future = Promise.GetFuture();
			
			AsyncTask(ENamedThreads::GameThread, [Handler = Entry.Handler, Arguments, Promise = MoveTemp(Promise)]() mutable
			{
				Promise.SetValue(Handler.Execute(Arguments));
			});
			
			// Wait for result with timeout
			if (Future.WaitFor(FTimespan::FromSeconds(5.0)))
			{
				Result = Future.Get();
			}
			else
			{
				FN2CLogger::Get().LogError(FString::Printf(TEXT("Timeout getting prompt on game thread: %s"), *Name));
				// MCP 2025-11-25, server/prompts, error handling: "Internal errors: -32603 (Internal error)"
				Result.ErrorCode = JsonRpcErrorCodes::InternalError;
				Result.ErrorMessage = FString::Printf(TEXT("Timed out getting prompt '%s' on the game thread"), *Name);
			}
		}
		else
		{
			Result = Entry.Handler.Execute(Arguments);
		}
	}

	return Result;
}

bool FN2CMcpPromptManager::IsPromptRegistered(const FString& Name) const
{
	FScopeLock Lock(&PromptLock);
	return Prompts.Contains(Name);
}

bool FN2CMcpPromptManager::ValidatePromptArguments(const FString& Name, const FMcpPromptArguments& Arguments, FString& OutError) const
{
	FScopeLock Lock(&PromptLock);
	
	const FMcpPromptEntry* Entry = Prompts.Find(Name);
	if (!Entry)
	{
		OutError = FString::Printf(TEXT("Prompt not found: %s"), *Name);
		return false;
	}

	// Check required arguments
	for (const FMcpPromptArgument& Arg : Entry->Definition.Arguments)
	{
		if (Arg.bRequired && !Arguments.Contains(Arg.Name))
		{
			OutError = FString::Printf(TEXT("Missing required argument: %s"), *Arg.Name);
			return false;
		}
	}

	// Check for unknown arguments
	for (const auto& ArgPair : Arguments)
	{
		bool bFound = false;
		for (const FMcpPromptArgument& DefArg : Entry->Definition.Arguments)
		{
			if (DefArg.Name == ArgPair.Key)
			{
				bFound = true;
				break;
			}
		}
		
		if (!bFound)
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Unknown argument '%s' for prompt '%s'"), *ArgPair.Key, *Name));
			// We'll allow unknown arguments but log a warning
		}
	}

	return true;
}

void FN2CMcpPromptManager::ClearAllPrompts()
{
	FScopeLock Lock(&PromptLock);
	
	Prompts.Empty();
	
	FN2CLogger::Get().Log(TEXT("Cleared all registered prompts"), EN2CLogSeverity::Info);
}