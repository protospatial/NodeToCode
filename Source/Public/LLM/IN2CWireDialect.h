// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CHttpTransport.h"
#include "LLM/N2CLLMRequest.h"

struct FN2CProviderProfile;

/**
 * A wire dialect (decision 0004, card 0218): how one family of APIs is spoken. It turns an FN2CLLMRequest and a
 * provider profile into the HTTP request (URL, body, the profile's headers; never the key, which the service adds
 * from the resolved credential) and the response body back into an FN2CLLMResult. Dialects hold no state, so one
 * instance serves every request.
 */
class NODETOCODE_API IN2CWireDialect
{
public:
	virtual ~IN2CWireDialect() = default;

	/** The catalog's dialect name (openai_chat, ...) */
	virtual FName GetName() const = 0;

	/** Fills Out (URL, body, headers). False with OutError when the request cannot be encoded (no model) */
	virtual bool EncodeRequest(const FN2CLLMRequest& Request, const FN2CProviderProfile& Profile, FN2CHttpRequestSpec& Out,
		FString& OutError) const = 0;

	/**
	 * Decodes one response body: the answer, or an error (the HTTP handler hands non-2xx bodies on wrapped with their
	 * "http_status", so a provider error is read from the body too)
	 */
	virtual FN2CLLMResult DecodeResponse(const FString& Body, const FN2CProviderProfile& Profile, const FString& OutputBudgetHint) const = 0;
};
