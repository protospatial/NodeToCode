// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CCredentialSource.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpGetAvailableLLMProvidersTool.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestHttpStub.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

/**
 * Card 0160: every request resolves its key from one credential source (IN2CCredentialSource) when it is sent.
 * - Two requests through one initialized service with a key change in between: the second carries the new key.
 * - With no stored key and ANTHROPIC_API_KEY set, an Anthropic request sends the variable's value in x-api-key; with
 *   neither, it fails before anything is sent, naming the setting and the variable.
 * - A request cancelled while its credential is still resolving sends nothing.
 * - get-available-llm-providers lists a provider whose key is only in the environment, with its origin, never the key.
 * - No config, service, handler or reflected property holds the key after a request.
 * Requests go to a local stub; stores live in a scratch folder with a fake environment.
 */
BEGIN_DEFINE_SPEC(FN2CCredentialSourceSpec, "NodeToCode.LLM.CredentialSource", N2C_TEST_FLAGS)

	static constexpr const TCHAR* FirstKey = TEXT("n2cFAKEKEY0160first5e2a91");
	static constexpr const TCHAR* SecondKey = TEXT("n2cFAKEKEY0160second7c4d03");
	static constexpr const TCHAR* EnvKey = TEXT("n2cFAKEKEY0160environment3b8f17");

	/** One request the stub received: its headers (names as sent) */
	struct FSeenRequest
	{
		TArray<TPair<FString, FString>> Headers;

		TArray<FString> ValuesOf(const FString& Name) const
		{
			TArray<FString> Values;
			for (const TPair<FString, FString>& Header : Headers)
			{
				if (Header.Key.Equals(Name, ESearchCase::IgnoreCase))
				{
					Values.Add(Header.Value);
				}
			}
			return Values;
		}

		/** The headers whose value contains Text */
		TArray<FString> Carrying(const FString& Text) const
		{
			TArray<FString> Names;
			for (const TPair<FString, FString>& Header : Headers)
			{
				if (Header.Value.Contains(Text))
				{
					Names.Add(Header.Key);
				}
			}
			return Names;
		}
	};

	/** A local endpoint that records each POST to /n2c/llm and answers 200 at once */
	struct FStub
	{
		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;
		TSharedRef<TArray<FSeenRequest>> Seen = MakeShared<TArray<FSeenRequest>>();

		bool Start()
		{
			FHttpServerModule& Server = FHttpServerModule::Get();
			// A port the OS picked, not a random one in a range another editor's stub may hold (parallel checkouts)
			if (!Router.IsValid())
			{
				Router = N2CTest::StartStubRouter(Port);
			}
			if (!Router.IsValid())
			{
				return false;
			}
			TSharedRef<TArray<FSeenRequest>> SeenRef = Seen;
			Route = Router->BindRoute(FHttpPath(TEXT("/n2c/llm")), EHttpServerRequestVerbs::VERB_POST,
				FHttpRequestHandler::CreateLambda([SeenRef](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
				{
					FSeenRequest Record;
					for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
					{
						for (const FString& Value : Header.Value)
						{
							Record.Headers.Emplace(Header.Key, Value);
						}
					}
					SeenRef->Add(MoveTemp(Record));
					OnComplete(FHttpServerResponse::Create(TEXT("{\"ok\":true}"), TEXT("application/json")));
					return true;
				}));
			Server.StartAllListeners();
			return Route.IsValid();
		}

		void Stop()
		{
			if (Router.IsValid() && Route.IsValid())
			{
				Router->UnbindRoute(Route);
			}
			Route.Reset();
		}

		FString Url() const { return FString::Printf(TEXT("http://127.0.0.1:%u/n2c/llm"), Port); }
	};

	/** Ticks the HTTP server and client until Seconds pass or Until is true */
	static void Pump(double Seconds, TFunctionRef<bool()> Until)
	{
		FHttpManager& Manager = FHttpModule::Get().GetHttpManager();
		FTSTickerObjectBase& ServerTicker = FHttpServerModule::Get();
		const double End = FPlatformTime::Seconds() + Seconds;
		double Last = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() < End && !Until())
		{
			FPlatformProcess::Sleep(0.005f);
			const double Now = FPlatformTime::Seconds();
			const float Delta = static_cast<float>(Now - Last);
			Last = Now;
			ServerTicker.Tick(Delta);
			Manager.Tick(Delta);
		}
	}

	template <typename TService>
	static TService* MakeService(EN2CLLMProvider Provider, const FString& Model, const FString& Endpoint,
		TSharedPtr<const IN2CCredentialSource> Source)
	{
		TService* Service = NewObject<TService>(GetTransientPackage());
		Service->SetCredentialSource(MoveTemp(Source));
		FN2CLLMConfig Config;
		Config.Provider = Provider;
		Config.Model = Model;
		Config.ApiEndpoint = Endpoint;
		Config.TimeoutSeconds = 30.0f;
		Config.ActivityTimeoutSeconds = 30.0f;
		Service->Initialize(Config);
		return Service;
	}

	struct FResult
	{
		bool bDone = false;
		FString Response;
	};

	/** Sends one request through Service; Result fills in when it completes */
	static FN2CLLMRequestHandleRef Send(UN2CBaseLLMService* Service, const TSharedRef<FResult>& Result)
	{
		return Service->SendRequest(TEXT("<n2c_json>{}</n2c_json>"), TEXT("You translate Blueprint graphs."),
			FOnLLMResponseReceived::CreateLambda([Result](const FString& Response)
			{
				Result->bDone = true;
				Result->Response = Response;
			}));
	}

	/** Sends one request through Service and waits for it */
	static FString SendAndWait(UN2CBaseLLMService* Service)
	{
		const TSharedRef<FResult> Result = MakeShared<FResult>();
		FN2CLLMRequestHandleRef Handle = Send(Service, Result);
		Pump(20.0, [Result] { return Result->bDone; });
		if (!Result->bDone)
		{
			Handle->Cancel();
		}
		return Result->Response;
	}

	/** A source that keeps each Resolve's callback until Answer is called (a keychain or token refresh that is slow) */
	struct FDeferredSource : public IN2CCredentialSource
	{
		mutable TArray<FOnResolved> Pending;

		virtual void Resolve(EN2CLLMProvider Provider, FOnResolved OnResolved) const override
		{
			Pending.Add(MoveTemp(OnResolved));
		}
		virtual EN2CApiKeySource GetOrigin(EN2CLLMProvider, FString* OutVariableName) const override
		{
			if (OutVariableName) { OutVariableName->Reset(); }
			return EN2CApiKeySource::Store;
		}
		void Answer(EN2CLLMProvider Provider, const FString& Key)
		{
			TArray<FOnResolved> ToRun = MoveTemp(Pending);
			for (FOnResolved& OnResolved : ToRun)
			{
				OnResolved(MakeValue(FN2CCredential::Make(Provider, Key)));
			}
		}
	};

	TMap<FString, FString> FakeEnvironment;

	/** A store in Scratch with a fake environment (FakeEnvironment, read at call time) */
	TUniquePtr<FN2CSecretStore> MakeStore(const N2CTest::FScratchDir& Scratch)
	{
		return MakeUnique<FN2CSecretStore>(MakeUnique<FN2CSecretFileBackend>(Scratch / TEXT("secrets.json")),
			[this](const FString& Name)
			{
				const FString* Value = FakeEnvironment.Find(Name);
				return Value ? *Value : FString();
			});
	}

	/** Every property's text form on Object (and its HTTP handler, parser and prompt manager) */
	static FString ExportAll(const UObject* Object)
	{
		FString Out;
		if (!Object)
		{
			return Out;
		}
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FString Value;
			It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Object), nullptr, nullptr, PPF_None);
			Out += It->GetName() + TEXT("=") + Value + TEXT("\n");
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(*It))
			{
				const UObject* Child = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
				if (Child && Child->GetOuter() == Object)
				{
					Out += ExportAll(Child);
				}
			}
		}
		return Out;
	}

END_DEFINE_SPEC(FN2CCredentialSourceSpec)

void FN2CCredentialSourceSpec::Define()
{
	BeforeEach([this]()
	{
		FakeEnvironment.Reset();
	});

	Describe(TEXT("PerRequest"), [this]()
	{
		It(TEXT("sends the new key on the next request after the key changes, through one initialized service"), [this]()
		{
			FStub Stub;
			if (!TestTrue(TEXT("stub listening"), Stub.Start()))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };

			const TSharedRef<FN2CFixedCredentialSource> Source = MakeShared<FN2CFixedCredentialSource>(EN2CLLMProvider::Anthropic, FirstKey);
			UN2CAnthropicService* Service = MakeService<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-haiku-5-5"), Stub.Url(), Source);
			TestEqual(TEXT("Initialize resolves no key"), Source->GetResolveCount(), 0);

			SendAndWait(Service);
			Source->SetKey(EN2CLLMProvider::Anthropic, SecondKey);
			SendAndWait(Service);

			TestEqual(TEXT("one resolve per request"), Source->GetResolveCount(), 2);
			if (!TestEqual(TEXT("the stub received both requests"), Stub.Seen->Num(), 2))
			{
				return;
			}
			const FSeenRequest& First = (*Stub.Seen)[0];
			const FSeenRequest& Second = (*Stub.Seen)[1];
			TestEqual(TEXT("the first request carries the first key"), First.ValuesOf(TEXT("x-api-key")), TArray<FString>{ FirstKey });
			TestEqual(TEXT("the second request carries the new key"), Second.ValuesOf(TEXT("x-api-key")), TArray<FString>{ SecondKey });
			TestEqual(TEXT("the old key is gone from the second request"), Second.Carrying(FirstKey).Num(), 0);
			// Card 0159's rule still holds: one credential header, and no Authorization for Anthropic
			TestEqual(TEXT("one header carries the key"), Second.Carrying(SecondKey).Num(), 1);
			TestEqual(TEXT("no Authorization header"), Second.ValuesOf(TEXT("Authorization")).Num(), 0);
			TestEqual(TEXT("anthropic-version still sent"), Second.ValuesOf(TEXT("anthropic-version")).Num(), 1);
		});

		It(TEXT("sends a Bearer key in exactly one Authorization header"), [this]()
		{
			FStub Stub;
			if (!TestTrue(TEXT("stub listening"), Stub.Start()))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			UN2COpenAIService* Service = MakeService<UN2COpenAIService>(EN2CLLMProvider::OpenAI, TEXT("gpt-6.1-sol"), Stub.Url(),
				MakeShared<FN2CFixedCredentialSource>(EN2CLLMProvider::OpenAI, FirstKey));
			SendAndWait(Service);
			if (TestEqual(TEXT("the stub received the request"), Stub.Seen->Num(), 1))
			{
				TestEqual(TEXT("Authorization: Bearer <key>, once"), (*Stub.Seen)[0].ValuesOf(TEXT("Authorization")),
					TArray<FString>{ FString::Printf(TEXT("Bearer %s"), FirstKey) });
				TestEqual(TEXT("no other header carries the key"), (*Stub.Seen)[0].Carrying(FirstKey).Num(), 1);
			}
		});

		It(TEXT("sends nothing for a request cancelled while its credential resolves"), [this]()
		{
			FStub Stub;
			if (!TestTrue(TEXT("stub listening"), Stub.Start()))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			const TSharedRef<FDeferredSource> Source = MakeShared<FDeferredSource>();
			UN2CAnthropicService* Service = MakeService<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-haiku-5-5"), Stub.Url(), Source);

			const TSharedRef<FResult> Cancelled = MakeShared<FResult>();
			FN2CLLMRequestHandleRef Handle = Send(Service, Cancelled);
			TestFalse(TEXT("pending while the key resolves"), Handle->IsFinished());
			TestTrue(TEXT("cancel wins"), Handle->Cancel());
			Source->Answer(EN2CLLMProvider::Anthropic, FirstKey);
			Pump(1.0, [] { return false; });
			TestEqual(TEXT("nothing was sent"), Stub.Seen->Num(), 0);
			TestFalse(TEXT("the cancelled request's callback never ran"), Cancelled->bDone);

			// A request that is not cancelled goes out once its key arrives
			const TSharedRef<FResult> Sent = MakeShared<FResult>();
			Send(Service, Sent);
			Source->Answer(EN2CLLMProvider::Anthropic, SecondKey);
			Pump(20.0, [Sent] { return Sent->bDone; });
			if (TestEqual(TEXT("the second request was sent"), Stub.Seen->Num(), 1))
			{
				TestEqual(TEXT("with the key resolved for it"), (*Stub.Seen)[0].ValuesOf(TEXT("x-api-key")), TArray<FString>{ SecondKey });
			}
		});
	});

	Describe(TEXT("Environment"), [this]()
	{
		It(TEXT("sends ANTHROPIC_API_KEY when no key is stored, and fails before sending when there is neither"), [this]()
		{
			FStub Stub;
			if (!TestTrue(TEXT("stub listening"), Stub.Start()))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Scratch(TEXT("N2CCredentialSourceSpec"));
			TUniquePtr<FN2CSecretStore> Store = MakeStore(Scratch);
			const TSharedRef<FN2CSecretStoreCredentialSource> Source = MakeShared<FN2CSecretStoreCredentialSource>(*Store);
			UN2CAnthropicService* Service = MakeService<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-haiku-5-5"), Stub.Url(), Source);

			// Neither a stored key nor the variable: an Auth error before any request, naming the setting and the variable
			AddExpectedMessage(TEXT("No Anthropic API key is set"), EAutomationExpectedMessageFlags::Contains, 1);
			const TSharedRef<FResult> Missing = MakeShared<FResult>();
			Send(Service, Missing);
			TestTrue(TEXT("completes inside SendRequest, without a request"), Missing->bDone);
			TestTrue(TEXT("names the setting"), Missing->Response.Contains(TEXT("Project Settings > Plugins > Node to Code > LLM Services > Anthropic > API Key")));
			TestTrue(TEXT("names the variable"), Missing->Response.Contains(TEXT("ANTHROPIC_API_KEY")));
			TestTrue(TEXT("is an Auth error"), Missing->Response.Contains(TEXT("\"error_kind\":\"Auth\"")));
			Pump(0.5, [] { return false; });
			TestEqual(TEXT("nothing was sent"), Stub.Seen->Num(), 0);

			// The variable set later (no stored key) is used by the same service's next request
			FakeEnvironment.Add(TEXT("ANTHROPIC_API_KEY"), EnvKey);
			FString Variable;
			TestEqual(TEXT("the origin is the environment"), Source->GetOrigin(EN2CLLMProvider::Anthropic, &Variable), EN2CApiKeySource::Environment);
			TestEqual(TEXT("named by its variable"), Variable, FString(TEXT("ANTHROPIC_API_KEY")));
			SendAndWait(Service);
			if (TestEqual(TEXT("the request was sent"), Stub.Seen->Num(), 1))
			{
				TestEqual(TEXT("x-api-key carries the variable's value"), (*Stub.Seen)[0].ValuesOf(TEXT("x-api-key")), TArray<FString>{ EnvKey });
			}

			// A stored key wins over the variable, again without re-initializing
			TestTrue(TEXT("a key saves"), Store->SetApiKey(EN2CLLMProvider::Anthropic, FirstKey));
			SendAndWait(Service);
			if (TestEqual(TEXT("the third request was sent"), Stub.Seen->Num(), 2))
			{
				TestEqual(TEXT("the stored key wins"), (*Stub.Seen)[1].ValuesOf(TEXT("x-api-key")), TArray<FString>{ FirstKey });
			}
		});

		It(TEXT("reads GEMINI_API_KEY, then GOOGLE_API_KEY, for Gemini"), [this]()
		{
			N2CTest::FScratchDir Scratch(TEXT("N2CCredentialSourceSpec"));
			TUniquePtr<FN2CSecretStore> Store = MakeStore(Scratch);
			const FN2CSecretStoreCredentialSource Source(*Store);
			FakeEnvironment.Add(TEXT("GOOGLE_API_KEY"), SecondKey);
			FN2CCredentialResult Google = Source.ResolveNow(EN2CLLMProvider::Gemini);
			if (TestTrue(TEXT("GOOGLE_API_KEY resolves"), Google.HasValue()))
			{
				TestEqual(TEXT("its value"), Google.GetValue().Value, FString(SecondKey));
				TestEqual(TEXT("its variable"), Google.GetValue().VariableName, FString(TEXT("GOOGLE_API_KEY")));
				TestEqual(TEXT("sent in x-goog-api-key"), Google.GetValue().AuthStyle, EN2CAuthStyle::XGoogApiKey);
			}
			FakeEnvironment.Add(TEXT("GEMINI_API_KEY"), FirstKey);
			FN2CCredentialResult Gemini = Source.ResolveNow(EN2CLLMProvider::Gemini);
			if (TestTrue(TEXT("GEMINI_API_KEY resolves"), Gemini.HasValue()))
			{
				TestEqual(TEXT("GEMINI_API_KEY wins"), Gemini.GetValue().Value, FString(FirstKey));
			}
		});

		It(TEXT("lists a provider whose key is only in the environment, with its origin and never the key"), [this]()
		{
			N2CTest::FScratchDir Scratch(TEXT("N2CCredentialSourceSpec"));
			TUniquePtr<FN2CSecretStore> Store = MakeStore(Scratch);
			FakeEnvironment.Add(TEXT("DEEPSEEK_API_KEY"), EnvKey);
			TestTrue(TEXT("an OpenAI key saves"), Store->SetApiKey(EN2CLLMProvider::OpenAI, FirstKey));

			FN2CMcpGetAvailableLLMProvidersTool Tool;
			Tool.SetCredentialSource(MakeShared<FN2CSecretStoreCredentialSource>(*Store));
			const FMcpToolCallResult Result = Tool.Execute(MakeShared<FJsonObject>());
			FString Text;
			if (!TestTrue(TEXT("the tool answers with text"), !Result.bIsError && Result.Content.Num() > 0
				&& Result.Content[0].IsValid() && Result.Content[0]->TryGetStringField(TEXT("text"), Text)))
			{
				return;
			}
			TestFalse(TEXT("the result holds no environment key"), Text.Contains(EnvKey));
			TestFalse(TEXT("the result holds no stored key"), Text.Contains(FirstKey));

			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (!TestTrue(TEXT("the result parses"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
			{
				return;
			}
			TMap<FString, TSharedPtr<FJsonObject>> ById;
			for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("providers")))
			{
				const TSharedPtr<FJsonObject> Provider = Value->AsObject();
				ById.Add(Provider->GetStringField(TEXT("id")), Provider);
			}
			const TSharedPtr<FJsonObject>* DeepSeek = ById.Find(TEXT("deepseek"));
			if (TestNotNull(TEXT("DeepSeek is listed with only an environment key"), DeepSeek))
			{
				TestEqual(TEXT("its keySource"), (*DeepSeek)->GetStringField(TEXT("keySource")), FString(TEXT("environment")));
				TestEqual(TEXT("its keyVariable"), (*DeepSeek)->GetStringField(TEXT("keyVariable")), FString(TEXT("DEEPSEEK_API_KEY")));
			}
			const TSharedPtr<FJsonObject>* OpenAI = ById.Find(TEXT("openai"));
			if (TestNotNull(TEXT("OpenAI is listed with a saved key"), OpenAI))
			{
				TestEqual(TEXT("its keySource"), (*OpenAI)->GetStringField(TEXT("keySource")), FString(TEXT("saved")));
			}
			TestFalse(TEXT("Anthropic, with no key, is not listed"), ById.Contains(TEXT("anthropic")));
			TestFalse(TEXT("Gemini, with no key, is not listed"), ById.Contains(TEXT("gemini")));
			TestTrue(TEXT("Ollama is listed"), ById.Contains(TEXT("ollama")));
		});
	});

	Describe(TEXT("Reflection"), [this]()
	{
		It(TEXT("leaves no key in a config, service or HTTP handler, and exposes no key through reflection"), [this]()
		{
			TestNull(TEXT("FN2CLLMConfig has no ApiKey"), FN2CLLMConfig::StaticStruct()->FindPropertyByName(TEXT("ApiKey")));

			FStub Stub;
			if (!TestTrue(TEXT("stub listening"), Stub.Start()))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			UN2CAnthropicService* Service = MakeService<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-haiku-5-5"), Stub.Url(),
				MakeShared<FN2CFixedCredentialSource>(EN2CLLMProvider::Anthropic, FirstKey));
			SendAndWait(Service);
			TestEqual(TEXT("the request was sent with the key"), Stub.Seen->Num() == 1 ? (*Stub.Seen)[0].ValuesOf(TEXT("x-api-key")).Num() : 0, 1);
			const FString Exported = ExportAll(Service);
			TestTrue(TEXT("the service's properties were read"), Exported.Contains(TEXT("HttpHandler=")));
			TestFalse(TEXT("no property of the service, its handler or parser holds the key"), Exported.Contains(FirstKey));

			// The module's config is what GetConfig (BlueprintCallable) returns
			FString ConfigText;
			FN2CLLMConfig::StaticStruct()->ExportText(ConfigText, &UN2CLLMModule::Get()->GetConfig(), nullptr, nullptr, PPF_None, nullptr);
			TestFalse(TEXT("GetConfig holds no key"), ConfigText.Contains(FirstKey));

			// No Blueprint-visible property and no Blueprint-callable function of the module is a key, except the four
			// Project Settings key fields that card 0161 makes write-only (Transient, PasswordField, no Blueprint access)
			const TSet<FString> SettingsKeyFields = { TEXT("OpenAI_API_Key_UI"), TEXT("Anthropic_API_Key_UI"),
				TEXT("Gemini_API_Key_UI"), TEXT("DeepSeek_API_Key_UI") };
			const UPackage* Package = FindPackage(nullptr, TEXT("/Script/NodeToCode"));
			if (!TestNotNull(TEXT("the module's script package"), Package))
			{
				return;
			}
			auto IsKeyName = [](const FString& Name)
			{
				return Name.Contains(TEXT("ApiKey")) || Name.Contains(TEXT("API_Key")) || Name.Contains(TEXT("Credential"));
			};
			TArray<FString> Exposed;
			for (TObjectIterator<UStruct> It; It; ++It)
			{
				if (It->GetPackage() != Package)
				{
					continue;
				}
				for (TFieldIterator<FProperty> Prop(*It, EFieldIteratorFlags::ExcludeSuper); Prop; ++Prop)
				{
					if (Prop->HasAnyPropertyFlags(CPF_BlueprintVisible) && IsKeyName(Prop->GetName())
						&& !Prop->GetName().EndsWith(TEXT("_Source")) && !SettingsKeyFields.Contains(Prop->GetName()))
					{
						Exposed.Add(It->GetName() + TEXT(".") + Prop->GetName());
					}
				}
				if (const UFunction* Function = Cast<UFunction>(*It))
				{
					if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) && IsKeyName(Function->GetName())
						&& Function->GetReturnProperty() && Function->GetReturnProperty()->IsA<FStrProperty>())
					{
						Exposed.Add(Function->GetPathName());
					}
				}
			}
			TestEqual(FString::Printf(TEXT("no reflected key accessor (found: %s)"), *FString::Join(Exposed, TEXT(", "))), Exposed.Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
