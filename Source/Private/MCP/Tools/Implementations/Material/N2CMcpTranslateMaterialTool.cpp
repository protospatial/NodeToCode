// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpTranslateMaterialTool.h"
#include "N2CMcpGetMaterialN2CJsonTool.h"
#include "MCP/Tools/Implementations/Translation/N2CTranslateBlueprintCall.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "LLM/N2CLLMModule.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Models/N2CTranslation.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"

REGISTER_MCP_TOOL(FN2CMcpTranslateMaterialTool)

namespace N2CMcpTranslateMaterialToolPrivate
{
	FN2CMcpTranslateMaterialTool::FMaterialRequestStarter& RequestStarterForTesting()
	{
		static FN2CMcpTranslateMaterialTool::FMaterialRequestStarter Starter;
		return Starter;
	}

	TSharedRef<FJsonObject> StringProperty(const TCHAR* Description)
	{
		const TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), TEXT("string"));
		Property->SetStringField(TEXT("description"), Description);
		return Property;
	}

	FMcpToolCallResult Refused(const FString& Message)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("translate-material: %s"), *Message));
		return FMcpToolCallResult::CreateErrorResult(Message);
	}
}

void FN2CMcpTranslateMaterialTool::SetRequestStarterForTesting(FMaterialRequestStarter InStarter)
{
	N2CMcpTranslateMaterialToolPrivate::RequestStarterForTesting() = MoveTemp(InStarter);
}

FMcpToolDefinition FN2CMcpTranslateMaterialTool::GetDefinition() const
{
	using namespace N2CMcpTranslateMaterialToolPrivate;

	FMcpToolDefinition Definition(
		TEXT("translate-material"),
		TEXT("Explains a Material or Material Function in Pseudocode with an LLM provider (NodeToCode's MaterialGen ")
		TEXT("instructions) and returns the explanation as this call's result. With materialPath it reads the saved asset; ")
		TEXT("without it, the working copy of the Material Editor the user activated last, unapplied edits included ")
		TEXT("(NO_FOCUSED_MATERIAL when none is open). Materials are explained in Pseudocode only: any other language is ")
		TEXT("TARGET_NOT_AVAILABLE_FOR_MATERIALS, refused before anything is sent. The material's text (expression ")
		TEXT("descriptions, parameter names, Custom node code and include paths) is sent to the provider. It can take ")
		TEXT("minutes: send params._meta.progressToken for notifications/progress on UE 5.8; on UE 5.6 and 5.7 the call ")
		TEXT("holds one response until it finishes. Nothing is saved to the translation output directory. The result's ")
		TEXT("structuredContent is the explanation (graphs with their code, usage) plus target {assetPath, kind, resolvedBy, ")
		TEXT("workingCopy}; one larger than 200,000 characters is saved under Saved/NodeToCode/Exports/Translations instead ")
		TEXT("and the result lists its graphs, savedTo and a resource_link to the file."),
		N2CMcpToolsets::Material
	);
	Definition.bIsLongRunning = true;

	const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("materialPath"), StringProperty(
		TEXT("Optional. Asset path of the Material or Material Function, e.g. /Engine/EngineMaterials/WorldGridMaterial: reads the saved asset. Omit it to explain the working copy of the Material Editor the user activated last.")));
	Properties->SetObjectField(TEXT("provider"), StringProperty(
		TEXT("Optional: LLM provider ID as get-available-llm-providers lists it (e.g. 'anthropic', 'gemini'; case-insensitive). Uses the settings default if empty; an unknown ID is an error.")));
	Properties->SetObjectField(TEXT("model"), StringProperty(
		TEXT("Optional: specific model ID. Uses the provider's model from settings if empty. An ID NodeToCode does not list for a cloud provider is still sent, with a note in the result's warnings array.")));
	Properties->SetObjectField(TEXT("language"), StringProperty(
		TEXT("Optional, default 'pseudocode', the only language a material is explained in; any other is TARGET_NOT_AVAILABLE_FOR_MATERIALS.")));
	const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	Definition.InputSchema = Schema;

	// translate-focused-blueprint's result shape, plus the material that was read
	const TSharedRef<FJsonObject> Output = FN2CTranslateBlueprintCall::BuildOutputSchema();
	const TSharedPtr<FJsonObject>* OutputProperties = nullptr;
	if (Output->TryGetObjectField(TEXT("properties"), OutputProperties))
	{
		const TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
		Target->SetStringField(TEXT("type"), TEXT("object"));
		Target->SetStringField(TEXT("description"), TEXT("The material explained: {assetPath, kind (material | material-function), resolvedBy (path | focus), workingCopy, workingCopyPath}"));
		(*OutputProperties)->SetObjectField(TEXT("target"), Target);
		(*OutputProperties)->RemoveField(TEXT("run_folder"));
		(*OutputProperties)->RemoveField(TEXT("files"));
	}
	Definition.OutputSchema = Output;

	// Writes nothing in the project; it sends the material to the LLM provider (card 0205's hints)
	SetAnnotations(Definition, FMcpToolAnnotations(true, false, true, true));

	// Expression text and Custom node code come from the asset's author, and the explanation is written from them (card 0124)
	Definition.bReturnsUntrustedContent = true;
	// Reads the material and writes nothing in the project (no archive run): it keeps working during Play In Editor
	Definition.bAllowedDuringPlaySession = true;
	// A session may run it as an MCP task (card 0211), as translate-focused-blueprint
	Definition.TaskSupport = TEXT("optional");

	return Definition;
}

FN2CLLMRequestHandleRef FN2CMcpTranslateMaterialTool::StartMaterialRequest(const FString& N2CJson, const FString& ProviderId,
	const FString& ModelId, const FOnLLMResponseReceived& OnComplete)
{
	check(IsInGameThread());
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (!LLMModule || (!LLMModule->IsInitialized() && !LLMModule->Initialize()))
	{
		FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
		Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("LLMModule failed to initialize.")));
		return Handle;
	}

	// Checked by InvokeAsync already; resolved again for the provider value and the model warnings
	FN2CTranslationOverrides Overrides;
	FString OverrideError;
	if (!FN2CEditorIntegration::ResolveTranslationOverrides(ProviderId, ModelId, FString(), Overrides, OverrideError))
	{
		FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
		Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(OverrideError, EN2CTranslationErrorKind::InvalidRequest));
		return Handle;
	}

	// The settings as they are now, the provider override, then that provider's model (as translate-focused-blueprint)
	FN2CLLMConfig RequestConfig = UN2CLLMModule::MakeConfigFromSettings();
	RequestConfig.Provider = Overrides.Provider.Get(RequestConfig.Provider);
	GetDefault<UN2CSettings>()->ResolveRequestModel(RequestConfig.Provider, ModelId, RequestConfig);

	// The module picks MaterialGen from metadata.asset_kind; no archive run until card 0465
	return LLMModule->ProcessN2CJsonWithOverrides(N2CJson, RequestConfig, EN2CCodeLanguage::Pseudocode, OnComplete,
		FOnN2CTranslationParsed(), /*bArchiveRun*/ false);
}

TFuture<FMcpToolCallResult> FN2CMcpTranslateMaterialTool::InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments)
{
	using namespace N2CMcpTranslateMaterialToolPrivate;
	check(IsInGameThread());

	FN2CMcpArgumentParser ArgParser(Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>());
	const FString MaterialPath = ArgParser.GetOptionalString(TEXT("materialPath")).TrimStartAndEnd();
	const FString ProviderId = ArgParser.GetOptionalString(TEXT("provider"));
	const FString ModelId = ArgParser.GetOptionalString(TEXT("model"));
	const FString LanguageId = ArgParser.GetOptionalString(TEXT("language"));

	// Every refusal before the material is read or a provider is created: unknown IDs, then the language
	FN2CTranslationOverrides Overrides;
	FString OverrideError;
	if (!FN2CEditorIntegration::ResolveTranslationOverrides(ProviderId, ModelId, LanguageId, Overrides, OverrideError))
	{
		return MakeFulfilledPromise<FMcpToolCallResult>(Refused(OverrideError)).GetFuture();
	}
	if (Overrides.Language.IsSet() && !N2C::IsLanguageAvailableForFamily(N2C::MaterialPromptFamily, Overrides.Language.GetValue()))
	{
		FMcpToolCallResult Result = Refused(N2C::TargetNotAvailableForMaterialsMessage(Overrides.Language.GetValue()));
		Result.SetErrorCode(N2C::TargetNotAvailableForMaterialsCode);
		return MakeFulfilledPromise<FMcpToolCallResult>(Result).GetFuture();
	}

	UObject* Asset = nullptr;
	FN2CCapture Captured;
	TSharedPtr<FJsonObject> Target;
	FString Error;
	if (!FN2CMcpGetMaterialN2CJsonTool::ResolveAndCapture(MaterialPath, INDEX_NONE, Asset, Captured, Target, Error))
	{
		return MakeFulfilledPromise<FMcpToolCallResult>(Refused(Error)).GetFuture();
	}
	const FString N2CJson = FN2CSerializer::ToJson(Captured.Blueprint, EN2CJsonStyle::Condensed);
	const FString Name = Asset->GetName();
	const bool bFocus = MaterialPath.IsEmpty();

	FN2CTranslateBlueprintCall::FRequestSource Source;
	const FMaterialRequestStarter& TestStarter = RequestStarterForTesting();
	const FMaterialRequestStarter Starter = TestStarter ? TestStarter : FMaterialRequestStarter(&FN2CMcpTranslateMaterialTool::StartMaterialRequest);
	Source.Start = [Starter, N2CJson](const FString& Provider, const FString& Model, const FString& /*Language*/, const FOnLLMResponseReceived& OnComplete)
	{
		return Starter(N2CJson, Provider, Model, OnComplete);
	};
	Source.StartingMessage = FString::Printf(TEXT("Captured material %s%s"), *Name, bFocus ? TEXT(" (the Material Editor's working copy)") : TEXT(""));
	Source.SentMessage = FString::Printf(TEXT("Explanation of material %s requested from the LLM provider"), *Name);
	Source.ResultFields = MakeShared<FJsonObject>();
	Source.ResultFields->SetObjectField(TEXT("target"), Target);

	const TSharedRef<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> Call = MakeShared<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe>(
		Context, ProviderId, ModelId, TEXT("pseudocode"), Overrides.Warnings, MoveTemp(Source));
	return Call->Start();
}
