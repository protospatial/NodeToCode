// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Tools/N2CToolCatalog.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Utils/N2CMcpPlaySessionGuard.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace N2CToolCatalogPrivate
{
	FString ToCompactJson(const TSharedPtr<FJsonValue>& Value)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Text;
	}

	/** One JSON object or array, else null */
	TSharedPtr<FJsonValue> ParseJsonContainer(const FString& Text)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("{")) && !Trimmed.StartsWith(TEXT("[")))
		{
			return nullptr;
		}
		TSharedPtr<FJsonValue> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid()
			&& (Parsed->Type == EJson::Object || Parsed->Type == EJson::Array))
		{
			return Parsed;
		}
		return nullptr;
	}

	/** The text items of a result, joined by newlines */
	FString JoinText(const FMcpToolCallResult& Result)
	{
		TArray<FString> Texts;
		for (const TSharedPtr<FJsonObject>& Item : Result.Content)
		{
			FString Type;
			FString Text;
			if (Item.IsValid() && Item->TryGetStringField(TEXT("type"), Type) && Type == TEXT("text")
				&& Item->TryGetStringField(TEXT("text"), Text))
			{
				Texts.Add(MoveTemp(Text));
			}
		}
		return FString::Join(Texts, TEXT("\n"));
	}
}

// ---- FN2CToolDescriptor ----

FN2CToolDescriptor FN2CToolDescriptor::FromDefinition(const TSharedPtr<const FMcpToolDefinition>& Definition)
{
	FN2CToolDescriptor Descriptor;
	if (!Definition.IsValid())
	{
		return Descriptor;
	}
	Descriptor.Name = Definition->Name;
	Descriptor.Title = Definition->Title;
	Descriptor.Description = Definition->Description;
	Descriptor.Toolset = Definition->Toolset;
	Descriptor.InputSchema = Definition->InputSchema;
	Descriptor.OutputSchema = Definition->OutputSchema;
	Descriptor.Hints = Definition->Annotations;
	Descriptor.ExecutionPolicy = Definition->bIsLongRunning ? EN2CToolExecutionPolicy::LongRunning : EN2CToolExecutionPolicy::Immediate;
	Descriptor.bReturnsUntrustedContent = Definition->bReturnsUntrustedContent;
	Descriptor.bAllowedDuringPlaySession = Definition->bAllowedDuringPlaySession;
	Descriptor.Source = Definition;
	return Descriptor;
}

FMcpToolDefinition FN2CToolDescriptor::ToMcpDefinition() const
{
	FMcpToolDefinition Definition = Source.IsValid() ? *Source : FMcpToolDefinition();
	Definition.Name = Name;
	Definition.Title = Title;
	Definition.Description = Description;
	Definition.Toolset = Toolset;
	Definition.InputSchema = InputSchema;
	Definition.OutputSchema = OutputSchema;
	Definition.Annotations = Hints;
	Definition.bIsLongRunning = ExecutionPolicy == EN2CToolExecutionPolicy::LongRunning;
	Definition.bReturnsUntrustedContent = bReturnsUntrustedContent;
	Definition.bAllowedDuringPlaySession = bAllowedDuringPlaySession;
	return Definition;
}

TArray<FString> FN2CToolDescriptor::GetRequiredArguments() const
{
	TArray<FString> Required;
	const TArray<TSharedPtr<FJsonValue>>* RequiredValues = nullptr;
	if (InputSchema.IsValid() && InputSchema->TryGetArrayField(TEXT("required"), RequiredValues) && RequiredValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RequiredValues)
		{
			FString ArgumentName;
			if (Value.IsValid() && Value->TryGetString(ArgumentName) && !ArgumentName.IsEmpty())
			{
				Required.Add(ArgumentName);
			}
		}
	}
	return Required;
}

// ---- FN2CToolError / FN2CToolOutcome ----

const TCHAR* FN2CToolError::CodeName(EN2CToolErrorCode Code)
{
	switch (Code)
	{
	case EN2CToolErrorCode::UnknownTool: return TEXT("UNKNOWN_TOOL");
	case EN2CToolErrorCode::InvalidArgument: return TEXT("INVALID_ARGUMENT");
	case EN2CToolErrorCode::PlaySessionActive: return TEXT("PIE_ACTIVE");
	case EN2CToolErrorCode::NotRunnableHere: return TEXT("NOT_RUNNABLE_HERE");
	case EN2CToolErrorCode::Cancelled: return TEXT("CANCELLED");
	case EN2CToolErrorCode::ToolFailed: return TEXT("TOOL_FAILED");
	case EN2CToolErrorCode::Internal: return TEXT("INTERNAL");
	default: return TEXT("TOOL_FAILED");
	}
}

FString FN2CToolOutcome::ToValueString() const
{
	if (Error.IsSet())
	{
		return Error->Message;
	}
	if (!Value.IsValid() || Value->Type == EJson::Null || Value->Type == EJson::None)
	{
		return FString();
	}
	if (Value->Type == EJson::String)
	{
		return Value->AsString();
	}
	return N2CToolCatalogPrivate::ToCompactJson(Value);
}

FN2CToolOutcome FN2CToolOutcome::MakeValue(const TSharedPtr<FJsonValue>& InValue)
{
	FN2CToolOutcome Outcome;
	Outcome.Value = InValue.IsValid() ? InValue : MakeShared<FJsonValueNull>();
	return Outcome;
}

FN2CToolOutcome FN2CToolOutcome::MakeError(EN2CToolErrorCode Code, const FString& Message, const FString& Field)
{
	FN2CToolOutcome Outcome;
	FN2CToolError Error;
	Error.Code = Code;
	Error.Message = Message;
	Error.Field = Field;
	Outcome.Error = MoveTemp(Error);
	return Outcome;
}

FN2CToolOutcome FN2CToolOutcome::FromCallToolResult(const FMcpToolCallResult& Result, EN2CToolErrorCode ErrorCode)
{
	using namespace N2CToolCatalogPrivate;
	const FString Text = JoinText(Result);

	FN2CToolOutcome Outcome;
	if (Result.bIsError)
	{
		Outcome = MakeError(ErrorCode, Text);
	}
	else if (Result.StructuredContent.IsValid())
	{
		Outcome = MakeValue(MakeShared<FJsonValueObject>(Result.StructuredContent));
	}
	else if (const TSharedPtr<FJsonValue> Parsed = ParseJsonContainer(Text))
	{
		Outcome = MakeValue(Parsed);
	}
	else
	{
		Outcome = MakeValue(MakeShared<FJsonValueString>(Text));
	}
	Outcome.LegacyResult = MakeShared<FMcpToolCallResult>(Result);
	return Outcome;
}

FMcpToolCallResult FN2CToolOutcome::ToCallToolResult() const
{
	if (LegacyResult.IsValid())
	{
		return *LegacyResult;
	}
	if (Error.IsSet())
	{
		return FMcpToolCallResult::CreateErrorResult(Error->Message);
	}
	FMcpToolCallResult Result = FMcpToolCallResult::CreateTextResult(ToValueString());
	if (Value.IsValid() && Value->Type == EJson::Object)
	{
		Result.StructuredContent = Value->AsObject();
	}
	return Result;
}

// ---- FN2CToolCatalog ----

FN2CToolCatalog::FN2CToolCatalog(FN2CMcpToolManager& InTools)
	: Tools(InTools)
{
}

FN2CToolCatalog& FN2CToolCatalog::Get()
{
	static FN2CToolCatalog Instance(FN2CMcpToolManager::Get());
	return Instance;
}

TArray<FN2CToolDescriptor> FN2CToolCatalog::Describe(const FN2CToolFilter& Filter) const
{
	TArray<FN2CToolDescriptor> Descriptors;
	for (const TSharedPtr<const FMcpToolDefinition>& Definition : Tools.CollectDefinitions(Filter.Scope == FN2CToolFilter::EScope::Listed))
	{
		FN2CToolDescriptor Descriptor = FN2CToolDescriptor::FromDefinition(Definition);
		if ((Filter.Toolset && Descriptor.Toolset != Filter.Toolset) || (Filter.bReadOnlyOnly && !Descriptor.IsReadOnly()))
		{
			continue;
		}
		Descriptors.Add(MoveTemp(Descriptor));
	}
	return Descriptors;
}

TOptional<FN2CToolDescriptor> FN2CToolCatalog::Find(const FString& Name) const
{
	const TSharedPtr<const FMcpToolDefinition> Definition = Tools.FindEntry(Name).Definition;
	if (!Definition.IsValid())
	{
		return {};
	}
	return FN2CToolDescriptor::FromDefinition(Definition);
}

bool FN2CToolCatalog::IsListed(const FString& Name) const
{
	return Tools.IsToolListed(Name);
}

TFuture<FN2CToolOutcome> FN2CToolCatalog::Invoke(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, const FN2CToolCallContextRef& Context) const
{
	// Copies of the shared pointers, taken under the manager's lock; the tool runs without it, so a tool may use the
	// manager (call_tool, a tool that changes the active set) and anything it waits for on the game thread may too
	const FN2CMcpToolManager::FRegisteredTool Entry = Tools.FindEntry(Name);
	if (!Entry.Tool.IsValid() || !Entry.Definition.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tool '%s' not found"), *Name));
		return MakeFulfilledPromise<FN2CToolOutcome>(FN2CToolOutcome::MakeError(EN2CToolErrorCode::UnknownTool,
			FString::Printf(TEXT("Tool '%s' not found"), *Name))).GetFuture();
	}
	return InvokeTool(Entry.Tool.ToSharedRef(), FN2CToolDescriptor::FromDefinition(Entry.Definition), Arguments, Context);
}

FN2CToolOutcome FN2CToolCatalog::InvokeNow(const FString& Name, const TSharedPtr<FJsonObject>& Arguments) const
{
	const TOptional<FN2CToolDescriptor> Descriptor = Find(Name);
	if (Descriptor.IsSet() && Descriptor->ExecutionPolicy == EN2CToolExecutionPolicy::LongRunning)
	{
		// A long-running tool is answered on its own tools/call request (FN2CMcpProtocol, also through call_tool); it
		// reaches this synchronous path only inside a JSON-RPC batch, which is answered as a whole and cannot wait for it
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("%s was called inside a JSON-RPC batch; it only runs as a single request."), *Name));
		return FN2CToolOutcome::MakeError(EN2CToolErrorCode::NotRunnableHere, FString::Printf(
			TEXT("%s cannot run inside a JSON-RPC batch. Send it as a single tools/call request."), *Name));
	}

	TFuture<FN2CToolOutcome> Future = Invoke(Name, Arguments, FN2CToolCallContext::MakeDetached());
	// An Immediate tool has set it already
	check(Future.IsReady());
	return Future.Get();
}

TOptional<FN2CToolError> FN2CToolCatalog::CheckArguments(const FN2CToolDescriptor& Descriptor, const TSharedPtr<FJsonObject>& Arguments)
{
	for (const FString& Required : Descriptor.GetRequiredArguments())
	{
		const TSharedPtr<FJsonValue> Value = Arguments.IsValid() ? Arguments->TryGetField(Required) : nullptr;
		if (!Value.IsValid() || Value->Type == EJson::Null || Value->Type == EJson::None)
		{
			FN2CToolError Error;
			Error.Code = EN2CToolErrorCode::InvalidArgument;
			// The code prefix the tools' own argument errors use ("INVALID_ARGUMENT: ...")
			Error.Message = FString::Printf(TEXT("%s: Missing required parameter: %s"), FN2CToolError::CodeName(Error.Code), *Required);
			Error.Field = Required;
			return Error;
		}
	}
	return {};
}

TFuture<FN2CToolOutcome> FN2CToolCatalog::InvokeTool(const TSharedRef<IN2CMcpTool>& Tool, const FN2CToolDescriptor& Descriptor,
	const TSharedPtr<FJsonObject>& Arguments, const FN2CToolCallContextRef& Context)
{
	const FString& Name = Descriptor.Name;

	// Tools that change a Blueprint, an asset or a level wait until Play In Editor ends (card 0170); call_tool is
	// allowed itself and comes back here for the tool it calls, which is judged by its own descriptor
	if (TOptional<FMcpToolCallResult> Refused = FN2CMcpPlaySessionGuard::CheckCall(Descriptor.ToMcpDefinition()))
	{
		return MakeFulfilledPromise<FN2CToolOutcome>(FN2CToolOutcome::FromCallToolResult(Refused.GetValue(), EN2CToolErrorCode::PlaySessionActive)).GetFuture();
	}

	// The input schema's required arguments, checked once here for every tool (the tool's own checks stay)
	if (TOptional<FN2CToolError> Missing = CheckArguments(Descriptor, Arguments))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Refused MCP tool %s: %s"), *Name, *Missing->Message), TEXT("MCP"));
		return MakeFulfilledPromise<FN2CToolOutcome>(FN2CToolOutcome::MakeError(Missing->Code, Missing->Message, Missing->Field)).GetFuture();
	}

	const TSharedPtr<FJsonObject> CallArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();

	if (Descriptor.ExecutionPolicy == EN2CToolExecutionPolicy::LongRunning)
	{
		// Started here (game thread); the tool sets its future later, on the game thread
		TFuture<FMcpToolCallResult> Future = Tool->InvokeAsync(Context, CallArguments);
		if (!Future.IsValid())
		{
			return MakeFulfilledPromise<FN2CToolOutcome>(FN2CToolOutcome::MakeError(EN2CToolErrorCode::Internal,
				FString::Printf(TEXT("Tool '%s' returned no result"), *Name))).GetFuture();
		}
		const FN2CToolCallContextPtr CallContext = Context;
		return Future.Next([CallContext](FMcpToolCallResult Result)
		{
			const bool bCancelled = Result.bIsError && CallContext.IsValid() && CallContext->IsCancelled();
			return FN2CToolOutcome::FromCallToolResult(Result, bCancelled ? EN2CToolErrorCode::Cancelled : EN2CToolErrorCode::ToolFailed);
		});
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Executing MCP tool: %s"), *Name), EN2CLogSeverity::Debug);

	try
	{
		FMcpToolCallResult Result = Tool->Execute(CallArguments);
		if (Descriptor.bReturnsUntrustedContent)
		{
			// Blueprint text (comments, tooltips, string literals) is data for the agent, never instructions (card 0124)
			FN2CMcpUntrustedContent::MarkToolResult(Result);
		}
		return MakeFulfilledPromise<FN2CToolOutcome>(FN2CToolOutcome::FromCallToolResult(Result)).GetFuture();
	}
	catch (...)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Exception thrown while executing tool '%s'"), *Name));
		return MakeFulfilledPromise<FN2CToolOutcome>(FN2CToolOutcome::MakeError(EN2CToolErrorCode::Internal,
			FString::Printf(TEXT("Internal error executing tool '%s'"), *Name))).GetFuture();
	}
}
