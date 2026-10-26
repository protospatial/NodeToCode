// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "MCP/Tools/N2CMcpEpicOverlap.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CUserSettings.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "MCP/Utils/N2CMcpPlaySessionGuard.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const TCHAR* const FN2CMcpToolManager::ListToolsetsName = TEXT("list_toolsets");
const TCHAR* const FN2CMcpToolManager::DescribeToolsetName = TEXT("describe_toolset");
const TCHAR* const FN2CMcpToolManager::CallToolName = TEXT("call_tool");

namespace N2CMcpToolSearch
{
	/** Base of the three meta-tools: each answers from the manager that built it */
	class FMetaTool : public FN2CMcpToolBase
	{
	public:
		explicit FMetaTool(const FN2CMcpToolManager& InOwner) : Owner(InOwner) {}

	protected:
		/** Shared by the three definitions: kept loaded by clients that defer tools (they are the only way in) */
		static FMcpToolDefinition MakeDefinition(const TCHAR* Name, const TCHAR* Description, const TSharedPtr<FJsonObject>& Schema)
		{
			FMcpToolDefinition Definition(Name, Description);
			Definition.InputSchema = Schema;
			Definition.bAlwaysLoad = true;
			return Definition;
		}

		const FN2CMcpToolManager& Owner;
	};

	class FListToolsetsTool final : public FMetaTool
	{
	public:
		using FMetaTool::FMetaTool;

		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition = MakeDefinition(FN2CMcpToolManager::ListToolsetsName,
				TEXT("List NodeToCode's toolsets with their names and descriptions. Call describe_toolset on one to see its tools, then call_tool to run a tool."),
				BuildEmptyObjectSchema());
			SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			return FMcpToolCallResult::CreateTextResult(Owner.ListToolsets());
		}
	};

	class FDescribeToolsetTool final : public FMetaTool
	{
	public:
		using FMetaTool::FMetaTool;

		virtual FMcpToolDefinition GetDefinition() const override
		{
			TMap<FString, FString> Properties;
			Properties.Add(TEXT("toolset_name"), TEXT("string"));
			TMap<FString, FString> Descriptions;
			Descriptions.Add(TEXT("toolset_name"), TEXT("Name of the toolset to describe, as list_toolsets shows it."));
			FMcpToolDefinition Definition = MakeDefinition(FN2CMcpToolManager::DescribeToolsetName,
				TEXT("Get a toolset's tools: each tool's name, description, input schema and behaviour hints, as JSON. Use list_toolsets for the toolset names."),
				BuildInputSchema(Properties, { TEXT("toolset_name") }, Descriptions));
			SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			FString ToolsetName;
			if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("toolset_name"), ToolsetName) || ToolsetName.IsEmpty())
			{
				return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: toolset_name"));
			}
			const TValueOrError<FString, FString> Description = Owner.DescribeToolset(ToolsetName);
			return Description.HasValue() ? FMcpToolCallResult::CreateTextResult(Description.GetValue())
				: FMcpToolCallResult::CreateErrorResult(Description.GetError());
		}
	};

	class FCallToolTool final : public FMetaTool
	{
	public:
		FCallToolTool(const FN2CMcpToolManager& InOwner, FN2CMcpToolManager& InExecutor)
			: FMetaTool(InOwner), Executor(InExecutor) {}

		virtual FMcpToolDefinition GetDefinition() const override
		{
			TMap<FString, FString> Properties;
			Properties.Add(TEXT("toolset_name"), TEXT("string"));
			Properties.Add(TEXT("tool_name"), TEXT("string"));
			Properties.Add(TEXT("arguments"), TEXT("object"));
			TMap<FString, FString> Descriptions;
			Descriptions.Add(TEXT("toolset_name"), TEXT("Optional. Name of the toolset that holds the tool, as list_toolsets shows it."));
			Descriptions.Add(TEXT("tool_name"), TEXT("Name of the tool to call, as describe_toolset shows it."));
			Descriptions.Add(TEXT("arguments"), TEXT("Arguments for the tool, matching the input schema describe_toolset shows. Defaults to an empty object."));
			const TSharedPtr<FJsonObject> Schema = BuildInputSchema(Properties, { TEXT("tool_name") }, Descriptions);
			// The called tool decides what the call does, so no behaviour hint is claimed (the MCP defaults apply)
			FMcpToolDefinition Definition = MakeDefinition(FN2CMcpToolManager::CallToolName,
				TEXT("Call a NodeToCode tool by name with its arguments and return its result. Use list_toolsets and describe_toolset to find tool names and input schemas. Long-running tools report progress and can be cancelled as when called directly."),
				Schema);
			// Not refused during Play In Editor itself: the tool it calls goes through ExecuteTool (or the async task
			// manager) and is refused there when its own definition says so (card 0170)
			Definition.bAllowedDuringPlaySession = true;
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			FString ToolName;
			TSharedPtr<FJsonObject> ToolArguments;
			FString Error;
			if (!Owner.ResolveCallTool(Arguments, ToolName, ToolArguments, Error))
			{
				return FMcpToolCallResult::CreateErrorResult(Error);
			}
			return Executor.ExecuteTool(ToolName, ToolArguments);
		}

	private:
		FN2CMcpToolManager& Executor;
	};

	/** Why a tool left to Unreal MCP (card 0174) is not served, and what to call instead */
	FString LeftToUnrealMcpText(const FString& ToolName)
	{
		const TCHAR* Counterpart = N2CMcpEpicOverlap::FindEpicCounterpart(ToolName);
		return FString::Printf(TEXT("%s is left to Unreal MCP in this editor: use %s from Unreal MCP (UE 5.8's EditorToolset). Project Settings > Plugins > Node to Code > MCP Server > Overlapping Tools serves it again."),
			*ToolName, Counterpart ? Counterpart : TEXT("Epic's tool"));
	}

	/** "A, B, C" */
	FString JoinToolsetNames(const TArray<const FN2CMcpToolset*>& Toolsets)
	{
		TArray<FString> Names;
		for (const FN2CMcpToolset* Toolset : Toolsets)
		{
			Names.Add(Toolset->Name);
		}
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
	}
}

TArray<FString> FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile Profile, const TArray<FString>& CustomToolsets)
{
	TArray<FString> Names;
	switch (Profile)
	{
	case EN2CMcpToolProfile::PythonScripting:
		Names.Add(N2CMcpToolsets::Python.Name);
		break;
	case EN2CMcpToolProfile::Custom:
		Names = CustomToolsets;
		break;
	case EN2CMcpToolProfile::DefaultToolsets:
	default:
		for (const FN2CMcpToolset* Toolset : N2CMcpToolsets::All)
		{
			if (Toolset->bDefaultEnabled)
			{
				Names.Add(Toolset->Name);
			}
		}
		break;
	}
	return Names;
}

FN2CMcpToolManager& FN2CMcpToolManager::Get()
{
	static FN2CMcpToolManager Instance;
	return Instance;
}

FN2CMcpToolManager::FN2CMcpToolManager()
{
	using namespace N2CMcpToolSearch;
	const TSharedPtr<IN2CMcpTool> Meta[] = {
		MakeShared<FListToolsetsTool>(*this), MakeShared<FDescribeToolsetTool>(*this), MakeShared<FCallToolTool>(*this, *this) };
	for (const TSharedPtr<IN2CMcpTool>& Tool : Meta)
	{
		FRegisteredTool& Entry = MetaTools.Add(Tool->GetDefinition().Name);
		Entry.Tool = Tool;
		Entry.Definition = MakeShared<FMcpToolDefinition>(Tool->GetDefinition());
	}
}

TArray<FString> FN2CMcpToolManager::Configure(const FN2CMcpToolConfig& Config)
{
	TArray<FString> Unknown;
	TArray<const FN2CMcpToolset*> Enabled;
	for (const FString& Name : Config.Toolsets)
	{
		if (const FN2CMcpToolset* Toolset = N2CMcpToolsets::Find(Name))
		{
			Enabled.AddUnique(Toolset);
		}
		else
		{
			Unknown.Add(Name);
		}
	}

	TArray<FString> ListedBefore;
	bool bBroadcast = false;
	{
		FScopeLock Lock(&ToolsLock);
		for (const FMcpToolDefinition& Definition : GetAllToolDefinitions())
		{
			ListedBefore.Add(Definition.Name);
		}

		EnabledToolsets = Enabled;
		Exposure = Config.Exposure;
		bLeaveOverlappingToUnrealMcp = Config.bLeaveOverlappingToUnrealMcp;
		LeftToUnrealMcp.Empty();
		RegisteredTools.Empty();
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (!Tool.IsValid())
			{
				continue;
			}
			const FMcpToolDefinition Definition = Tool->GetDefinition();
			if (!Definition.Toolset)
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP tool '%s' names no toolset, so it is never served"), *Definition.Name));
			}
			else if (!EnabledToolsets.Contains(Definition.Toolset))
			{
				continue;
			}
			else if (bLeaveOverlappingToUnrealMcp && N2CMcpEpicOverlap::FindEpicCounterpart(Definition.Name))
			{
				// Epic's EditorToolset does the same through Unreal MCP in this editor (card 0174)
				LeftToUnrealMcp.Add(Definition.Name, Definition.Toolset);
			}
			else
			{
				RegisterTool(Tool);
			}
		}

		TArray<FString> ListedAfter;
		for (const FMcpToolDefinition& Definition : GetAllToolDefinitions())
		{
			ListedAfter.Add(Definition.Name);
		}
		bBroadcast = bConfigured && ListedAfter != ListedBefore;
		bConfigured = true;

		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP tools: %d tools served from toolsets %s%s; tools/list shows %s"),
			RegisteredTools.Num(), *N2CMcpToolSearch::JoinToolsetNames(EnabledToolsets),
			LeftToUnrealMcp.Num() > 0 ? *FString::Printf(TEXT(" (%d left to Unreal MCP)"), LeftToUnrealMcp.Num()) : TEXT(""),
			Exposure == EN2CMcpToolExposure::ToolSearch ? TEXT("list_toolsets, describe_toolset and call_tool (tool search)") : TEXT("every served tool")),
			EN2CLogSeverity::Info);
	}

	if (bBroadcast)
	{
		// Outside the lock: the protocol core's handler may read the tool list
		ToolListChanged.Broadcast();
	}
	return Unknown;
}

FN2CMcpToolConfig FN2CMcpToolManager::GetConfig() const
{
	FScopeLock Lock(&ToolsLock);
	FN2CMcpToolConfig Config;
	Config.Exposure = Exposure;
	Config.bLeaveOverlappingToUnrealMcp = bLeaveOverlappingToUnrealMcp;
	for (const FN2CMcpToolset* Toolset : EnabledToolsets)
	{
		Config.Toolsets.Add(Toolset->Name);
	}
	return Config;
}

TArray<FString> FN2CMcpToolManager::GetToolsLeftToUnrealMcp() const
{
	FScopeLock Lock(&ToolsLock);
	TArray<FString> Names;
	LeftToUnrealMcp.GetKeys(Names);
	Names.Sort();
	return Names;
}

FString FN2CMcpToolManager::LeftToUnrealMcpHint(const FString& ToolName) const
{
	{
		FScopeLock Lock(&ToolsLock);
		if (!LeftToUnrealMcp.Contains(ToolName))
		{
			return FString();
		}
	}
	return N2CMcpToolSearch::LeftToUnrealMcpText(ToolName);
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
	// A tool refused during Play In Editor says so in its description (card 0170)
	FN2CMcpPlaySessionGuard::AddRuleToDescription(*Definition);
	const FString& ToolName = Definition->Name;
	if (ToolName.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register tool with empty name"));
		return false;
	}
	if (IsMetaTool(ToolName))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Cannot register tool '%s': the name belongs to a tool-search meta-tool"), *ToolName));
		return false;
	}

	// Python execution tools exist only when the user opted in (per-user setting, off by default; a committed
	// project config cannot turn it on, card 0423). Every registration path (Configure, specs) goes through here.
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

	const TMap<FString, FRegisteredTool>& Listed = Exposure == EN2CMcpToolExposure::ToolSearch ? MetaTools : RegisteredTools;
	TArray<FMcpToolDefinition> Definitions;
	Definitions.Reserve(Listed.Num());
	for (const auto& Pair : Listed)
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

FN2CMcpToolManager::FRegisteredTool FN2CMcpToolManager::FindEntry(const FString& ToolName) const
{
	FScopeLock Lock(&ToolsLock);
	if (const FRegisteredTool* Entry = RegisteredTools.Find(ToolName))
	{
		return *Entry;
	}
	if (const FRegisteredTool* Meta = MetaTools.Find(ToolName))
	{
		return *Meta;
	}
	return FRegisteredTool();
}

TSharedPtr<const FMcpToolDefinition> FN2CMcpToolManager::GetToolDefinition(const FString& ToolName) const
{
	return FindEntry(ToolName).Definition;
}

FMcpToolCallResult FN2CMcpToolManager::ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
	// Copies of the shared pointers, taken under the lock; the tool runs without it, so call_tool (and anything the
	// tool waits for on the game thread) can use the manager
	const FRegisteredTool Entry = FindEntry(ToolName);
	if (!Entry.Tool.IsValid() || !Entry.Definition.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Tool '%s' not found"), *ToolName));
	}

	// A long-running tool is answered on its own tools/call request (FN2CMcpProtocol, also through call_tool); it
	// reaches this synchronous path only inside a JSON-RPC batch, which is answered as a whole and cannot wait for it.
	if (Entry.Definition->bIsLongRunning)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("%s was called inside a JSON-RPC batch; it only runs as a single request."), *ToolName));
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(
			TEXT("%s cannot run inside a JSON-RPC batch. Send it as a single tools/call request."), *ToolName));
	}

	// Tools that change a Blueprint, an asset or a level wait until Play In Editor ends (card 0170); call_tool is
	// allowed itself and comes back here for the tool it calls, which is judged by its own definition
	if (TOptional<FMcpToolCallResult> Refused = FN2CMcpPlaySessionGuard::CheckCall(*Entry.Definition))
	{
		return MoveTemp(Refused.GetValue());
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Executing MCP tool: %s"), *ToolName), EN2CLogSeverity::Debug);

	try
	{
		FMcpToolCallResult Result = Entry.Tool->Execute(Arguments);
		if (Entry.Definition->bReturnsUntrustedContent)
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
	return FindEntry(ToolName).Tool;
}

bool FN2CMcpToolManager::IsToolRegistered(const FString& ToolName) const
{
	FScopeLock Lock(&ToolsLock);
	return RegisteredTools.Contains(ToolName) || MetaTools.Contains(ToolName);
}

bool FN2CMcpToolManager::IsToolListed(const FString& ToolName) const
{
	FScopeLock Lock(&ToolsLock);
	return Exposure == EN2CMcpToolExposure::ToolSearch ? MetaTools.Contains(ToolName) : RegisteredTools.Contains(ToolName);
}

bool FN2CMcpToolManager::IsMetaTool(const FString& ToolName)
{
	return ToolName == ListToolsetsName || ToolName == DescribeToolsetName || ToolName == CallToolName;
}

TArray<const FN2CMcpToolset*> FN2CMcpToolManager::ListedToolsetsLocked() const
{
	TArray<const FN2CMcpToolset*> Listed;
	for (const FN2CMcpToolset* Toolset : N2CMcpToolsets::All)
	{
		if (!EnabledToolsets.Contains(Toolset))
		{
			continue;
		}
		for (const auto& Pair : RegisteredTools)
		{
			if (Pair.Value.Definition->Toolset == Toolset)
			{
				Listed.Add(Toolset);
				break;
			}
		}
	}
	return Listed;
}

FString FN2CMcpToolManager::UnknownToolsetErrorLocked(const FString& ToolsetName) const
{
	const FString Valid = N2CMcpToolSearch::JoinToolsetNames(ListedToolsetsLocked());
	const FN2CMcpToolset* Declared = N2CMcpToolsets::Find(ToolsetName);
	if (Declared && EnabledToolsets.Contains(Declared))
	{
		// Enabled, but every tool it has here was left to Unreal MCP (card 0174)
		TArray<FString> Left;
		for (const auto& Pair : LeftToUnrealMcp)
		{
			if (Pair.Value == Declared)
			{
				Left.Add(FString::Printf(TEXT("%s (%s)"), *Pair.Key, N2CMcpEpicOverlap::FindEpicCounterpart(Pair.Key)));
			}
		}
		Left.Sort();
		return FString::Printf(TEXT("Toolset '%s' is left to Unreal MCP in this editor, whose EditorToolset does the same: %s. Project Settings > Plugins > Node to Code > MCP Server > Overlapping Tools serves it again. Toolsets: %s"),
			Declared->Name, *FString::Join(Left, TEXT(", ")), *Valid);
	}
	if (Declared)
	{
		return FString::Printf(TEXT("Toolset '%s' is not enabled in this project (Project Settings > Plugins > Node to Code > MCP Server > Tool Profile). Toolsets: %s"),
			*ToolsetName, *Valid);
	}
	return FString::Printf(TEXT("Unknown toolset '%s'. Toolsets: %s"), *ToolsetName, *Valid);
}

FString FN2CMcpToolManager::ListToolsets() const
{
	FScopeLock Lock(&ToolsLock);
	FString Result;
	for (const FN2CMcpToolset* Toolset : ListedToolsetsLocked())
	{
		Result += FString::Printf(TEXT("- %s: %s\n"), Toolset->Name, Toolset->Description);
	}
	return Result.IsEmpty() ? FString(TEXT("No toolsets are enabled.\n")) : Result;
}

TValueOrError<FString, FString> FN2CMcpToolManager::DescribeToolset(const FString& ToolsetName) const
{
	FScopeLock Lock(&ToolsLock);
	const FN2CMcpToolset* Toolset = N2CMcpToolsets::Find(ToolsetName);
	if (!Toolset || !ListedToolsetsLocked().Contains(Toolset))
	{
		return MakeError(UnknownToolsetErrorLocked(ToolsetName));
	}

	TArray<TSharedPtr<const FMcpToolDefinition>> Definitions;
	for (const auto& Pair : RegisteredTools)
	{
		if (Pair.Value.Definition->Toolset == Toolset)
		{
			Definitions.Add(Pair.Value.Definition);
		}
	}
	Definitions.Sort([](const TSharedPtr<const FMcpToolDefinition>& A, const TSharedPtr<const FMcpToolDefinition>& B)
	{
		return A->Name.Compare(B->Name, ESearchCase::CaseSensitive) < 0;
	});

	TArray<TSharedPtr<FJsonValue>> Tools;
	for (const TSharedPtr<const FMcpToolDefinition>& Definition : Definitions)
	{
		Tools.Add(MakeShared<FJsonValueObject>(Definition->ToJson()));
	}
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Toolset->Name);
	Json->SetStringField(TEXT("description"), Toolset->Description);
	Json->SetArrayField(TEXT("tools"), Tools);

	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	return MakeValue(MoveTemp(Text));
}

bool FN2CMcpToolManager::ResolveCallTool(const TSharedPtr<FJsonObject>& CallArguments, FString& OutToolName,
	TSharedPtr<FJsonObject>& OutArguments, FString& OutError) const
{
	OutToolName.Reset();
	OutArguments.Reset();
	OutError.Reset();

	FString ToolName;
	if (!CallArguments.IsValid() || !CallArguments->TryGetStringField(TEXT("tool_name"), ToolName) || ToolName.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: tool_name");
		return false;
	}
	if (IsMetaTool(ToolName))
	{
		OutError = FString::Printf(TEXT("call_tool cannot call %s; call it directly"), *ToolName);
		return false;
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	if (const TSharedPtr<FJsonValue> ArgumentsValue = CallArguments->TryGetField(TEXT("arguments")))
	{
		if (ArgumentsValue->Type == EJson::Object)
		{
			Arguments = ArgumentsValue->AsObject();
		}
		else if (ArgumentsValue->Type != EJson::Null)
		{
			OutError = TEXT("arguments must be a JSON object");
			return false;
		}
	}

	FString ToolsetName;
	CallArguments->TryGetStringField(TEXT("toolset_name"), ToolsetName);

	FScopeLock Lock(&ToolsLock);
	const FRegisteredTool* Entry = RegisteredTools.Find(ToolName);
	if (!ToolsetName.IsEmpty())
	{
		const FN2CMcpToolset* Toolset = N2CMcpToolsets::Find(ToolsetName);
		if (!Toolset || !ListedToolsetsLocked().Contains(Toolset))
		{
			OutError = UnknownToolsetErrorLocked(ToolsetName);
			return false;
		}
		if (!Entry || Entry->Definition->Toolset != Toolset)
		{
			TArray<FString> Names;
			for (const auto& Pair : RegisteredTools)
			{
				if (Pair.Value.Definition->Toolset == Toolset)
				{
					Names.Add(Pair.Key);
				}
			}
			Names.Sort();
			OutError = FString::Printf(TEXT("Toolset '%s' has no tool '%s'. Its tools: %s"), Toolset->Name, *ToolName, *FString::Join(Names, TEXT(", ")));
			return false;
		}
	}
	else if (!Entry)
	{
		OutError = LeftToUnrealMcp.Contains(ToolName) ? N2CMcpToolSearch::LeftToUnrealMcpText(ToolName)
			: FString::Printf(TEXT("Unknown tool '%s'. Call list_toolsets and describe_toolset for the tool names."), *ToolName);
		return false;
	}

	OutToolName = ToolName;
	OutArguments = Arguments;
	return true;
}

void FN2CMcpToolManager::ClearAllTools()
{
	FScopeLock Lock(&ToolsLock);
	RegisteredTools.Empty();
	FN2CLogger::Get().Log(TEXT("Cleared all registered MCP tools"), EN2CLogSeverity::Info);
}
