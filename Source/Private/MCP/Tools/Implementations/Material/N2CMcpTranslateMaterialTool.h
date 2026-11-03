// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * translate-material (card 0455, decision 0009): explains a Material or Material Function in Pseudocode. A long-running
 * tool: InvokeAsync checks the arguments, captures the material as get-material-n2cjson does (by path, or the focused
 * Material Editor's working copy), and starts an FN2CTranslateBlueprintCall whose request source sends that N2C JSON
 * through UN2CLLMModule::ProcessN2CJsonWithOverrides. The module picks the MaterialGen prompt from the document's
 * metadata.asset_kind (card 0454). Progress names the material; it streams on UE 5.8 and is one held response on 5.6
 * and 5.7 (decision 0013). Nothing is written to the translation archive (bArchiveRun false) until card 0465.
 *
 * Every refusal comes before any provider is created: a language other than Pseudocode is
 * TARGET_NOT_AVAILABLE_FOR_MATERIALS, an unknown provider or language is INVALID_ARGUMENT, and the material errors are
 * get-material-n2cjson's (NO_FOCUSED_MATERIAL, ASSET_NOT_FOUND, UNSUPPORTED_ASSET_TYPE, CAPTURE_FAILED).
 */
class FN2CMcpTranslateMaterialTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments) override;

	/** Sends a material's N2C JSON with the provider and model overrides (already checked; empty = the settings default) */
	using FMaterialRequestStarter = TFunction<FN2CLLMRequestHandleRef(const FString& /*N2CJson*/, const FString& /*ProviderId*/,
		const FString& /*ModelId*/, const FOnLLMResponseReceived& /*OnComplete*/)>;

	/**
	 * The production request: the settings' config with the overrides, EN2CCodeLanguage::Pseudocode, no archive run.
	 * Game thread.
	 */
	static FN2CLLMRequestHandleRef StartMaterialRequest(const FString& N2CJson, const FString& ProviderId, const FString& ModelId,
		const FOnLLMResponseReceived& OnComplete);

	/** Tests only: calls send their request through InStarter (a fake provider) instead; unset restores the real one */
	static void SetRequestStarterForTesting(FMaterialRequestStarter InStarter);
};
