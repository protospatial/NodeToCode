// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CLLMRequest.h"
#include "LLM/N2CModelCatalog.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2COpenAICompatibleService.generated.h"

/**
 * Reads an OpenAI-compatible answer through FN2COpenAIChatDialect::DecodeResponse with its profile, then the N2C JSON
 * in it (card 0218: one parser for DeepSeek, LM Studio and every OpenAI-Compatible profile)
 */
UCLASS()
class NODETOCODE_API UN2COpenAICompatibleResponseParser : public UN2CResponseParserBase
{
	GENERATED_BODY()

public:
	virtual bool ParseLLMResponse(const FString& InJson, FN2CTranslationResponse& OutResponse) override;

	/** The profile and the Truncated hint (where the output budget is raised) this parser reads with */
	void SetProfile(const FN2CProviderProfile& InProfile, const FString& InOutputBudgetHint)
	{
		Profile = InProfile;
		OutputBudgetHint = InOutputBudgetHint;
	}

	/** A parser for Provider's resolved profile (specs; services make their own) */
	static UN2COpenAICompatibleResponseParser* MakeFor(EN2CLLMProvider Provider, UObject* Outer, const FString& ProfileId = FString());

private:
	FN2CProviderProfile Profile;
	FString OutputBudgetHint;
};

/**
 * The one service for every OpenAI-compatible host (card 0218): DeepSeek, LM Studio and the OpenAI-Compatible provider's
 * presets and custom profiles. The profile (ResolveProfile) says where requests go and how they are written; the
 * request (model, budget, thinking, temperature) comes from this request's config and the model's catalog row;
 * FN2COpenAIChatDialect encodes it. DeepSeek and LM Studio keep their credential-source keys; an OpenAI-Compatible
 * profile reads its key from the secret store entry or environment variables it names.
 */
UCLASS()
class NODETOCODE_API UN2COpenAICompatibleService : public UN2CBaseLLMService
{
	GENERATED_BODY()

public:
	virtual bool Initialize(const FN2CLLMConfig& InConfig) override;
	virtual FN2CLLMRequestHandleRef SendRequest(const FString& JsonPayload, const FString& SystemMessage,
		const FOnLLMResponseReceived& OnComplete) override;
	virtual void GetConfiguration(FString& OutEndpoint, bool& OutSupportsSystemPrompts) override;
	virtual EN2CLLMProvider GetProviderType() const override { return Config.Provider; }
	virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

	/**
	 * The profile a provider's requests use: DeepSeek and LM Studio are their catalog rows (LM Studio with the user's
	 * LM Studio Server Endpoint); OpenAI-Compatible is ProfileId (else the user's Provider Profile): a custom profile
	 * of that ID, else the catalog preset, with the user's Base URL Override. False with OutError when there is none.
	 */
	static bool ResolveProfile(EN2CLLMProvider Provider, const FString& ProfileId, FN2CProviderProfile& OutProfile, FString* OutError = nullptr);

	/** The request this service sends for one translation (what FormatRequestPayload encodes) */
	FN2CLLMRequest BuildRequest(const FString& UserMessage, const FString& SystemMessage) const;

	/** The resolved profile (valid after Initialize) */
	const FN2CProviderProfile& GetProfile() const { return Profile; }

	/** Where a Truncated error tells the user to raise the output budget, per provider */
	static FString GetOutputBudgetHint(EN2CLLMProvider Provider);

protected:
	virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
	virtual UN2CResponseParserBase* CreateResponseParser() override;
	virtual FString GetDefaultEndpoint() const override { return Profile.GetEndpoint(); }

private:
	FN2CProviderProfile Profile;
	bool bHasProfile = false;
};
