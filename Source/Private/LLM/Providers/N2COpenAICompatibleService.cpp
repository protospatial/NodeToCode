// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Providers/N2COpenAICompatibleService.h"

#include "Core/N2CSecretStore.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "HAL/PlatformMisc.h"
#include "LLM/Dialects/N2COpenAIChatDialect.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

// ---------------------------------------------------------------------------------------------------------------------
// Parser

bool UN2COpenAICompatibleResponseParser::ParseLLMResponse(const FString& InJson, FN2CTranslationResponse& OutResponse)
{
	const FN2CLLMResult Result = FN2COpenAIChatDialect::Get().DecodeResponse(InJson, Profile, OutputBudgetHint);
	OutResponse.Usage = Result.Usage;
	if (Result.Usage.InputTokens > 0 || Result.Usage.OutputTokens > 0)
	{
		LogUsage(OutResponse.Usage, TEXT("OpenAICompatibleResponseParser"));
	}
	if (Result.IsError())
	{
		// HandleCommonErrorResponse, ExtractChatCompletionContent and SetParseError logged it
		OutResponse.Error = Result.Error;
		return false;
	}
	FN2CLogger::Get().LogLLMContent(TEXT("LLM Response Message Content"), TEXT("LLM response message content"), Result.Content);
	return Super::ParseLLMResponse(Result.Content, OutResponse);
}

UN2COpenAICompatibleResponseParser* UN2COpenAICompatibleResponseParser::MakeFor(EN2CLLMProvider Provider, UObject* Outer, const FString& ProfileId)
{
	UN2COpenAICompatibleResponseParser* Parser = NewObject<UN2COpenAICompatibleResponseParser>(Outer ? Outer : GetTransientPackage());
	FN2CProviderProfile Resolved;
	UN2COpenAICompatibleService::ResolveProfile(Provider, ProfileId, Resolved);
	Parser->SetProfile(Resolved, UN2COpenAICompatibleService::GetOutputBudgetHint(Provider));
	return Parser;
}

// ---------------------------------------------------------------------------------------------------------------------
// Service

FString UN2COpenAICompatibleService::GetOutputBudgetHint(EN2CLLMProvider Provider)
{
	switch (Provider)
	{
	case EN2CLLMProvider::DeepSeek:
		return TEXT("Project Settings > Node to Code > LLM Services > DeepSeek > Max Output Tokens (0 = Auto)");
	case EN2CLLMProvider::LMStudio:
		return TEXT("Project Settings > Node to Code > LLM Services > LM Studio > Max Output Tokens (0 = Auto sends none), ")
			TEXT("or the model's loaded context length on the server");
	default:
		return TEXT("the model's context length on the server (OpenAI-Compatible requests send no output budget)");
	}
}

bool UN2COpenAICompatibleService::ResolveProfile(EN2CLLMProvider Provider, const FString& ProfileId, FN2CProviderProfile& OutProfile,
	FString* OutError)
{
	const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
	const UN2CUserSettings* User = GetDefault<UN2CUserSettings>();
	auto Fail = [OutError](const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		return false;
	};

	if (Provider == EN2CLLMProvider::DeepSeek || Provider == EN2CLLMProvider::LMStudio)
	{
		const FN2CProviderProfile* Row = Catalog.FindProvider(FN2CModelCatalog::ProviderIdFor(Provider));
		if (!Row)
		{
			return Fail(TEXT("The model catalog has no row for this provider"));
		}
		OutProfile = *Row;
		if (Provider == EN2CLLMProvider::LMStudio && User && !User->LMStudioEndpoint.TrimStartAndEnd().IsEmpty())
		{
			// The user's server address; the path is added when the address does not already end with it
			FString Endpoint = User->LMStudioEndpoint.TrimStartAndEnd();
			while (Endpoint.EndsWith(TEXT("/")))
			{
				Endpoint.LeftChopInline(1);
			}
			if (Endpoint.EndsWith(OutProfile.Path))
			{
				Endpoint.LeftChopInline(OutProfile.Path.Len());
			}
			OutProfile.BaseUrl = Endpoint;
		}
		return true;
	}

	if (Provider != EN2CLLMProvider::OpenAICompatible)
	{
		return Fail(TEXT("Not an OpenAI-compatible provider"));
	}

	const FString Wanted = (ProfileId.TrimStartAndEnd().IsEmpty() && User ? User->ProviderProfile : ProfileId).TrimStartAndEnd();
	bool bFound = false;
	if (User)
	{
		for (const FN2CCustomProviderProfile& Custom : User->CustomProviderProfiles)
		{
			if (Custom.Id.TrimStartAndEnd().Equals(Wanted, ESearchCase::IgnoreCase))
			{
				OutProfile = Custom.ToProfile();
				bFound = true;
				break;
			}
		}
	}
	if (!bFound)
	{
		const FN2CProviderProfile* Row = Catalog.FindProvider(FName(*Wanted));
		if (!Row || Row->Provider != EN2CLLMProvider::OpenAICompatible)
		{
			return Fail(FString::Printf(
				TEXT("No OpenAI-compatible profile '%s': pick a preset or one of your Custom Provider Profiles under Editor Preferences > Plugins > Node to Code (User) > Provider Profile"),
				*Wanted));
		}
		OutProfile = *Row;
	}
	if (User && !User->OpenAICompatibleBaseUrl.TrimStartAndEnd().IsEmpty())
	{
		OutProfile.BaseUrl = User->OpenAICompatibleBaseUrl.TrimStartAndEnd();
	}
	return true;
}

bool UN2COpenAICompatibleService::ResolveProfileCredential(const FN2CProviderProfile& InProfile, FN2CCredential& OutCredential)
{
	OutCredential = FN2CCredential();
	OutCredential.AuthStyle = FN2CCredential::AuthStyleFromName(InProfile.AuthStyle);
	if (!InProfile.SecretId.IsEmpty())
	{
		FString VariableName;
		const EN2CApiKeySource Origin = FN2CSecretStore::Get().GetApiKeySource(InProfile.SecretId, &VariableName);
		if (Origin != EN2CApiKeySource::None)
		{
			OutCredential.Value = FN2CSecretStore::Get().GetApiKey(InProfile.SecretId).TrimStartAndEnd();
			OutCredential.Origin = Origin;
			OutCredential.VariableName = VariableName;
		}
	}
	if (OutCredential.Value.IsEmpty())
	{
		for (const FString& Variable : InProfile.ApiKeyEnvironmentVariables)
		{
			const FString Value = FPlatformMisc::GetEnvironmentVariable(*Variable).TrimStartAndEnd();
			if (!Value.IsEmpty())
			{
				OutCredential.Value = Value;
				OutCredential.Origin = EN2CApiKeySource::Environment;
				OutCredential.VariableName = Variable;
				break;
			}
		}
	}
	return !OutCredential.Value.IsEmpty() || !InProfile.bNeedsKey;
}

bool UN2COpenAICompatibleService::Initialize(const FN2CLLMConfig& InConfig)
{
	FString Error;
	bHasProfile = ResolveProfile(InConfig.Provider, InConfig.ProviderProfile, Profile, &Error);
	if (!bHasProfile)
	{
		FN2CLogger::Get().LogError(Error, TEXT("OpenAICompatibleService"));
		return false;
	}
	if (!InConfig.ApiEndpoint.TrimStartAndEnd().IsEmpty())
	{
		// An endpoint the caller set (an integration, a spec's local stub) is the whole URL
		Profile.BaseUrl = InConfig.ApiEndpoint.TrimStartAndEnd();
		Profile.Path.Reset();
	}
	FN2CLLMConfig Resolved = InConfig;
	Resolved.ApiEndpoint = Profile.GetEndpoint();
	if (Resolved.Model.TrimStartAndEnd().IsEmpty())
	{
		Resolved.Model = Profile.DefaultModel;
	}
	FN2CLogger::Get().Log(FString::Printf(TEXT("OpenAI-compatible profile %s: %s"), *Profile.Id.ToString(), *Resolved.ApiEndpoint),
		EN2CLogSeverity::Info, TEXT("OpenAICompatibleService"));
	return Super::Initialize(Resolved);
}

UN2CResponseParserBase* UN2COpenAICompatibleService::CreateResponseParser()
{
	UN2COpenAICompatibleResponseParser* Parser = NewObject<UN2COpenAICompatibleResponseParser>(this);
	Parser->SetProfile(Profile, GetOutputBudgetHint(Config.Provider));
	return Parser;
}

void UN2COpenAICompatibleService::GetConfiguration(FString& OutEndpoint, bool& OutSupportsSystemPrompts)
{
	OutEndpoint = Profile.GetEndpoint();
	OutSupportsSystemPrompts = true;
}

void UN2COpenAICompatibleService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
	// The key goes in the profile's header, added per request from the resolved credential; never one of these
	OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
	for (const TPair<FString, FString>& Header : Profile.ExtraHeaders)
	{
		OutHeaders.Add(Header.Key, Header.Value);
	}
}

FN2CLLMRequest UN2COpenAICompatibleService::BuildRequest(const FString& UserMessage, const FString& SystemMessage) const
{
	FN2CLLMRequest Request;
	Request.Model = Config.Model.IsEmpty() ? Profile.DefaultModel : Config.Model;
	Request.SystemPrompt = SystemMessage;

	FString FinalUserMessage = UserMessage;
	if (PromptManager)
	{
		PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
	}
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	if (Config.Provider == EN2CLLMProvider::LMStudio && Settings && !Settings->LMStudioPrependedModelCommand.IsEmpty())
	{
		FinalUserMessage = Settings->LMStudioPrependedModelCommand + TEXT("\n\n") + FinalUserMessage;
	}
	Request.UserMessage = FinalUserMessage;

	// The model's catalog row decides thinking, sampling, the Auto budget and whether it takes a response_format; the
	// request's own thinking mode picks among rows that share an ID (DeepSeek)
	const FN2CModelMetadata* Meta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Request.Model, Config.ReasoningEffort);
	if (Meta && Meta->Provider != Config.Provider)
	{
		Meta = nullptr;
	}
	Request.ReasoningEffort = !Config.ReasoningEffort.IsEmpty() ? Config.ReasoningEffort : (Meta ? Meta->ReasoningEffort : FString());
	const bool bThinking = !Request.ReasoningEffort.IsEmpty() && !Request.ReasoningEffort.Equals(TEXT("none"), ESearchCase::IgnoreCase);
	if (Meta && Meta->bAcceptsSamplingParams && !bThinking && Config.Provider != EN2CLLMProvider::LMStudio)
	{
		Request.Temperature = Meta->DefaultTemperature;
	}
	Request.MaxOutputTokens = UN2CLLMPayloadBuilder::ResolveMaxOutputTokens(Config.Provider, Meta, Config.MaxOutputTokens,
		FN2CTokenEstimationService::EstimateTokensForChars(SystemMessage.Len() + FinalUserMessage.Len()));

	// A model with a catalog row takes a response_format when its row says so; a host whose catalog lists models
	// (DeepSeek) sends none for an ID it does not know; local and custom hosts always get the profile's format
	Request.ResponseSchema = UN2CLLMPayloadBuilder::GetN2CResponseSchema();
	Request.bAllowStructuredOutput = Meta ? Meta->bSupportsStructuredOutput : FN2CModelCatalog::Get().List(Profile.Id).Num() == 0;
	Request.OutputBudgetHint = GetOutputBudgetHint(Config.Provider);
	return Request;
}

FString UN2COpenAICompatibleService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
	const FN2CLLMRequest Request = BuildRequest(UserMessage, SystemMessage);
	FN2CHttpRequestSpec Spec;
	FString Error;
	if (!FN2COpenAIChatDialect::Get().EncodeRequest(Request, Profile, Spec, Error))
	{
		// No model: SendRequest refuses an OpenAI-Compatible request without one before it gets here
		FN2CLogger::Get().LogError(Error, TEXT("OpenAICompatibleService"));
	}
	// The body holds the Blueprint logic: logged in full only with n2c.LogLLMPayloads 1, else its size
	FN2CLogger::Get().LogLLMContent(TEXT("LLM Request Payload"), TEXT("LLM request body"), Spec.Body);
	return Spec.Body;
}

FN2CLLMRequestHandleRef UN2COpenAICompatibleService::SendRequest(const FString& JsonPayload, const FString& SystemMessage,
	const FOnLLMResponseReceived& OnComplete)
{
	// DeepSeek and LM Studio resolve their keys through the credential source, as every built-in provider does
	if (Config.Provider != EN2CLLMProvider::OpenAICompatible)
	{
		return Super::SendRequest(JsonPayload, SystemMessage, OnComplete);
	}

	FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
	if (!bIsInitialized)
	{
		Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("Service not initialized")));
		return Handle;
	}
	if (Config.Model.TrimStartAndEnd().IsEmpty())
	{
		Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(FString::Printf(
			TEXT("No model for %s: set Model under Editor Preferences > Plugins > Node to Code (User) > OpenAI-Compatible (the model ID the server serves)"),
			*Profile.DisplayName), EN2CTranslationErrorKind::InvalidRequest));
		return Handle;
	}

	// The profile's key: the secret store entry it names, else its environment variables; read for this request only
	FN2CCredential Credential;
	ResolveProfileCredential(Profile, Credential);
	if (Credential.Value.IsEmpty() && Profile.bNeedsKey)
	{
		const FString Message = FString::Printf(TEXT("No API key for %s: set the environment variable %s for the editor"),
			*Profile.DisplayName, Profile.ApiKeyEnvironmentVariables.Num() > 0 ? *FString::Join(Profile.ApiKeyEnvironmentVariables, TEXT(" or ")) : TEXT("the profile names"));
		FN2CLogger::Get().LogError(Message, TEXT("OpenAICompatibleService"));
		Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(Message, EN2CTranslationErrorKind::Auth));
		return Handle;
	}
	SendWithCredential(Credential, JsonPayload, SystemMessage, OnComplete, Handle);
	return Handle;
}
