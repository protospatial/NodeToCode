// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/Services/N2CConnectionTest.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMProviderRegistry.h"
#include "Models/N2CTranslation.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

FN2CConnectionTestResult FN2CConnectionTest::InterpretResponse(const FString& Response, EN2CLLMProvider Provider, const FString& Model)
{
	FN2CConnectionTestResult Result;
	const FString ProviderName = UN2CLLMModule::GetProviderName(Provider);
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response), Root) || !Root.IsValid())
	{
		Result.Message = FString::Printf(TEXT("%s answered with something that is not JSON: %s"), *ProviderName, *Response.Left(200));
		return Result;
	}
	const TSharedPtr<FJsonValue> ErrorField = Root->TryGetField(TEXT("error"));
	if (ErrorField.IsValid() && !ErrorField->IsNull() && !(ErrorField->Type == EJson::String && ErrorField->AsString().IsEmpty()))
	{
		FN2CTranslationError Error = FN2CTranslationError::FromErrorJson(Root);
		if (Error.Provider.IsEmpty())
		{
			Error.Provider = ProviderName;
		}
		Result.Message = Error.ToDisplayString();
		return Result;
	}
	Result.bOk = true;
	Result.Message = Model.IsEmpty()
		? FString::Printf(TEXT("Connected to %s."), *ProviderName)
		: FString::Printf(TEXT("Connected to %s (%s)."), *ProviderName, *Model);
	return Result;
}

void FN2CConnectionTest::Run(const FOnDone& OnDone)
{
	UN2CLLMModule* Module = UN2CLLMModule::Get();
	if (Module && !Module->IsInitialized())
	{
		Module->Initialize();
	}

	const FN2CLLMConfig Config = UN2CLLMModule::MakeConfigFromSettings();
	const FString ProviderName = UN2CLLMModule::GetProviderName(Config.Provider);
	UN2CLLMProviderRegistry* Registry = UN2CLLMProviderRegistry::Get();
	if (!Registry || !Registry->IsProviderRegistered(Config.Provider))
	{
		OnDone.ExecuteIfBound({ false, FString::Printf(TEXT("%s is not available in this build."), *ProviderName) });
		return;
	}

	// The service lives until its answer arrives: the strong pointer is held by the callback
	TSharedRef<TStrongObjectPtr<UObject>> Keep = MakeShared<TStrongObjectPtr<UObject>>();
	const TScriptInterface<IN2CLLMService> Service = Registry->CreateProvider(Config.Provider, GetTransientPackage());
	if (!Service.GetInterface() || !Service.GetInterface()->Initialize(Config))
	{
		OnDone.ExecuteIfBound({ false, FString::Printf(TEXT("Could not set up the %s service; check its settings."), *ProviderName) });
		return;
	}
	Keep->Reset(Service.GetObject());

	const EN2CLLMProvider Provider = Config.Provider;
	const FString Model = Config.Model;
	Service.GetInterface()->SendRequest(
		TEXT("Reply with the single word OK."),
		TEXT("This is a connection test from Node to Code. Reply with the single word OK."),
		FOnLLMResponseReceived::CreateLambda([OnDone, Keep, Provider, Model](const FString& Response)
		{
			OnDone.ExecuteIfBound(InterpretResponse(Response, Provider, Model));
			Keep->Reset();
		}));
}
