// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CCustomProviderSettings.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const FN2CCustomProviderDefinition* UN2CCustomProviderSettings::GetActiveProvider() const
{
    return GetProvider(ActiveProviderName);
}

FN2CCustomProviderDefinition* UN2CCustomProviderSettings::GetProvider(const FString& ProviderName)
{
    return Providers.FindByPredicate([&ProviderName](const FN2CCustomProviderDefinition& Provider)
    {
        return Provider.Name.Equals(ProviderName, ESearchCase::IgnoreCase);
    });
}

const FN2CCustomProviderDefinition* UN2CCustomProviderSettings::GetProvider(const FString& ProviderName) const
{
    return Providers.FindByPredicate([&ProviderName](const FN2CCustomProviderDefinition& Provider)
    {
        return Provider.Name.Equals(ProviderName, ESearchCase::IgnoreCase);
    });
}

FString UN2CCustomProviderSettings::MakeUniqueProviderName(const FString& BaseName) const
{
    FString Candidate = BaseName.TrimStartAndEnd();
    if (Candidate.IsEmpty())
    {
        Candidate = TEXT("Provider Profile");
    }

    if (!GetProvider(Candidate))
    {
        return Candidate;
    }

    int32 Suffix = 2;
    FString UniqueName;
    do
    {
        UniqueName = FString::Printf(TEXT("%s %d"), *Candidate, Suffix++);
    }
    while (GetProvider(UniqueName));

    return UniqueName;
}

const FN2CCustomProviderDefinition* UN2CCustomProviderSettings::FindFirstCustomEndpointProvider() const
{
    return Providers.FindByPredicate([](const FN2CCustomProviderDefinition& Provider)
    {
        return Provider.ProfileSource == EN2CCustomProviderProfileSource::CustomEndpoint;
    });
}

bool UN2CCustomProviderSettings::AddProvider(const FString& ProviderName, EN2CCustomProviderApiType ApiType)
{
    const FString TrimmedName = ProviderName.TrimStartAndEnd();
    if (TrimmedName.IsEmpty() || GetProvider(TrimmedName))
    {
        return false;
    }

    FN2CCustomProviderDefinition& Provider = Providers.AddDefaulted_GetRef();
    Provider.Name = TrimmedName;
    Provider.ProfileSource = EN2CCustomProviderProfileSource::CustomEndpoint;
    Provider.ApiType = ApiType;
    ActiveProviderName = TrimmedName;
    SaveDefinitions();
    return true;
}

bool UN2CCustomProviderSettings::AddBuiltInProviderProfile(
    EN2CLLMProvider BuiltInProvider,
    const FString& Model,
    FString& OutProfileName)
{
    OutProfileName.Empty();

    if (BuiltInProvider == EN2CLLMProvider::Custom)
    {
        return false;
    }

    const FString TrimmedModel = Model.TrimStartAndEnd();
    if (TrimmedModel.IsEmpty())
    {
        return false;
    }

    FString ProviderDisplayName = UEnum::GetValueAsString(BuiltInProvider);
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        ProviderDisplayName = ProviderEnum->GetDisplayNameTextByValue(
            static_cast<int64>(BuiltInProvider)).ToString();
    }

    const FString BaseProfileName = FString::Printf(
        TEXT("%s %s"),
        *ProviderDisplayName,
        *TrimmedModel);

    OutProfileName = BaseProfileName;
    if (GetProvider(OutProfileName))
    {
        int32 Suffix = 2;
        do
        {
            OutProfileName = FString::Printf(
                TEXT("%s (%d)"),
                *BaseProfileName,
                Suffix++);
        }
        while (GetProvider(OutProfileName));
    }

    FN2CCustomProviderDefinition& Profile = Providers.AddDefaulted_GetRef();
    Profile.Name = OutProfileName;
    Profile.ProfileSource = EN2CCustomProviderProfileSource::BuiltInProvider;
    Profile.BuiltInProvider = BuiltInProvider;
    Profile.Model = TrimmedModel;

    // Do not change ActiveProviderName: it remains the active custom-endpoint service. Built-in
    // reference profiles are resolved to their native service when selected in the request picker.
    SaveDefinitions();
    return true;
}

bool UN2CCustomProviderSettings::DuplicateProvider(
    const FString& ProviderName,
    FString& OutDuplicateName)
{
    OutDuplicateName.Empty();

    const FN2CCustomProviderDefinition* SourceProvider = GetProvider(ProviderName);
    if (!SourceProvider)
    {
        return false;
    }

    // Copy before adding because Providers may reallocate and invalidate SourceProvider.
    const FN2CCustomProviderDefinition SourceCopy = *SourceProvider;
    OutDuplicateName = MakeUniqueProviderName(SourceCopy.Name + TEXT(" Copy"));

    FN2CCustomProviderDefinition Duplicate = SourceCopy;
    Duplicate.Name = OutDuplicateName;
    Providers.Add(MoveTemp(Duplicate));

    // Built-in reference profiles intentionally own no secret. For custom endpoints, duplicate the
    // current secret value so the copy is immediately usable but can subsequently diverge.
    if (SourceCopy.ProfileSource == EN2CCustomProviderProfileSource::CustomEndpoint)
    {
        LoadApiKeys();
        const FString ExistingApiKey = ApiKeys.FindRef(SourceCopy.Name);
        if (!ExistingApiKey.IsEmpty())
        {
            ApiKeys.Add(OutDuplicateName, ExistingApiKey);
            SaveApiKeys();
        }
    }

    SaveDefinitions();
    return true;
}

bool UN2CCustomProviderSettings::RenameProvider(
    const FString& ProviderName,
    const FString& NewProviderName)
{
    const FString TrimmedName = NewProviderName.TrimStartAndEnd();
    FN2CCustomProviderDefinition* Provider = GetProvider(ProviderName);
    if (!Provider || TrimmedName.IsEmpty())
    {
        return false;
    }

    const FN2CCustomProviderDefinition* ConflictingProvider = GetProvider(TrimmedName);
    if (ConflictingProvider && ConflictingProvider != Provider)
    {
        return false;
    }

    const FString PreviousName = Provider->Name;
    if (PreviousName.Equals(TrimmedName, ESearchCase::CaseSensitive))
    {
        return true;
    }

    LoadApiKeys();

    FString ApiKey;
    const bool bHadApiKey = ApiKeys.RemoveAndCopyValue(PreviousName, ApiKey);

    Provider->Name = TrimmedName;
    if (ActiveProviderName.Equals(PreviousName, ESearchCase::IgnoreCase))
    {
        ActiveProviderName = TrimmedName;
    }

    if (bHadApiKey && !ApiKey.IsEmpty())
    {
        ApiKeys.Add(TrimmedName, MoveTemp(ApiKey));
    }

    SaveDefinitions();
    SaveApiKeys();
    return true;
}

bool UN2CCustomProviderSettings::RemoveProvider(const FString& ProviderName)
{
    const int32 ProviderIndex = Providers.IndexOfByPredicate(
        [&ProviderName](const FN2CCustomProviderDefinition& Provider)
        {
            return Provider.Name.Equals(ProviderName, ESearchCase::IgnoreCase);
        });

    if (ProviderIndex == INDEX_NONE)
    {
        return false;
    }

    const FString RemovedName = Providers[ProviderIndex].Name;
    const bool bRemovedActiveProvider =
        ActiveProviderName.Equals(RemovedName, ESearchCase::IgnoreCase);

    Providers.RemoveAt(ProviderIndex);

    LoadApiKeys();
    ApiKeys.Remove(RemovedName);

    const FN2CCustomProviderDefinition* ActiveProvider = GetProvider(ActiveProviderName);
    if (bRemovedActiveProvider ||
        (!ActiveProviderName.IsEmpty() &&
         (!ActiveProvider ||
          ActiveProvider->ProfileSource != EN2CCustomProviderProfileSource::CustomEndpoint)))
    {
        const FN2CCustomProviderDefinition* Fallback = FindFirstCustomEndpointProvider();
        ActiveProviderName = Fallback ? Fallback->Name : FString();
    }

    SaveDefinitions();
    SaveApiKeys();
    return true;
}

bool UN2CCustomProviderSettings::SetActiveProvider(const FString& ProviderName)
{
    const FN2CCustomProviderDefinition* Provider = GetProvider(ProviderName);
    if (!Provider || Provider->ProfileSource != EN2CCustomProviderProfileSource::CustomEndpoint)
    {
        return false;
    }

    ActiveProviderName = Provider->Name;
    SaveDefinitions();
    return true;
}

void UN2CCustomProviderSettings::SaveDefinitions()
{
    TryUpdateDefaultConfigFile(FString(), true);
}

FString UN2CCustomProviderSettings::GetBuiltInModelOverride(EN2CLLMProvider Provider) const
{
    if (Provider == EN2CLLMProvider::Custom)
    {
        return FString();
    }

    const FString* Override = BuiltInModelOverrides.Find(Provider);
    return Override ? Override->TrimStartAndEnd() : FString();
}

void UN2CCustomProviderSettings::SetBuiltInModelOverride(
    EN2CLLMProvider Provider,
    const FString& Model)
{
    if (Provider == EN2CLLMProvider::Custom)
    {
        return;
    }

    const FString TrimmedModel = Model.TrimStartAndEnd();
    if (TrimmedModel.IsEmpty())
    {
        BuiltInModelOverrides.Remove(Provider);
    }
    else
    {
        BuiltInModelOverrides.Add(Provider, TrimmedModel);
    }

    SaveDefinitions();
}

FString UN2CCustomProviderSettings::GetApiKey(const FString& ProviderName) const
{
    const FN2CCustomProviderDefinition* Provider = GetProvider(ProviderName);
    if (Provider && Provider->ProfileSource == EN2CCustomProviderProfileSource::BuiltInProvider)
    {
        return FString();
    }

    LoadApiKeys();
    return ApiKeys.FindRef(ProviderName);
}

void UN2CCustomProviderSettings::SetApiKey(const FString& ProviderName, const FString& ApiKey)
{
    const FN2CCustomProviderDefinition* Provider = GetProvider(ProviderName);
    if (Provider && Provider->ProfileSource == EN2CCustomProviderProfileSource::BuiltInProvider)
    {
        // A linked built-in profile must never fork or duplicate the referenced provider secret.
        LoadApiKeys();
        if (ApiKeys.Remove(ProviderName) > 0)
        {
            SaveApiKeys();
        }
        return;
    }

    LoadApiKeys();

    if (ApiKey.IsEmpty())
    {
        ApiKeys.Remove(ProviderName);
    }
    else
    {
        ApiKeys.Add(ProviderName, ApiKey);
    }

    SaveApiKeys();
}

FString UN2CCustomProviderSettings::GetSecretsFilePath()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("User"), TEXT("custom_provider_secrets.json")));
}

void UN2CCustomProviderSettings::EnsureSecretsDirectoryExists()
{
    const FString SecretsDir = FPaths::GetPath(GetSecretsFilePath());
    if (!IFileManager::Get().DirectoryExists(*SecretsDir))
    {
        IFileManager::Get().MakeDirectory(*SecretsDir, true);
    }
}

void UN2CCustomProviderSettings::LoadApiKeys() const
{
    if (bApiKeysLoaded)
    {
        return;
    }

    bApiKeysLoaded = true;
    ApiKeys.Empty();

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *GetSecretsFilePath()))
    {
        return;
    }

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* ProvidersArray = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("providers"), ProvidersArray))
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : *ProvidersArray)
    {
        const TSharedPtr<FJsonObject> Entry = Value->AsObject();
        if (!Entry.IsValid())
        {
            continue;
        }

        FString Name;
        FString ApiKey;
        if (Entry->TryGetStringField(TEXT("name"), Name) &&
            Entry->TryGetStringField(TEXT("api_key"), ApiKey) &&
            !Name.IsEmpty())
        {
            ApiKeys.Add(Name, ApiKey);
        }
    }
}

void UN2CCustomProviderSettings::SaveApiKeys() const
{
    EnsureSecretsDirectoryExists();

    TArray<TSharedPtr<FJsonValue>> ProvidersArray;
    for (const TPair<FString, FString>& Pair : ApiKeys)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Pair.Key);
        Entry->SetStringField(TEXT("api_key"), Pair.Value);
        ProvidersArray.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetArrayField(TEXT("providers"), ProvidersArray);

    FString JsonString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
    {
        FFileHelper::SaveStringToFile(JsonString, *GetSecretsFilePath());
    }
}
