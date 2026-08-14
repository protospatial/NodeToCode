// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettingsCustomization.h"

#include "Core/N2CConnectionTester.h"
#include "Core/N2CSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IDetailCustomization> FN2CSettingsCustomization::MakeInstance()
{
    return MakeShared<FN2CSettingsCustomization>();
}

void FN2CSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    ActiveDetailBuilder = &DetailBuilder;

    const TArray<TWeakObjectPtr<UN2CSettings>> SettingsObjects =
        DetailBuilder.GetObjectsOfTypeBeingCustomized<UN2CSettings>();
    if (SettingsObjects.IsEmpty())
    {
        return;
    }

    Settings = SettingsObjects[0];
    CustomProviderSettings = GetMutableDefault<UN2CCustomProviderSettings>();

    TSharedRef<IPropertyHandle> ProviderHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider));
    ProviderHandle->SetOnPropertyValueChanged(
        FSimpleDelegate::CreateSP(this, &FN2CSettingsCustomization::ForceRefresh));

    ApiTypeOptions.Reset();
    ApiTypeOptions.Add(MakeShared<FString>(TEXT("OpenAI")));
    RebuildActiveProviderOptions();

    if (Settings.IsValid() && Settings->Provider == EN2CLLMProvider::Custom)
    {
        // Attach custom-provider creation controls to the existing LLM Provider category.
        FDetailWidgetRow& ActiveProviderRow = DetailBuilder.AddCustomRowToCategory(
            ProviderHandle,
            FText::FromString(TEXT("Active Custom Provider")));

        if (!ActiveProviderOptions.IsEmpty())
        {
            ActiveProviderRow
            .NameContent()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Active Custom Provider")))
                .Font(IDetailLayoutBuilder::GetDetailFont())
            ]
            .ValueContent()
            .MinDesiredWidth(320.0f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&ActiveProviderOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock)
                        .Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty());
                })
                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (Item.IsValid() && CustomProviderSettings.IsValid())
                    {
                        CustomProviderSettings->SetActiveProvider(*Item);
                        ForceRefresh();
                    }
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return CustomProviderSettings.IsValid()
                            ? FText::FromString(CustomProviderSettings->ActiveProviderName)
                            : FText::GetEmpty();
                    })
                ]
            ];
        }
        else
        {
            ActiveProviderRow.Visibility(EVisibility::Collapsed);
        }

        FDetailWidgetRow& AddProviderRow = DetailBuilder.AddCustomRowToCategory(
            ProviderHandle,
            FText::FromString(TEXT("Add Custom Provider")));
        AddProviderRow
        .NameContent()
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Custom Provider")))
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
        .ValueContent()
        .MinDesiredWidth(500.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(0.0f, 0.0f, 6.0f, 0.0f)
            [
                SAssignNew(NewProviderNameTextBox, SEditableTextBox)
                .HintText(FText::FromString(TEXT("Provider Name")))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 6.0f, 0.0f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&ApiTypeOptions)
                .InitiallySelectedItem(ApiTypeOptions[0])
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock)
                        .Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty());
                })
                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (Item.IsValid() && *Item == TEXT("OpenAI"))
                    {
                        PendingApiType = EN2CCustomProviderApiType::OpenAI;
                    }
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("OpenAI")))
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("+")))
                .ToolTipText(FText::FromString(TEXT("Add custom provider")))
                .OnClicked_Lambda([this]()
                {
                    AddPendingProvider();
                    return FReply::Handled();
                })
            ]
        ];
    }

    // Add connection checks to each provider's existing reflected category instead of
    // creating new categories with literal pipe-delimited names.
    AddConnectionCheckRow(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModel),
        EN2CLLMProvider::Anthropic);
    AddConnectionCheckRow(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAI_Model),
        EN2CLLMProvider::OpenAI);
    AddConnectionCheckRow(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_Model),
        EN2CLLMProvider::Gemini);
    AddConnectionCheckRow(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekModel),
        EN2CLLMProvider::DeepSeek);
    AddConnectionCheckRow(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, OllamaConfig),
        EN2CLLMProvider::Ollama);
    AddConnectionCheckRow(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, LMStudioModel),
        EN2CLLMProvider::LMStudio);

    // Locate the generated parent LLM Services category and place dynamic custom
    // providers inside it as category-style groups.
    IDetailCategoryBuilder* LLMServicesCategory = nullptr;
    TArray<FName> ExistingCategoryNames;
    DetailBuilder.GetCategoryNames(ExistingCategoryNames);
    for (const FName& CategoryName : ExistingCategoryNames)
    {
        IDetailCategoryBuilder& CandidateCategory = DetailBuilder.EditCategory(CategoryName);
        if (CandidateCategory.GetDisplayName().ToString().Equals(TEXT("LLM Services"), ESearchCase::CaseSensitive))
        {
            LLMServicesCategory = &CandidateCategory;
            break;
        }
    }

    if (CustomProviderSettings.IsValid() && !CustomProviderSettings->Providers.IsEmpty())
    {
        if (!LLMServicesCategory)
        {
            // This is only a fallback for engine/layout variants where the generated
            // parent category is not exposed through GetCategoryNames().
            LLMServicesCategory = &DetailBuilder.EditCategory(
                TEXT("LLM Services"),
                FText::FromString(TEXT("LLM Services")));
        }

        for (int32 ProviderIndex = 0;
             ProviderIndex < CustomProviderSettings->Providers.Num();
             ++ProviderIndex)
        {
            AddProviderGroup(*LLMServicesCategory, ProviderIndex);
        }
    }
}

void FN2CSettingsCustomization::ForceRefresh()
{
    if (ActiveDetailBuilder)
    {
        ActiveDetailBuilder->ForceRefreshDetails();
    }
}

void FN2CSettingsCustomization::RebuildActiveProviderOptions()
{
    ActiveProviderOptions.Reset();
    if (!CustomProviderSettings.IsValid())
    {
        return;
    }

    for (const FN2CCustomProviderDefinition& Provider : CustomProviderSettings->Providers)
    {
        ActiveProviderOptions.Add(MakeShared<FString>(Provider.Name));
    }
}

void FN2CSettingsCustomization::AddPendingProvider()
{
    if (!CustomProviderSettings.IsValid() || !NewProviderNameTextBox.IsValid())
    {
        return;
    }

    const FString ProviderName = NewProviderNameTextBox->GetText().ToString().TrimStartAndEnd();
    if (CustomProviderSettings->AddProvider(ProviderName, PendingApiType))
    {
        NewProviderNameTextBox->SetText(FText::GetEmpty());
        ForceRefresh();
    }
}

void FN2CSettingsCustomization::AddConnectionCheckRow(
    IDetailLayoutBuilder& DetailBuilder,
    FName AnchorPropertyName,
    EN2CLLMProvider Provider)
{
    TSharedRef<IPropertyHandle> AnchorProperty = DetailBuilder.GetProperty(AnchorPropertyName);
    if (!AnchorProperty->IsValidHandle())
    {
        return;
    }

    FDetailWidgetRow& ConnectionRow = DetailBuilder.AddCustomRowToCategory(
        AnchorProperty,
        FText::FromString(TEXT("Connection")));
    ConnectionRow
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Connection")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    [
        SNew(SButton)
        .Text(FText::FromString(TEXT("Check Connection")))
        .ToolTipText(FText::FromString(TEXT("Send a lightweight request to verify provider reachability and authentication.")))
        .OnClicked_Lambda([this, Provider]()
        {
            if (Settings.IsValid())
            {
                FN2CConnectionTester::TestProvider(Provider, *Settings.Get());
            }
            return FReply::Handled();
        })
    ];
}

void FN2CSettingsCustomization::AddProviderGroup(
    IDetailCategoryBuilder& LLMServicesCategory,
    int32 ProviderIndex)
{
    if (!CustomProviderSettings.IsValid() ||
        !CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
    {
        return;
    }

    const FString ProviderName = CustomProviderSettings->Providers[ProviderIndex].Name;
    const FName GroupName(*FString::Printf(TEXT("N2CCustomProvider_%s"), *ProviderName));
    IDetailGroup& ProviderGroup = LLMServicesCategory.AddGroup(
        GroupName,
        FText::FromString(ProviderName),
        false,
        false);
    ProviderGroup.SetDisplayMode(EDetailGroupDisplayMode::Category);

    ProviderGroup.AddWidgetRow()
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("API Type")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    [
        SNew(STextBlock)
        .Text_Lambda([this, ProviderIndex]()
        {
            if (!CustomProviderSettings.IsValid() ||
                !CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
            {
                return FText::GetEmpty();
            }

            return StaticEnum<EN2CCustomProviderApiType>()->GetDisplayNameTextByValue(
                static_cast<int64>(CustomProviderSettings->Providers[ProviderIndex].ApiType));
        })
    ];

    ProviderGroup.AddWidgetRow()
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Provider Endpoint *")))
        .ToolTipText(FText::FromString(TEXT("Required OpenAI-compatible API base endpoint. NodeToCode appends /chat/completions internally.")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    .MinDesiredWidth(420.0f)
    [
        SNew(SEditableTextBox)
        .HintText(FText::FromString(TEXT("Required, e.g. https://host/api/v1")))
        .Text_Lambda([this, ProviderIndex]()
        {
            return CustomProviderSettings.IsValid() &&
                   CustomProviderSettings->Providers.IsValidIndex(ProviderIndex)
                ? FText::FromString(CustomProviderSettings->Providers[ProviderIndex].Endpoint)
                : FText::GetEmpty();
        })
        .OnTextCommitted_Lambda([this, ProviderIndex](const FText& Text, ETextCommit::Type)
        {
            if (CustomProviderSettings.IsValid() &&
                CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
            {
                CustomProviderSettings->Providers[ProviderIndex].Endpoint =
                    Text.ToString().TrimStartAndEnd();
                CustomProviderSettings->SaveDefinitions();
            }
        })
    ];

    ProviderGroup.AddWidgetRow()
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("API Key (Optional)")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    .MinDesiredWidth(420.0f)
    [
        SNew(SEditableTextBox)
        .IsPassword(true)
        .HintText(FText::FromString(TEXT("Optional")))
        .Text_Lambda([this, ProviderName]()
        {
            return CustomProviderSettings.IsValid()
                ? FText::FromString(CustomProviderSettings->GetApiKey(ProviderName))
                : FText::GetEmpty();
        })
        .OnTextCommitted_Lambda([this, ProviderName](const FText& Text, ETextCommit::Type)
        {
            if (CustomProviderSettings.IsValid())
            {
                CustomProviderSettings->SetApiKey(ProviderName, Text.ToString());
            }
        })
    ];

    ProviderGroup.AddWidgetRow()
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Model Name")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    .MinDesiredWidth(420.0f)
    [
        SNew(SEditableTextBox)
        .HintText(FText::FromString(TEXT("Provider model identifier")))
        .Text_Lambda([this, ProviderIndex]()
        {
            return CustomProviderSettings.IsValid() &&
                   CustomProviderSettings->Providers.IsValidIndex(ProviderIndex)
                ? FText::FromString(CustomProviderSettings->Providers[ProviderIndex].Model)
                : FText::GetEmpty();
        })
        .OnTextCommitted_Lambda([this, ProviderIndex](const FText& Text, ETextCommit::Type)
        {
            if (CustomProviderSettings.IsValid() &&
                CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
            {
                CustomProviderSettings->Providers[ProviderIndex].Model = Text.ToString().TrimStartAndEnd();
                CustomProviderSettings->SaveDefinitions();
            }
        })
    ];

    ProviderGroup.AddWidgetRow()
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Use System Prompts")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    [
        SNew(SCheckBox)
        .IsChecked_Lambda([this, ProviderIndex]()
        {
            return CustomProviderSettings.IsValid() &&
                   CustomProviderSettings->Providers.IsValidIndex(ProviderIndex) &&
                   CustomProviderSettings->Providers[ProviderIndex].bUseSystemPrompts
                ? ECheckBoxState::Checked
                : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([this, ProviderIndex](ECheckBoxState State)
        {
            if (CustomProviderSettings.IsValid() &&
                CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
            {
                CustomProviderSettings->Providers[ProviderIndex].bUseSystemPrompts =
                    State == ECheckBoxState::Checked;
                CustomProviderSettings->SaveDefinitions();
            }
        })
    ];

    ProviderGroup.AddWidgetRow()
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Connection")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    [
        SNew(SButton)
        .Text(FText::FromString(TEXT("Check Connection")))
        .ToolTipText(FText::FromString(TEXT("Send a lightweight request to verify provider reachability and authentication.")))
        .OnClicked_Lambda([this, ProviderName]()
        {
            if (CustomProviderSettings.IsValid())
            {
                FN2CConnectionTester::TestCustomProvider(ProviderName, *CustomProviderSettings.Get());
            }
            return FReply::Handled();
        })
    ];
}
