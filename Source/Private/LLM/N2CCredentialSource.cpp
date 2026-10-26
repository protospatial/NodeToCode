// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CCredentialSource.h"

#include "Misc/ScopeLock.h"

FN2CCredential FN2CCredential::Make(EN2CLLMProvider Provider, const FString& Key, EN2CApiKeySource InOrigin)
{
    FN2CCredential Credential;
    Credential.Value = Key.TrimStartAndEnd();
    Credential.AuthStyle = GetAuthStyle(Provider);
    Credential.Origin = Credential.Value.IsEmpty() ? EN2CApiKeySource::None : InOrigin;
    return Credential;
}

EN2CAuthStyle FN2CCredential::GetAuthStyle(EN2CLLMProvider Provider)
{
    switch (Provider)
    {
    case EN2CLLMProvider::Anthropic: return EN2CAuthStyle::XApiKey;
    case EN2CLLMProvider::Gemini: return EN2CAuthStyle::XGoogApiKey;
    case EN2CLLMProvider::OpenAI:
    case EN2CLLMProvider::DeepSeek:
    case EN2CLLMProvider::LMStudio:
    case EN2CLLMProvider::Ollama:
        return EN2CAuthStyle::Bearer;
    default:
        return EN2CAuthStyle::None;
    }
}

bool FN2CCredential::GetHeader(FString& OutName, FString& OutValue) const
{
    OutName.Reset();
    OutValue.Reset();
    if (Value.IsEmpty())
    {
        return false;
    }
    switch (AuthStyle)
    {
    case EN2CAuthStyle::XApiKey:
        OutName = TEXT("x-api-key");
        OutValue = Value;
        return true;
    case EN2CAuthStyle::XGoogApiKey:
        OutName = TEXT("x-goog-api-key");
        OutValue = Value;
        return true;
    case EN2CAuthStyle::Bearer:
        OutName = TEXT("Authorization");
        OutValue = FString::Printf(TEXT("Bearer %s"), *Value);
        return true;
    default:
        return false;
    }
}

bool FN2CCredential::IsCredentialHeader(const FString& HeaderName)
{
    return HeaderName.Equals(TEXT("Authorization"), ESearchCase::IgnoreCase)
        || HeaderName.Equals(TEXT("x-api-key"), ESearchCase::IgnoreCase)
        || HeaderName.Equals(TEXT("x-goog-api-key"), ESearchCase::IgnoreCase);
}

bool IN2CCredentialSource::Has(EN2CLLMProvider Provider) const
{
    return !RequiresKey(Provider) || GetOrigin(Provider) != EN2CApiKeySource::None;
}

bool IN2CCredentialSource::RequiresKey(EN2CLLMProvider Provider)
{
    // Ollama and LM Studio run locally and take a key only when their server is set up for one
    return !FN2CSecretStore::GetProviderId(Provider).IsEmpty();
}

FString IN2CCredentialSource::MakeMissingKeyMessage(EN2CLLMProvider Provider)
{
    const TCHAR* Name = nullptr;
    switch (Provider)
    {
    case EN2CLLMProvider::OpenAI: Name = TEXT("OpenAI"); break;
    case EN2CLLMProvider::Anthropic: Name = TEXT("Anthropic"); break;
    case EN2CLLMProvider::Gemini: Name = TEXT("Gemini"); break;
    case EN2CLLMProvider::DeepSeek: Name = TEXT("DeepSeek"); break;
    default: return FString(); // Ollama and LM Studio run locally without a key
    }

    FString Message = FString::Printf(
        TEXT("No %s API key is set. Add one in Project Settings > Plugins > Node to Code > LLM Services > %s > API Key, or set %s in the editor's environment."),
        Name, Name, *FString::Join(FN2CSecretStore::GetEnvironmentVariableNames(Provider), TEXT(" or ")));
    if (Provider == EN2CLLMProvider::Anthropic || Provider == EN2CLLMProvider::Gemini)
    {
        Message += FString::Printf(TEXT(" Claude Pro/Max and Google account sign-in were removed in NodeToCode 2.0; see %s"),
            N2CSignInRemoval::DocsUrl);
    }
    return Message;
}

TSharedRef<const IN2CCredentialSource> IN2CCredentialSource::Get()
{
    static const TSharedRef<const IN2CCredentialSource> Default =
        MakeShared<FN2CSecretStoreCredentialSource>(FN2CSecretStore::Get());
    return Default;
}

FN2CCredentialResult FN2CSecretStoreCredentialSource::ResolveNow(EN2CLLMProvider Provider) const
{
    FString VariableName;
    const EN2CApiKeySource Origin = Store.GetApiKeySource(Provider, &VariableName);
    // GetApiKey applies the same precedence (stored, then environment) and logs the variable's name once
    FN2CCredential Credential = FN2CCredential::Make(Provider,
        Origin == EN2CApiKeySource::None ? FString() : Store.GetApiKey(Provider), Origin);
    if (Credential.Origin == EN2CApiKeySource::Environment)
    {
        Credential.VariableName = VariableName;
    }
    if (Credential.Value.IsEmpty() && Provider == EN2CLLMProvider::LMStudio)
    {
        // LM Studio's OpenAI-compatible endpoint takes any Bearer value when its server has no key set (as 1.x sent)
        Credential.Value = TEXT("lm-studio");
    }
    if (Credential.Value.IsEmpty() && RequiresKey(Provider))
    {
        return MakeError(FText::FromString(MakeMissingKeyMessage(Provider)));
    }
    return MakeValue(MoveTemp(Credential));
}

void FN2CSecretStoreCredentialSource::Resolve(EN2CLLMProvider Provider, FOnResolved OnResolved) const
{
    OnResolved(ResolveNow(Provider));
}

EN2CApiKeySource FN2CSecretStoreCredentialSource::GetOrigin(EN2CLLMProvider Provider, FString* OutVariableName) const
{
    return Store.GetApiKeySource(Provider, OutVariableName);
}

void FN2CFixedCredentialSource::SetKey(EN2CLLMProvider Provider, const FString& Key)
{
    FScopeLock Lock(&Mutex);
    if (Key.TrimStartAndEnd().IsEmpty())
    {
        Keys.Remove(Provider);
    }
    else
    {
        Keys.Add(Provider, Key);
    }
}

void FN2CFixedCredentialSource::Resolve(EN2CLLMProvider Provider, FOnResolved OnResolved) const
{
    FString Key;
    {
        FScopeLock Lock(&Mutex);
        ++ResolveCount;
        if (const FString* Found = Keys.Find(Provider))
        {
            Key = *Found;
        }
    }
    FN2CCredential Credential = FN2CCredential::Make(Provider, Key);
    if (Credential.Value.IsEmpty() && RequiresKey(Provider))
    {
        OnResolved(MakeError(FText::FromString(MakeMissingKeyMessage(Provider))));
        return;
    }
    OnResolved(MakeValue(MoveTemp(Credential)));
}

EN2CApiKeySource FN2CFixedCredentialSource::GetOrigin(EN2CLLMProvider Provider, FString* OutVariableName) const
{
    if (OutVariableName)
    {
        OutVariableName->Reset();
    }
    FScopeLock Lock(&Mutex);
    const FString* Found = Keys.Find(Provider);
    return Found && !Found->TrimStartAndEnd().IsEmpty() ? EN2CApiKeySource::Store : EN2CApiKeySource::None;
}
