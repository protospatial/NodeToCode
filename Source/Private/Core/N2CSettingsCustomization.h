// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CCustomProviderSettings.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class SEditableTextBox;
class UN2CSettings;

class FN2CSettingsCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    void ForceRefresh();
    void RebuildActiveProviderOptions();
    void AddPendingProvider();
    void AddProviderCategory(IDetailLayoutBuilder& DetailBuilder, int32 ProviderIndex);

    IDetailLayoutBuilder* ActiveDetailBuilder = nullptr;
    TWeakObjectPtr<UN2CSettings> Settings;
    TWeakObjectPtr<UN2CCustomProviderSettings> CustomProviderSettings;

    TSharedPtr<SEditableTextBox> NewProviderNameTextBox;
    TArray<TSharedPtr<FString>> ApiTypeOptions;
    TArray<TSharedPtr<FString>> ActiveProviderOptions;
    EN2CCustomProviderApiType PendingApiType = EN2CCustomProviderApiType::OpenAI;
};
