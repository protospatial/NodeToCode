// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CSecretStore.h"
#include "LLM/N2CLLMTypes.h"
#include "Templates/ValueOrError.h"

/** How a request carries its key: the one credential header a provider takes */
enum class EN2CAuthStyle : uint8
{
    /** No credential header */
    None,
    /** x-api-key: <key> (Anthropic) */
    XApiKey,
    /** Authorization: Bearer <key> (OpenAI, DeepSeek, and LM Studio or Ollama when a key is set) */
    Bearer,
    /** x-goog-api-key: <key> (Gemini) */
    XGoogApiKey,
};

/**
 * The credential one request is sent with. Lives only for that request: nothing reflected (no UPROPERTY, no
 * Blueprint or Python access) ever holds it, and logs name its origin and variable, never its value.
 */
struct NODETOCODE_API FN2CCredential
{
    /** The key; empty for a local provider without one */
    FString Value;

    /** The header the key goes in */
    EN2CAuthStyle AuthStyle = EN2CAuthStyle::None;

    /** Where the key came from: the secret store (Project Settings) or an environment variable */
    EN2CApiKeySource Origin = EN2CApiKeySource::None;

    /** The environment variable's name when Origin is Environment */
    FString VariableName;

    /** A credential for Provider carrying Key (in Provider's header) */
    static FN2CCredential Make(EN2CLLMProvider Provider, const FString& Key, EN2CApiKeySource InOrigin = EN2CApiKeySource::Store);

    /** The header Provider's key goes in (EN2CAuthStyle) */
    static EN2CAuthStyle GetAuthStyle(EN2CLLMProvider Provider);

    /**
     * The header this credential is sent in.
     * @return false when there is none (no auth style, or an empty key)
     */
    bool GetHeader(FString& OutName, FString& OutValue) const;

    /** True for a header name that carries a credential (Authorization, x-api-key or x-goog-api-key, any case) */
    static bool IsCredentialHeader(const FString& HeaderName);
};

/** A resolved credential, or why there is none (a message for the user that names the setting and the variables) */
using FN2CCredentialResult = TValueOrError<FN2CCredential, FText>;

/**
 * The one way a request gets its key. UN2CBaseLLMService::SendRequest resolves the provider's credential right before
 * every request is sent, so a key changed in Project Settings or in the environment applies to the next request
 * (including the next item of a running batch) without re-initializing anything, and no service, config or HTTP
 * handler keeps a key between requests.
 *
 * The process default (Get) reads FN2CSecretStore: the key saved in Project Settings, else the provider's environment
 * variable (FN2CSecretStore::GetEnvironmentVariableNames). Card 0161's OS keychain plugs in behind the store
 * (IN2CSecretBackend), so this interface does not change. Tests and integrations can give a service another source
 * with UN2CBaseLLMService::SetCredentialSource.
 */
class NODETOCODE_API IN2CCredentialSource
{
public:
    /** Receives the result of one Resolve: called exactly once, on the game thread */
    using FOnResolved = TFunction<void(FN2CCredentialResult)>;

    virtual ~IN2CCredentialSource() = default;

    /**
     * Resolves Provider's credential for one request. OnResolved runs exactly once, on the game thread, and may run
     * before Resolve returns (the default source answers at once; a keychain or token refresh may answer later).
     * The result is an error when Provider needs a key and has none; a local provider (Ollama, LM Studio) without a
     * key resolves to a credential with an empty value.
     */
    virtual void Resolve(EN2CLLMProvider Provider, FOnResolved OnResolved) const = 0;

    /**
     * Where Provider's key would come from now, without reading its value.
     * @param OutVariableName receives the variable's name when the origin is Environment
     */
    virtual EN2CApiKeySource GetOrigin(EN2CLLMProvider Provider, FString* OutVariableName = nullptr) const = 0;

    /** True when a request to Provider can be authenticated: it has a key, or needs none (Ollama, LM Studio) */
    bool Has(EN2CLLMProvider Provider) const;

    /** True for providers whose requests fail without a key (the cloud providers) */
    static bool RequiresKey(EN2CLLMProvider Provider);

    /**
     * The error a request to Provider fails with when it has no key: names the provider, the Project Settings field
     * and the environment variables. Empty for providers that need no key.
     */
    static FString MakeMissingKeyMessage(EN2CLLMProvider Provider);

    /** The process-wide source every service uses unless given another: FN2CSecretStore::Get() */
    static TSharedRef<const IN2CCredentialSource> Get();
};

/**
 * The default credential source: FN2CSecretStore, which gives the stored key, else the first set environment variable
 * (the precedence of card 0414: a stored key always wins).
 */
class NODETOCODE_API FN2CSecretStoreCredentialSource : public IN2CCredentialSource
{
public:
    /** Store must outlive this source (the process-wide store always does) */
    explicit FN2CSecretStoreCredentialSource(const FN2CSecretStore& InStore) : Store(InStore) {}

    virtual void Resolve(EN2CLLMProvider Provider, FOnResolved OnResolved) const override;
    virtual EN2CApiKeySource GetOrigin(EN2CLLMProvider Provider, FString* OutVariableName = nullptr) const override;

    /** The credential the store gives Provider now (the synchronous form of Resolve) */
    FN2CCredentialResult ResolveNow(EN2CLLMProvider Provider) const;

private:
    const FN2CSecretStore& Store;
};

/**
 * A source with keys set in code, per provider (tests, and integrations that hold keys elsewhere). Its keys can change
 * between requests (SetKey); a provider without a key resolves as the default source does without one.
 */
class NODETOCODE_API FN2CFixedCredentialSource : public IN2CCredentialSource
{
public:
    FN2CFixedCredentialSource() = default;

    /** A source with Key for Provider */
    FN2CFixedCredentialSource(EN2CLLMProvider Provider, const FString& Key) { SetKey(Provider, Key); }

    /** Sets (or, with an empty Key, clears) Provider's key; the next Resolve uses it */
    void SetKey(EN2CLLMProvider Provider, const FString& Key);

    /** How many times Resolve has run */
    int32 GetResolveCount() const { return ResolveCount; }

    virtual void Resolve(EN2CLLMProvider Provider, FOnResolved OnResolved) const override;
    virtual EN2CApiKeySource GetOrigin(EN2CLLMProvider Provider, FString* OutVariableName = nullptr) const override;

private:
    mutable FCriticalSection Mutex;
    TMap<EN2CLLMProvider, FString> Keys;
    mutable int32 ResolveCount = 0;
};
