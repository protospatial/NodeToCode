// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/N2CUserSecrets.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

/**
 * NodeToCode 2.0 authenticates every cloud provider with an API key only: settings and secrets saved while
 * Claude Pro/Max or Google account sign-in existed still load, land on the API key, and lose nothing else;
 * a request without a key fails before it is sent.
 */
BEGIN_DEFINE_SPEC(FN2CApiKeyAuthSpec, "NodeToCode.LLM.Auth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	static FString TempPath(const TCHAR* Extension)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CApiKeyAuthSpec_%s.%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits), Extension));
	}

	static TSharedPtr<FJsonObject> ParseFile(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(Reader, Json);
		}
		return Json;
	}

	template <typename TService>
	TService* MakeService(EN2CLLMProvider Provider, const FString& Model, const FString& ApiKey)
	{
		TService* Service = NewObject<TService>(GetTransientPackage());
		FN2CLLMConfig Config;
		Config.Provider = Provider;
		Config.Model = Model;
		Config.ApiKey = ApiKey;
		Service->Initialize(Config);
		return Service;
	}

END_DEFINE_SPEC(FN2CApiKeyAuthSpec)

void FN2CApiKeyAuthSpec::Define()
{
	Describe(TEXT("SavedSettings"), [this]()
	{
		It(TEXT("loads a config that selected OAuth sign-in and uses the API key instead"), [this]()
		{
			const FString Path = TempPath(TEXT("ini"));
			const FString Ini = FString::Printf(TEXT("[%s]\nProvider=Anthropic\nAnthropicAuthMethod=OAuth\nGeminiAuthMethod=OAuth\n"),
				*UN2CSettings::StaticClass()->GetPathName());
			if (!TestTrue(TEXT("test ini written"), FFileHelper::SaveStringToFile(Ini, *Path))) { return; }

			GConfig->LoadFile(Path);
			UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
			Settings->LoadConfig(nullptr, *Path, UE::LCPF_ReloadingConfigData);
			const TArray<FString> Found = UN2CSettings::FindRemovedSignInSelections(Path);
			GConfig->UnloadFile(Path);
			IFileManager::Get().Delete(*Path);

			TestEqual(TEXT("the rest of the config still loads"), Settings->Provider, EN2CLLMProvider::Anthropic);
			TestEqual(TEXT("both removed sign-in selections are found"), Found, TArray<FString>{ TEXT("Anthropic"), TEXT("Gemini") });
			TestNull(TEXT("no Anthropic sign-in setting is left to show"), UN2CSettings::StaticClass()->FindPropertyByName(TEXT("AnthropicAuthMethod")));
			TestNull(TEXT("no Gemini sign-in setting is left to show"), UN2CSettings::StaticClass()->FindPropertyByName(TEXT("GeminiAuthMethod")));

			// The provider those settings now reach authenticates with its API key
			const FString FakeKey = TEXT("n2c-test-key-not-real");
			UN2CAnthropicService* Anthropic = MakeService<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-sonnet-5-5"), FakeKey);
			TMap<FString, FString> Headers;
			Anthropic->GetProviderHeaders(Headers);
			const FString* ApiKeyHeader = Headers.Find(TEXT("x-api-key"));
			if (TestNotNull(TEXT("Anthropic sends x-api-key"), ApiKeyHeader))
			{
				TestEqual(TEXT("x-api-key carries the key"), *ApiKeyHeader, FakeKey);
			}
			TestFalse(TEXT("Anthropic sends no Authorization header"), Headers.Contains(TEXT("Authorization")));
			TestFalse(TEXT("Anthropic sends no anthropic-beta header"), Headers.Contains(TEXT("anthropic-beta")));

			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
				Anthropic->BuildRequestPayloadForTesting(TEXT("Translate this graph."), TEXT("You are a translator.")));
			if (TestTrue(TEXT("Anthropic payload parses"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
			{
				FString System;
				TestTrue(TEXT("Anthropic system is a plain string"), Root->TryGetStringField(TEXT("system"), System));
				TestEqual(TEXT("system is the prompt alone"), System, FString(TEXT("You are a translator.")));
			}

			UN2CGeminiService* Gemini = MakeService<UN2CGeminiService>(EN2CLLMProvider::Gemini, TEXT("gemini-3-flash-preview"), FakeKey);
			FString Endpoint, AuthToken;
			bool bSupportsSystemPrompts = false;
			Gemini->GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);
			TestTrue(TEXT("Gemini posts to the Generative Language API"), Endpoint.StartsWith(TEXT("https://generativelanguage.googleapis.com/")));
		});

		It(TEXT("finds nothing in a config saved without the removed settings"), [this]()
		{
			const FString Path = TempPath(TEXT("ini"));
			const FString Ini = FString::Printf(TEXT("[%s]\nProvider=Gemini\n"), *UN2CSettings::StaticClass()->GetPathName());
			if (!TestTrue(TEXT("test ini written"), FFileHelper::SaveStringToFile(Ini, *Path))) { return; }
			GConfig->LoadFile(Path);
			const TArray<FString> Found = UN2CSettings::FindRemovedSignInSelections(Path);
			GConfig->UnloadFile(Path);
			IFileManager::Get().Delete(*Path);
			TestEqual(TEXT("no removed selections"), Found.Num(), 0);
		});
	});

	Describe(TEXT("Secrets"), [this]()
	{
		It(TEXT("removes saved sign-in tokens from secrets.json once and keeps every other field"), [this]()
		{
			const FString Path = TempPath(TEXT("json"));
			const FString Saved = TEXT("{\"OpenAI_API_Key\":\"k-openai\",\"Anthropic_API_Key\":\"k-anthropic\",\"Gemini_API_Key\":\"k-gemini\",\"DeepSeek_API_Key\":\"k-deepseek\",")
				TEXT("\"Custom_Field\":\"kept\",")
				TEXT("\"OAuth\":{\"Anthropic\":{\"AccessToken\":\"a\",\"RefreshToken\":\"r\",\"ExpiresAt\":\"\",\"Scope\":\"\"}},")
				TEXT("\"Claude_OAuth_AccessToken\":\"a\",\"Claude_OAuth_RefreshToken\":\"r\",\"Google_OAuth_AccessToken\":\"a\",\"Google_OAuth_Scope\":\"s\"}");
			if (!TestTrue(TEXT("test secrets written"), FFileHelper::SaveStringToFile(Saved, *Path))) { return; }

			AddExpectedMessage(TEXT("sign-in were removed in NodeToCode 2.0"), EAutomationExpectedMessageFlags::Contains, 1);
			UN2CUserSecrets* Secrets = NewObject<UN2CUserSecrets>(GetTransientPackage());
			TestTrue(TEXT("secrets load"), Secrets->LoadSecretsFromFile(Path));
			TestEqual(TEXT("OpenAI key loaded"), Secrets->OpenAI_API_Key, FString(TEXT("k-openai")));
			TestEqual(TEXT("Anthropic key loaded"), Secrets->Anthropic_API_Key, FString(TEXT("k-anthropic")));
			TestEqual(TEXT("Gemini key loaded"), Secrets->Gemini_API_Key, FString(TEXT("k-gemini")));
			TestEqual(TEXT("DeepSeek key loaded"), Secrets->DeepSeek_API_Key, FString(TEXT("k-deepseek")));

			const TSharedPtr<FJsonObject> After = ParseFile(Path);
			if (TestTrue(TEXT("rewritten secrets parse"), After.IsValid()))
			{
				TestFalse(TEXT("OAuth block removed"), After->HasField(TEXT("OAuth")));
				for (const TCHAR* Field : { TEXT("Claude_OAuth_AccessToken"), TEXT("Claude_OAuth_RefreshToken"), TEXT("Google_OAuth_AccessToken"), TEXT("Google_OAuth_Scope") })
				{
					TestFalse(FString::Printf(TEXT("%s removed"), Field), After->HasField(Field));
				}
				TestEqual(TEXT("Anthropic key kept in the file"), After->GetStringField(TEXT("Anthropic_API_Key")), FString(TEXT("k-anthropic")));
				TestEqual(TEXT("DeepSeek key kept in the file"), After->GetStringField(TEXT("DeepSeek_API_Key")), FString(TEXT("k-deepseek")));
				TestEqual(TEXT("unknown field kept"), After->GetStringField(TEXT("Custom_Field")), FString(TEXT("kept")));
			}

			// A second load finds nothing to remove, so it neither rewrites nor logs again
			TestTrue(TEXT("secrets load again"), Secrets->LoadSecretsFromFile(Path));
			TestEqual(TEXT("keys still loaded"), Secrets->Anthropic_API_Key, FString(TEXT("k-anthropic")));

			// Saving keeps the unknown field too
			Secrets->Gemini_API_Key = TEXT("k-gemini-2");
			TestTrue(TEXT("secrets save"), Secrets->SaveSecretsToFile(Path));
			const TSharedPtr<FJsonObject> Resaved = ParseFile(Path);
			if (TestTrue(TEXT("saved secrets parse"), Resaved.IsValid()))
			{
				TestEqual(TEXT("edited key saved"), Resaved->GetStringField(TEXT("Gemini_API_Key")), FString(TEXT("k-gemini-2")));
				TestEqual(TEXT("unknown field survives a save"), Resaved->GetStringField(TEXT("Custom_Field")), FString(TEXT("kept")));
			}
			IFileManager::Get().Delete(*Path);
		});
	});

	Describe(TEXT("MissingKey"), [this]()
	{
		It(TEXT("fails an Anthropic request without an API key before sending it"), [this]()
		{
			AddExpectedMessage(TEXT("No Anthropic API key is set"), EAutomationExpectedMessageFlags::Contains, 1);
			UN2CAnthropicService* Service = MakeService<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-sonnet-5-5"), FString());
			// Shared state: if a regression sends the request, its later callback must not write to this frame
			struct FResult { bool bCompleted = false; FString Response; };
			const TSharedRef<FResult> Result = MakeShared<FResult>();
			Service->SendRequest(TEXT("{}"), TEXT("You are a translator."), FOnLLMResponseReceived::CreateLambda(
				[Result](const FString& InResponse)
				{
					Result->bCompleted = true;
					Result->Response = InResponse;
				}));
			// Completing inside SendRequest means no HTTP request was made
			TestTrue(TEXT("completes immediately, without a request"), Result->bCompleted);
			TestTrue(TEXT("the error names the missing key"), Result->Response.Contains(TEXT("No Anthropic API key is set")));
			TestTrue(TEXT("the error points to the sign-in removal page"), Result->Response.Contains(N2CSignInRemoval::DocsUrl));
		});

		It(TEXT("needs no key for local providers"), [this]()
		{
			TestTrue(TEXT("Ollama"), UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider::Ollama, FString()).IsEmpty());
			TestTrue(TEXT("LM Studio"), UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider::LMStudio, FString()).IsEmpty());
			TestTrue(TEXT("a set key"), UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider::OpenAI, TEXT("k")).IsEmpty());
			TestFalse(TEXT("a blank key"), UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider::DeepSeek, TEXT("  ")).IsEmpty());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
