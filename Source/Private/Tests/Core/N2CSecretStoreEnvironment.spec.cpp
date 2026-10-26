// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CCredentialSource.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

/**
 * When the secret store has no key for a provider, requests use the provider's environment variable
 * (ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY or GOOGLE_API_KEY, DEEPSEEK_API_KEY or DEEPSEEK_API); a stored key always wins,
 * an environment key is never written to the store, and logs and the settings panel name the variable, never its value.
 * Every store here works in its own temporary folder; the process environment is restored after each case.
 */
BEGIN_DEFINE_SPEC(FN2CSecretStoreEnvironmentSpec, "NodeToCode.Secrets.Environment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	FString BaseDir;
	FString UserFile;
	TMap<FString, FString> FakeEnvironment;

	TUniquePtr<FN2CSecretStore> MakeStore()
	{
		// The reader looks the map up at call time, so a case can change the environment after making the store
		return MakeUnique<FN2CSecretStore>(MakeUnique<FN2CSecretFileBackend>(UserFile),
			[this](const FString& Name)
			{
				const FString* Value = FakeEnvironment.Find(Name);
				return Value ? *Value : FString();
			});
	}

	static FString ReadFile(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	/** Collects every log line written while it is registered */
	struct FLogCapture : public FOutputDevice
	{
		TArray<FString> Lines;
		FLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override { Lines.Add(V); }
		int32 CountContaining(const FString& Text) const
		{
			GLog->Flush();
			int32 Count = 0;
			for (const FString& Line : Lines)
			{
				Count += Line.Contains(Text) ? 1 : 0;
			}
			return Count;
		}
	};

	/** Sets real process environment variables for one case and puts the previous values back afterwards */
	struct FScopedEnvironment
	{
		TMap<FString, FString> Previous;
		void Set(const FString& Name, const FString& Value)
		{
			if (!Previous.Contains(Name))
			{
				Previous.Add(Name, FPlatformMisc::GetEnvironmentVariable(*Name));
			}
			FPlatformMisc::SetEnvironmentVar(*Name, *Value);
		}
		~FScopedEnvironment()
		{
			for (const TPair<FString, FString>& Pair : Previous)
			{
				FPlatformMisc::SetEnvironmentVar(*Pair.Key, *Pair.Value);
			}
		}
	};

END_DEFINE_SPEC(FN2CSecretStoreEnvironmentSpec)

void FN2CSecretStoreEnvironmentSpec::Define()
{
	BeforeEach([this]()
	{
		BaseDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CSecretStoreEnvironmentSpec_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		UserFile = BaseDir / TEXT("User") / TEXT("NodeToCode") / TEXT("secrets.json");
		FakeEnvironment.Reset();
	});

	AfterEach([this]()
	{
		IFileManager::Get().DeleteDirectory(*BaseDir, false, true);
	});

	It(TEXT("names the standard variables for each cloud provider and none for local ones"), [this]()
	{
		TestEqual(TEXT("OpenAI"), FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider::OpenAI), TArray<FString>{ TEXT("OPENAI_API_KEY") });
		TestEqual(TEXT("Anthropic"), FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider::Anthropic), TArray<FString>{ TEXT("ANTHROPIC_API_KEY") });
		TestEqual(TEXT("Gemini, then Google's SDK name"), FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider::Gemini),
			TArray<FString>{ TEXT("GEMINI_API_KEY"), TEXT("GOOGLE_API_KEY") });
		TestEqual(TEXT("DeepSeek, then the .env alias"), FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider::DeepSeek),
			TArray<FString>{ TEXT("DEEPSEEK_API_KEY"), TEXT("DEEPSEEK_API") });
		TestEqual(TEXT("Ollama"), FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider::Ollama).Num(), 0);
		TestEqual(TEXT("LM Studio"), FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider::LMStudio).Num(), 0);
	});

	It(TEXT("uses the environment key when nothing is stored, and a stored key overrides it"), [this]()
	{
		FakeEnvironment.Add(TEXT("ANTHROPIC_API_KEY"), TEXT("  k-env-anthropic \n"));
		TUniquePtr<FN2CSecretStore> Store = MakeStore();

		FString Variable;
		TestEqual(TEXT("nothing is stored"), Store->GetStoredApiKey(EN2CLLMProvider::Anthropic), FString());
		TestEqual(TEXT("the environment key is used, trimmed"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("k-env-anthropic")));
		TestTrue(TEXT("the provider counts as having a key"), Store->HasApiKey(EN2CLLMProvider::Anthropic));
		TestEqual(TEXT("the source is the environment"), Store->GetApiKeySource(EN2CLLMProvider::Anthropic, &Variable), EN2CApiKeySource::Environment);
		TestEqual(TEXT("the source names the variable"), Variable, FString(TEXT("ANTHROPIC_API_KEY")));
		TestFalse(TEXT("reading the environment key writes no file"), FPaths::FileExists(UserFile));

		TestEqual(TEXT("a provider with neither has no key"), Store->GetApiKeySource(EN2CLLMProvider::OpenAI, &Variable), EN2CApiKeySource::None);
		TestTrue(TEXT("and no variable name"), Variable.IsEmpty());
		TestTrue(TEXT("and an empty key"), Store->GetApiKey(EN2CLLMProvider::OpenAI).IsEmpty());

		TestTrue(TEXT("a key saves"), Store->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("k-stored-anthropic")));
		TestEqual(TEXT("the stored key wins"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("k-stored-anthropic")));
		TestEqual(TEXT("the source is the store"), Store->GetApiKeySource(EN2CLLMProvider::Anthropic, &Variable), EN2CApiKeySource::Store);
		TestTrue(TEXT("with no variable name"), Variable.IsEmpty());
		TestFalse(TEXT("the environment key never reaches the file"), ReadFile(UserFile).Contains(TEXT("k-env-anthropic")));

		TestTrue(TEXT("clearing the key saves"), Store->SetApiKey(EN2CLLMProvider::Anthropic, FString()));
		TestEqual(TEXT("the environment key is used again"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("k-env-anthropic")));

		TestTrue(TEXT("a blank key saves"), Store->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("   ")));
		TestEqual(TEXT("a blank stored key does not hide the environment key"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("k-env-anthropic")));
	});

	It(TEXT("reads DEEPSEEK_API when DEEPSEEK_API_KEY is not set, and prefers DEEPSEEK_API_KEY"), [this]()
	{
		FakeEnvironment.Add(TEXT("DEEPSEEK_API"), TEXT("k-env-alias"));
		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		FString Variable;
		TestEqual(TEXT("the alias is used"), Store->GetApiKey(EN2CLLMProvider::DeepSeek), FString(TEXT("k-env-alias")));
		Store->GetApiKeySource(EN2CLLMProvider::DeepSeek, &Variable);
		TestEqual(TEXT("and named"), Variable, FString(TEXT("DEEPSEEK_API")));

		FakeEnvironment.Add(TEXT("DEEPSEEK_API_KEY"), TEXT("k-env-standard"));
		TestEqual(TEXT("the standard name wins"), Store->GetApiKey(EN2CLLMProvider::DeepSeek), FString(TEXT("k-env-standard")));
		Store->GetApiKeySource(EN2CLLMProvider::DeepSeek, &Variable);
		TestEqual(TEXT("and is named"), Variable, FString(TEXT("DEEPSEEK_API_KEY")));

		FakeEnvironment.Add(TEXT("DEEPSEEK_API_KEY"), TEXT("   "));
		TestEqual(TEXT("a blank standard variable falls through to the alias"), Store->GetApiKey(EN2CLLMProvider::DeepSeek), FString(TEXT("k-env-alias")));
	});

	It(TEXT("logs the variable's name once and never its value"), [this]()
	{
		const FString Value = TEXT("k-env-gemini-must-not-be-logged");
		FakeEnvironment.Add(TEXT("GEMINI_API_KEY"), Value);
		TUniquePtr<FN2CSecretStore> Store = MakeStore();

		FN2CLogger::Get().SetMinSeverity(EN2CLogSeverity::Info);
		{
			FLogCapture Log;
			Store->GetApiKey(EN2CLLMProvider::Gemini);
			Store->GetApiKey(EN2CLLMProvider::Gemini);
			Store->GetApiKeySource(EN2CLLMProvider::Gemini);
			TestEqual(TEXT("the fallback is logged once, by variable name"), Log.CountContaining(TEXT("the GEMINI_API_KEY environment variable is used")), 1);
			TestEqual(TEXT("the value is never logged"), Log.CountContaining(Value), 0);
		}
		FN2CLogger::Get().SetMinSeverity(GetDefault<UN2CSettings>()->MinSeverity);
	});

	It(TEXT("puts a key from the process environment on the request and shows only its source in settings"), [this]()
	{
		const FString FakeKey = TEXT("n2c-test-env-key-not-real");
		FScopedEnvironment Environment;
		Environment.Set(TEXT("ANTHROPIC_API_KEY"), FakeKey);

		// The process-wide store reads the real environment; a key saved on this machine still wins over it
		const FString Stored = FN2CSecretStore::Get().GetStoredApiKey(EN2CLLMProvider::Anthropic);
		const bool bHasStored = !Stored.TrimStartAndEnd().IsEmpty();
		const FString Expected = bHasStored ? Stored : FakeKey;

		// The credential source every request resolves its key from
		FString ApiKey;
		IN2CCredentialSource::Get()->Resolve(EN2CLLMProvider::Anthropic, [this, &ApiKey, &Expected, bHasStored](FN2CCredentialResult Result)
		{
			if (!TestTrue(TEXT("the credential resolves"), Result.HasValue()))
			{
				return;
			}
			const FN2CCredential& Credential = Result.GetValue();
			ApiKey = Credential.Value;
			TestEqual(TEXT("the source gives the key requests use"), Credential.Value, Expected);
			TestEqual(TEXT("and says where it comes from"), Credential.Origin, bHasStored ? EN2CApiKeySource::Store : EN2CApiKeySource::Environment);

			TMap<FString, FString> Headers;
			for (const TPair<FString, FString>& Header : UN2CHttpHandlerBase::BuildRequestHeaders({}, Credential))
			{
				Headers.Add(Header.Key, Header.Value);
			}
			const FString* Header = Headers.Find(TEXT("x-api-key"));
			if (TestNotNull(TEXT("the request sends x-api-key"), Header))
			{
				TestEqual(TEXT("x-api-key carries the environment key (or the stored one)"), *Header, Expected);
			}
		});
		TestTrue(TEXT("no missing-key error"), UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider::Anthropic, ApiKey).IsEmpty());

		const FString Source = UN2CSettings::DescribeApiKeySource(EN2CLLMProvider::Anthropic);
		TestFalse(TEXT("the settings source never shows the key"), Source.Contains(FakeKey));
		if (!bHasStored)
		{
			TestTrue(TEXT("the settings source names the variable"), Source.Contains(TEXT("ANTHROPIC_API_KEY")));
			UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
			TestTrue(TEXT("the API Key field stays empty"), Settings->Anthropic_API_Key_UI.IsEmpty());
			TestTrue(TEXT("the API Key Source field names the variable"), Settings->Anthropic_API_Key_Source.Contains(TEXT("ANTHROPIC_API_KEY")));
		}
	});

	It(TEXT("names the environment variables in the missing-key error"), [this]()
	{
		const FString Error = UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider::DeepSeek, FString());
		TestTrue(TEXT("names DEEPSEEK_API_KEY"), Error.Contains(TEXT("DEEPSEEK_API_KEY")));
		TestTrue(TEXT("still starts with the old wording"), Error.StartsWith(TEXT("No DeepSeek API key is set")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
