// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettingsCustomization.h"

#include "Core/N2CSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
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
        IDetailCategoryBuilder& ProviderCategory =
            DetailBuilder.EditCategory(TEXT("Node to Code | LLM Provider"));

        if (!ActiveProviderOptions.IsEmpty())
        {
            ProviderCategory.AddCustomRow(FText::FromString(TEXT("Active Custom Provider")))
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

        ProviderCategory.AddCustomRow(FText::FromString(TEXT("Add Custom Provider")))
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

    if (CustomProviderSettings.IsValid())
    {
        for (int32 ProviderIndex = 0;
             ProviderIndex < CustomProviderSettings->Providers.Num();
             ++ProviderIndex)
        {
            AddProviderCategory(DetailBuilder, ProviderIndex);
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

void FN2CSettingsCustomization::AddProviderCategory(
    IDetailLayoutBuilder& DetailBuilder,
    int32 ProviderIndex)
{
    if (!CustomProviderSettings.IsValid() ||
        !CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
    {
        return;
    }

    const FString ProviderName = CustomProviderSettings->Providers[ProviderIndex].Name;
    const FString CategoryName = FString::Printf(
        TEXT("Node to Code | LLM Services | %s"), *ProviderName);
    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(FName(*CategoryName));

    Category.AddCustomRow(FText::FromString(TEXT("API Type")))
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

    Category.AddCustomRow(FText::FromString(TEXT("Provider Endpoint")))
    .NameContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("Provider Endpoint *")))
        .ToolTipText(FText::FromString(TEXT("Required OpenAI-compatible chat completions endpoint")))
        .Font(IDetailLayoutBuilder::GetDetailFont())
    ]
    .ValueContent()
    .MinDesiredWidth(420.0f)
    [
        SNew(SEditableTextBox)
        .HintText(FText::FromString(TEXT("Required, e.g. https://host/v1/chat/completions")))
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

    Category.AddCustomRow(FText::FromString(TEXT("API Key")))
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

    Category.AddCustomRow(FText::FromString(TEXT("Model Name")))
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

    Category.AddCustomRow(FText::FromString(TEXT("Use System Prompts")))
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
}
