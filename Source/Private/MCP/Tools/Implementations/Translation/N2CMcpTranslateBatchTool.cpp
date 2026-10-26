// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpTranslateBatchTool.h"
#include "N2CTranslateBatchCall.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpBatchTargets.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModule.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpTranslateBatchTool)

namespace N2CMcpTranslateBatchToolPrivate
{
	TSharedPtr<FJsonObject> StringProperty(const TCHAR* Description)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("type"), TEXT("string"));
		Json->SetStringField(TEXT("description"), Description);
		return Json;
	}

	/** The lowercase ID of an enum value, as the listing tools give it (anthropic, cpp) */
	template <typename TEnum>
	FString EnumId(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value)).ToLower();
	}

	FMcpToolCallResult Fail(const FString& Error, const TArray<TSharedPtr<FJsonValue>>& Failures, const TArray<TSharedPtr<FJsonValue>>& Skipped)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("error"), Error);
		Json->SetStringField(TEXT("code"), N2CMcpBatchTargets::ErrorCode(Error));
		Json->SetArrayField(TEXT("failures"), Failures);
		if (Skipped.Num() > 0)
		{
			Json->SetArrayField(TEXT("skippedGraphs"), Skipped);
		}
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Json, Writer);
		return FMcpToolCallResult::CreateTextResult(Text, /*bInIsError*/ true);
	}
}

FMcpToolDefinition FN2CMcpTranslateBatchTool::GetDefinition() const
{
	using namespace N2CMcpTranslateBatchToolPrivate;

	FMcpToolDefinition Definition(
		TEXT("translate-batch"),
		TEXT("Translates many Blueprint graphs with an LLM provider in one call, with no Blueprint editor open and without ")
		TEXT("changing focus: one LLM request per graph, one after another. Select the graphs with targets [{blueprintPath, ")
		TEXT("graphName?, graphGuid?}] (a target with only blueprintPath translates every graph of that Blueprint that has ")
		TEXT("nodes) or with a tag and/or category. It can take minutes: send params._meta.progressToken to receive ")
		TEXT("notifications/progress, and cancel with notifications/cancelled (the request in flight stops at once, the ")
		TEXT("rest are skipped). The result lists every graph with its status, the folder its files were saved in (under ")
		TEXT("the translation output directory, BatchTranslation_<time>), its code while the whole result stays under about ")
		TEXT("20000 tokens (else read the files) and, for a failure, the typed error. Only one batch runs at a time. ")
		TEXT("Assets that are not Blueprints are refused per target (UNSUPPORTED_ASSET_TYPE)."),
		N2CMcpToolsets::Translation
	);
	Definition.Title = TEXT("Translate many Blueprint graphs");
	Definition.bIsLongRunning = true;

	const TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	N2CMcpBatchTargets::AddSchema(Properties, MaxGraphs);
	Properties->SetObjectField(TEXT("language"), StringProperty(
		TEXT("Optional: target language ID as get-available-translation-targets lists it (e.g. 'cpp', 'python'; case-insensitive). Default: the settings' Target Language; an unknown ID is an error.")));
	Properties->SetObjectField(TEXT("provider"), StringProperty(
		TEXT("Optional: LLM provider ID as get-available-llm-providers lists it (e.g. 'anthropic', 'ollama'; case-insensitive). Default: the settings' provider; an unknown ID is an error.")));
	Properties->SetObjectField(TEXT("model"), StringProperty(
		TEXT("Optional: model ID. Default: the provider's model in settings. An ID NodeToCode does not list for a cloud provider is still sent, with a note in the result's warnings.")));

	const TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	Schema->SetBoolField(TEXT("additionalProperties"), false);
	Definition.InputSchema = Schema;

	// Sends each graph to an LLM provider (open world) and saves new translation folders; changes no asset
	SetAnnotations(Definition, /*bReadOnly*/ false, /*bDestructive*/ false, /*bIdempotent*/ false, /*bOpenWorld*/ true);

	// Results carry Blueprint text (graph names, code an LLM wrote from the graphs; the call marks them): card 0124
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

TFuture<FMcpToolCallResult> FN2CMcpTranslateBatchTool::InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments)
{
	using namespace N2CMcpTranslateBatchToolPrivate;
	check(IsInGameThread());

	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
	FN2CMcpArgumentParser ArgParser(Args);
	const FString ProviderId = ArgParser.GetOptionalString(TEXT("provider"));
	const FString ModelId = ArgParser.GetOptionalString(TEXT("model"));
	const FString LanguageId = ArgParser.GetOptionalString(TEXT("language"));

	// An unknown provider or language is the agent's mistake: say so, with the valid IDs, before anything is sent
	FN2CTranslationOverrides Overrides;
	FString OverrideError;
	if (!FN2CEditorIntegration::ResolveTranslationOverrides(ProviderId, ModelId, LanguageId, Overrides, OverrideError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("translate-batch: %s"), *OverrideError));
		return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateErrorResult(FString(TEXT("INVALID_ARGUMENT: ")) + OverrideError)).GetFuture();
	}

	const FN2CBatchTargets Targets = N2CMcpBatchTargets::Resolve(Args, MaxGraphs, TEXT("translate-batch"));
	if (!Targets.IsValid())
	{
		return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateErrorResult(Targets.Error)).GetFuture();
	}
	if (Targets.Graphs.Num() == 0)
	{
		return MakeFulfilledPromise<FMcpToolCallResult>(Fail(TEXT("NO_VALID_ITEMS: none of the targets names a graph that can be translated"),
			Targets.FailuresJson(), Targets.SkippedJson())).GetFuture();
	}

	// The request every item is sent with: the settings now, with this call's provider and model
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	FN2CTranslateBatchCall::FSetup Setup;
	Setup.Options.bUseRequestConfig = true;
	Setup.Options.RequestConfig = UN2CLLMModule::MakeConfigFromSettings();
	const EN2CLLMProvider Provider = Overrides.Provider.Get(Setup.Options.RequestConfig.Provider);
	Setup.Options.RequestConfig.Provider = Provider; // its service resolves the key for this provider when each request is sent
	Settings->ResolveRequestModel(Provider, Overrides.ModelId, Setup.Options.RequestConfig);
	Setup.Options.Language = Overrides.Language.Get(Settings->TargetLanguage);

	Setup.ProviderId = EnumId(Provider);
	Setup.ModelId = Setup.Options.RequestConfig.Model;
	Setup.LanguageId = EnumId(Setup.Options.Language);
	Setup.Warnings = Overrides.Warnings;
	Setup.TargetFailures = Targets.FailuresJson();
	Setup.Skipped = Targets.SkippedJson();

	for (const FN2CBatchGraph& Graph : Targets.Graphs)
	{
		FN2CTagInfo& TagInfo = Setup.TagInfos.AddDefaulted_GetRef();
		TagInfo.Tag = Graph.Tag;
		TagInfo.Category = Graph.Category;
		TagInfo.GraphGuid = Graph.GraphGuid;
		TagInfo.GraphName = Graph.GraphName;
		TagInfo.BlueprintPath = Graph.BlueprintPath;
		Setup.Options.Graphs.Add(Graph.Graph);
		Setup.Kinds.Add(Graph.Kind);
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("translate-batch: %d graphs with %s %s to %s (%d targets failed)"),
		Setup.TagInfos.Num(), *Setup.ProviderId, *Setup.ModelId, *Setup.LanguageId, Targets.Failures.Num()), EN2CLogSeverity::Info);

	const TSharedRef<FN2CTranslateBatchCall, ESPMode::ThreadSafe> Call =
		MakeShared<FN2CTranslateBatchCall, ESPMode::ThreadSafe>(Context, MoveTemp(Setup));
	return Call->Start();
}
