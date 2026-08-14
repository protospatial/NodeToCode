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

bool UN2CCustomProviderSettings::AddProvider(const FString& ProviderName, EN2CCustomProviderApiType ApiType)
{
    const FString TrimmedName = ProviderName.TrimStartAndEnd();
    if (TrimmedName.IsEmpty() || GetProvider(TrimmedName))
    {
        return false;
    }

    FN2CCustomProviderDefinition& Provider = Providers.AddDefaulted_GetRef();
    Provider.Name = TrimmedName;
    Provider.ApiType = ApiType;
    ActiveProviderName = TrimmedName;
    SaveDefinitions();
    return true;
}

bool UN2CCustomProviderSettings::SetActiveProvider(const FString& ProviderName)
{
    const FN2CCustomProviderDefinition* Provider = GetProvider(ProviderName);
    if (!Provider)
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

FString UN2CCustomProviderSettings::GetApiKey(const FString& ProviderName) const
{
    LoadApiKeys();
    return ApiKeys.FindRef(ProviderName);
}

void UN2CCustomProviderSettings::SetApiKey(const FString& ProviderName, const FString& ApiKey)
{
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
