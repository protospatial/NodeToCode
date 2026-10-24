// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CCustomProviderSettings.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IPropertyHandle;
class SEditableTextBox;
class SWidget;
class UN2CSettings;
enum class EN2CLLMProvider : uint8;

class FN2CSettingsCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    void ForceRefresh();
    void RebuildActiveProviderOptions();
    void AddPendingProvider();
    void DiscoverModelsForProvider(EN2CLLMProvider Provider);
    void DiscoverModelsForProfile(const FString& ProviderName);
    void SaveBuiltInProviderProfile(EN2CLLMProvider Provider);

    void CustomizeProviderProperty(
        IDetailLayoutBuilder& DetailBuilder,
        const TSharedRef<IPropertyHandle>& ProviderHandle);
    void AddConnectionButtonToProperty(
        IDetailLayoutBuilder& DetailBuilder,
        FName PropertyName,
        EN2CLLMProvider Provider);
    void CustomizeCustomProvidersProperty(IDetailLayoutBuilder& DetailBuilder);
    TSharedRef<SWidget> BuildCustomProviderArea(int32 ProviderIndex);

    IDetailLayoutBuilder* ActiveDetailBuilder = nullptr;
    TWeakObjectPtr<UN2CSettings> Settings;
    TWeakObjectPtr<UN2CCustomProviderSettings> CustomProviderSettings;

    TSharedPtr<SEditableTextBox> NewProviderNameTextBox;
    TArray<TSharedPtr<FString>> ApiTypeOptions;
    TArray<TSharedPtr<FString>> ActiveProviderOptions;
    EN2CCustomProviderApiType PendingApiType = EN2CCustomProviderApiType::OpenAI;
};
