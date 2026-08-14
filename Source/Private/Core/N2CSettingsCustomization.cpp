// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettingsCustomization.h"

#include "Core/N2CConnectionTester.h"
#include "Core/N2CSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
TSharedRef<SWidget> MakeLabeledRow(
    const FText& Label,
    const TSharedRef<SWidget>& ValueWidget,
    const FText& ToolTip = FText::GetEmpty())
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(0.35f)
        .VAlign(VAlign_Center)
        .Padding(0.0f, 2.0f, 8.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(Label)
            .ToolTipText(ToolTip)
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
        + SHorizontalBox::Slot()
        .FillWidth(0.65f)
        .VAlign(VAlign_Center)
        .Padding(0.0f, 2.0f)
        [
            ValueWidget
        ];
}
}

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

    ApiTypeOptions.Reset();
    ApiTypeOptions.Add(MakeShared<FString>(TEXT("OpenAI")));
    RebuildActiveProviderOptions();

    TSharedRef<IPropertyHandle> ProviderHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider));
    ProviderHandle->SetOnPropertyValueChanged(
        FSimpleDelegate::CreateSP(this, &FN2CSettingsCustomization::ForceRefresh));
    CustomizeProviderProperty(DetailBuilder, ProviderHandle);

    // Edit only existing default property rows. EditDefaultProperty explicitly keeps
    // each row in its native reflected category, preserving Unreal's pipe-delimited
    // category hierarchy.
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModel),
        EN2CLLMProvider::Anthropic);
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAI_Model),
        EN2CLLMProvider::OpenAI);
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_Model),
        EN2CLLMProvider::Gemini);
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekModel),
        EN2CLLMProvider::DeepSeek);
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, OllamaModel),
        EN2CLLMProvider::Ollama);
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, LMStudioModel),
        EN2CLLMProvider::LMStudio);

    CustomizeCustomProvidersProperty(DetailBuilder);
}

void FN2CSettingsCustomization::CustomizeProviderProperty(
    IDetailLayoutBuilder& DetailBuilder,
    const TSharedRef<IPropertyHandle>& ProviderHandle)
{
    IDetailPropertyRow* PropertyRow = DetailBuilder.EditDefaultProperty(ProviderHandle);
    if (!PropertyRow)
    {
        return;
    }

    TSharedPtr<SWidget> DefaultNameWidget;
    TSharedPtr<SWidget> DefaultValueWidget;
    PropertyRow->GetDefaultWidgets(DefaultNameWidget, DefaultValueWidget, true);
    if (!DefaultNameWidget.IsValid() || !DefaultValueWidget.IsValid())
    {
        return;
    }

    FDetailWidgetRow& Row = PropertyRow->CustomWidget(true);
    Row.NameContent()
    [
        DefaultNameWidget.ToSharedRef()
    ]
    .ValueContent()
    .MinDesiredWidth(560.0f)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            DefaultValueWidget.ToSharedRef()
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 5.0f, 0.0f, 0.0f)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]()
            {
                return Settings.IsValid() && Settings->Provider == EN2CLLMProvider::Custom
                    ? EVisibility::Visible
                    : EVisibility::Collapsed;
            })
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Active Custom Provider")))
                    .Font(IDetailLayoutBuilder::GetDetailFont())
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
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
                            if (!CustomProviderSettings.IsValid() ||
                                CustomProviderSettings->ActiveProviderName.IsEmpty())
                            {
                                return FText::FromString(TEXT("None"));
                            }
                            return FText::FromString(CustomProviderSettings->ActiveProviderName);
                        })
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
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
            ]
        ]
    ];
}

void FN2CSettingsCustomization::AddConnectionButtonToProperty(
    IDetailLayoutBuilder& DetailBuilder,
    FName PropertyName,
    EN2CLLMProvider Provider)
{
    TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(PropertyName);
    if (!PropertyHandle->IsValidHandle())
    {
        return;
    }

    IDetailPropertyRow* PropertyRow = DetailBuilder.EditDefaultProperty(PropertyHandle);
    if (!PropertyRow)
    {
        return;
    }

    TSharedPtr<SWidget> DefaultNameWidget;
    TSharedPtr<SWidget> DefaultValueWidget;
    PropertyRow->GetDefaultWidgets(DefaultNameWidget, DefaultValueWidget, true);
    if (!DefaultNameWidget.IsValid() || !DefaultValueWidget.IsValid())
    {
        return;
    }

    FDetailWidgetRow& Row = PropertyRow->CustomWidget(true);
    Row.NameContent()
    [
        DefaultNameWidget.ToSharedRef()
    ]
    .ValueContent()
    .MinDesiredWidth(500.0f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        [
            DefaultValueWidget.ToSharedRef()
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(6.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Check Connection")))
            .ToolTipText(FText::FromString(TEXT("Verify provider reachability and authentication without generating text.")))
            .OnClicked_Lambda([this, Provider]()
            {
                if (Settings.IsValid())
                {
                    FN2CConnectionTester::TestProvider(Provider, *Settings.Get());
                }
                return FReply::Handled();
            })
        ]
    ];
}

void FN2CSettingsCustomization::CustomizeCustomProvidersProperty(IDetailLayoutBuilder& DetailBuilder)
{
    TSharedRef<IPropertyHandle> AnchorHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UN2CSettings, bCustomProvidersUIAnchor));
    if (!AnchorHandle->IsValidHandle())
    {
        return;
    }

    IDetailPropertyRow* PropertyRow = DetailBuilder.EditDefaultProperty(AnchorHandle);
    if (!PropertyRow)
    {
        return;
    }

    PropertyRow->Visibility(TAttribute<EVisibility>::CreateLambda([this]()
    {
        return CustomProviderSettings.IsValid() && !CustomProviderSettings->Providers.IsEmpty()
            ? EVisibility::Visible
            : EVisibility::Collapsed;
    }));

    TSharedRef<SVerticalBox> ProvidersBox = SNew(SVerticalBox);
    if (CustomProviderSettings.IsValid())
    {
        for (int32 ProviderIndex = 0;
             ProviderIndex < CustomProviderSettings->Providers.Num();
             ++ProviderIndex)
        {
            ProvidersBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 2.0f)
            [
                BuildCustomProviderArea(ProviderIndex)
            ];
        }
    }

    FDetailWidgetRow& Row = PropertyRow->CustomWidget(false);
    Row.WholeRowContent()
    .MinDesiredWidth(600.0f)
    [
        ProvidersBox
    ];
}

TSharedRef<SWidget> FN2CSettingsCustomization::BuildCustomProviderArea(int32 ProviderIndex)
{
    if (!CustomProviderSettings.IsValid() ||
        !CustomProviderSettings->Providers.IsValidIndex(ProviderIndex))
    {
        return SNew(STextBlock).Text(FText::GetEmpty());
    }

    const FString ProviderName = CustomProviderSettings->Providers[ProviderIndex].Name;

    return SNew(SExpandableArea)
        .InitiallyCollapsed(true)
        .AllowAnimatedTransition(false)
        .AreaTitle(FText::FromString(ProviderName))
        .Padding(FMargin(8.0f, 4.0f))
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("API Type")),
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
                    }))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Provider Endpoint *")),
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
                    }),
                    FText::FromString(TEXT("Required OpenAI-compatible API base endpoint. NodeToCode appends /chat/completions internally.")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("API Key (Optional)")),
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
                    }))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Model Name")),
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
                            CustomProviderSettings->Providers[ProviderIndex].Model =
                                Text.ToString().TrimStartAndEnd();
                            CustomProviderSettings->SaveDefinitions();
                        }
                    }))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Use System Prompts")),
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
                    }))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Connection")),
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Check Connection")))
                        .ToolTipText(FText::FromString(TEXT("Verify provider reachability and authentication without generating text.")))
                        .OnClicked_Lambda([this, ProviderName]()
                        {
                            if (CustomProviderSettings.IsValid())
                            {
                                FN2CConnectionTester::TestCustomProvider(
                                    ProviderName,
                                    *CustomProviderSettings.Get());
                            }
                            return FReply::Handled();
                        })
                    ])
            ]
        ];
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
