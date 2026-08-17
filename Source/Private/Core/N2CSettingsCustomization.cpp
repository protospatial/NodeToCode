// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettingsCustomization.h"

#include "Core/N2CConnectionTester.h"
#include "Core/N2CModelDiscovery.h"
#include "Core/N2CSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailPropertyRow.h"
#include "LLM/N2CLLMModels.h"
#include "Misc/MessageDialog.h"
#include "PropertyHandle.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

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

FString GetSettingsProviderDisplayName(EN2CLLMProvider Provider)
{
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        return ProviderEnum->GetDisplayNameTextByValue(static_cast<int64>(Provider)).ToString();
    }
    return UEnum::GetValueAsString(Provider);
}

FString GetSettingsBuiltInFallbackModel(
    const UN2CSettings& Settings,
    EN2CLLMProvider Provider)
{
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return FN2CLLMModelUtils::GetOpenAIModelValue(Settings.OpenAI_Model);
        case EN2CLLMProvider::Anthropic:
            return FN2CLLMModelUtils::GetAnthropicModelValue(Settings.AnthropicModel);
        case EN2CLLMProvider::Gemini:
            return FN2CLLMModelUtils::GetGeminiModelValue(Settings.Gemini_Model);
        case EN2CLLMProvider::DeepSeek:
            return FN2CLLMModelUtils::GetDeepSeekModelValue(Settings.DeepSeekModel);
        case EN2CLLMProvider::Ollama:
            return Settings.OllamaModel;
        case EN2CLLMProvider::LMStudio:
            return Settings.LMStudioModel;
        case EN2CLLMProvider::MiniMax:
            return Settings.MiniMaxModel;
        default:
            return FString();
    }
}

FString GetSettingsConfiguredBuiltInModel(
    const UN2CSettings& Settings,
    const UN2CCustomProviderSettings* CustomSettings,
    EN2CLLMProvider Provider)
{
    if (CustomSettings)
    {
        const FString Override = CustomSettings->GetBuiltInModelOverride(Provider);
        if (!Override.IsEmpty())
        {
            return Override;
        }
    }
    return GetSettingsBuiltInFallbackModel(Settings, Provider);
}

FString GetSettingsProviderApiKey(
    const UN2CSettings& Settings,
    EN2CLLMProvider Provider)
{
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return Settings.OpenAI_API_Key_UI;
        case EN2CLLMProvider::Anthropic:
            return Settings.Anthropic_API_Key_UI;
        case EN2CLLMProvider::Gemini:
            return Settings.Gemini_API_Key_UI;
        case EN2CLLMProvider::DeepSeek:
            return Settings.DeepSeek_API_Key_UI;
        case EN2CLLMProvider::Ollama:
            return Settings.OllamaConfig.ApiKey;
        case EN2CLLMProvider::LMStudio:
            return TEXT("lm-studio");
        case EN2CLLMProvider::MiniMax:
            return Settings.MiniMax_API_Key_UI;
        default:
            return FString();
    }
}

struct FSettingsModelDiscoveryDialogState
{
    TArray<TSharedPtr<FString>> Models;
    TSharedPtr<FString> SelectedModel;
    TWeakPtr<SListView<TSharedPtr<FString>>> ModelList;
    FString StatusText = TEXT("Discovering models...");
};
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
    AddConnectionButtonToProperty(
        DetailBuilder,
        GET_MEMBER_NAME_CHECKED(UN2CSettings, MiniMaxModel),
        EN2CLLMProvider::MiniMax);

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
    if (!DefaultNameWidget.IsValid())
    {
        return;
    }

    FDetailWidgetRow& Row = PropertyRow->CustomWidget(true);
    Row.NameContent()
    [
        DefaultNameWidget.ToSharedRef()
    ]
    .ValueContent()
    .MinDesiredWidth(860.0f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        [
            SNew(SEditableTextBox)
            .ToolTipText(FText::FromString(
                TEXT("Effective API model identifier. Use Discover Models to fetch current provider models. Clearing a discovered override restores the compiled/configured fallback model.")))
            .Text_Lambda([this, Provider]()
            {
                if (!Settings.IsValid())
                {
                    return FText::GetEmpty();
                }
                return FText::FromString(GetSettingsConfiguredBuiltInModel(
                    *Settings.Get(),
                    CustomProviderSettings.Get(),
                    Provider));
            })
            .OnTextCommitted_Lambda([this, Provider](const FText& Text, ETextCommit::Type)
            {
                if (CustomProviderSettings.IsValid())
                {
                    CustomProviderSettings->SetBuiltInModelOverride(
                        Provider,
                        Text.ToString().TrimStartAndEnd());
                    ForceRefresh();
                }
            })
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
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(6.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Discover Models")))
            .ToolTipText(FText::FromString(TEXT("Fetch the provider's currently available models and choose the configured model.")))
            .OnClicked_Lambda([this, Provider]()
            {
                DiscoverModelsForProvider(Provider);
                return FReply::Handled();
            })
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(6.0f, 0.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .Text(FText::FromString(TEXT("Save Profile")))
            .ToolTipText(FText::FromString(TEXT("Save this built-in provider/model as a reusable profile that continues sharing the built-in provider configuration and API key.")))
            .IsEnabled_Lambda([this, Provider]()
            {
                return Settings.IsValid() &&
                       !GetSettingsConfiguredBuiltInModel(
                           *Settings.Get(),
                           CustomProviderSettings.Get(),
                           Provider).IsEmpty();
            })
            .OnClicked_Lambda([this, Provider]()
            {
                SaveBuiltInProviderProfile(Provider);
                return FReply::Handled();
            })
        ]
    ];
}

void FN2CSettingsCustomization::DiscoverModelsForProvider(EN2CLLMProvider Provider)
{
    if (!Settings.IsValid() || !CustomProviderSettings.IsValid() ||
        !FN2CModelDiscovery::SupportsProvider(Provider))
    {
        return;
    }

    const FString CurrentModel = GetSettingsConfiguredBuiltInModel(
        *Settings.Get(),
        CustomProviderSettings.Get(),
        Provider);

    FN2CResolvedRequestProvider DiscoveryProvider;
    DiscoveryProvider.Provider = Provider;
    DiscoveryProvider.ApiKey = GetSettingsProviderApiKey(*Settings.Get(), Provider);
    DiscoveryProvider.Model = CurrentModel;

    const TSharedRef<FSettingsModelDiscoveryDialogState> DialogState =
        MakeShared<FSettingsModelDiscoveryDialogState>();

    if (!CurrentModel.IsEmpty())
    {
        DialogState->SelectedModel = MakeShared<FString>(CurrentModel);
        DialogState->Models.Add(DialogState->SelectedModel);
    }

    bool bModelApplied = false;
    const TWeakObjectPtr<UN2CCustomProviderSettings> CustomSettingsObject = CustomProviderSettings;

    TSharedPtr<SWindow> DialogWindow;
    TSharedPtr<SListView<TSharedPtr<FString>>> ModelList;

    SAssignNew(DialogWindow, SWindow)
        .Title(FText::FromString(FString::Printf(
            TEXT("Discover %s Models"),
            *GetSettingsProviderDisplayName(Provider))))
        .ClientSize(FVector2D(640.0f, 520.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false);

    DialogWindow->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 12.0f, 12.0f, 6.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([DialogState]()
            {
                return FText::FromString(DialogState->StatusText);
            })
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(12.0f, 0.0f, 12.0f, 10.0f)
        [
            SNew(SBox)
            .MinDesiredHeight(360.0f)
            [
                SAssignNew(ModelList, SListView<TSharedPtr<FString>>)
                .ListItemsSource(&DialogState->Models)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](
                    TSharedPtr<FString> Item,
                    const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
                        .Padding(FMargin(10.0f, 5.0f))
                        [
                            SNew(STextBlock)
                            .Text(Item.IsValid()
                                ? FText::FromString(*Item)
                                : FText::GetEmpty())
                        ];
                })
                .OnSelectionChanged_Lambda([DialogState](
                    TSharedPtr<FString> Item,
                    ESelectInfo::Type)
                {
                    DialogState->SelectedModel = Item;
                })
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(12.0f, 0.0f, 12.0f, 12.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Cancel")))
                .OnClicked_Lambda([DialogWindow]()
                {
                    DialogWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Use Selected Model")))
                .IsEnabled_Lambda([DialogState]()
                {
                    return DialogState->SelectedModel.IsValid() &&
                           !DialogState->SelectedModel->IsEmpty();
                })
                .OnClicked_Lambda([
                    DialogWindow,
                    DialogState,
                    CustomSettingsObject,
                    Provider,
                    &bModelApplied]()
                {
                    if (CustomSettingsObject.IsValid() && DialogState->SelectedModel.IsValid())
                    {
                        CustomSettingsObject->SetBuiltInModelOverride(
                            Provider,
                            *DialogState->SelectedModel);
                        bModelApplied = true;
                    }
                    DialogWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]
    );

    DialogState->ModelList = ModelList;
    if (ModelList.IsValid() && DialogState->SelectedModel.IsValid())
    {
        ModelList->SetSelection(DialogState->SelectedModel, ESelectInfo::Direct);
    }

    const TWeakPtr<SWindow> WeakDialogWindow = DialogWindow;
    const bool bStarted = FN2CModelDiscovery::FetchAvailableModels(
        DiscoveryProvider,
        [DialogState, WeakDialogWindow, CurrentModel, Provider](
            bool bSuccess,
            TArray<FString> Models,
            FString Error)
        {
            if (!WeakDialogWindow.IsValid())
            {
                return;
            }

            if (!bSuccess)
            {
                DialogState->StatusText = Error.IsEmpty()
                    ? TEXT("Model discovery failed. The currently configured model remains available.")
                    : FString::Printf(
                        TEXT("Model discovery failed: %s. The currently configured model remains available."),
                        *Error);
                return;
            }

            Models.Sort([](const FString& Left, const FString& Right)
            {
                return Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
            });

            DialogState->Models.Reset();
            for (const FString& Model : Models)
            {
                if (!DialogState->Models.ContainsByPredicate(
                        [&Model](const TSharedPtr<FString>& Existing)
                        {
                            return Existing.IsValid() &&
                                   Existing->Equals(Model, ESearchCase::IgnoreCase);
                        }))
                {
                    DialogState->Models.Add(MakeShared<FString>(Model));
                }
            }

            if (!CurrentModel.IsEmpty() &&
                !DialogState->Models.ContainsByPredicate(
                    [&CurrentModel](const TSharedPtr<FString>& Existing)
                    {
                        return Existing.IsValid() &&
                               Existing->Equals(CurrentModel, ESearchCase::IgnoreCase);
                    }))
            {
                DialogState->Models.Insert(MakeShared<FString>(CurrentModel), 0);
            }

            DialogState->SelectedModel.Reset();
            if (!CurrentModel.IsEmpty())
            {
                if (TSharedPtr<FString>* MatchingModel = DialogState->Models.FindByPredicate(
                        [&CurrentModel](const TSharedPtr<FString>& Existing)
                        {
                            return Existing.IsValid() &&
                                   Existing->Equals(CurrentModel, ESearchCase::IgnoreCase);
                        }))
                {
                    DialogState->SelectedModel = *MatchingModel;
                }
            }
            if (!DialogState->SelectedModel.IsValid() && !DialogState->Models.IsEmpty())
            {
                DialogState->SelectedModel = DialogState->Models[0];
            }

            DialogState->StatusText = FString::Printf(
                TEXT("Discovered %d model(s). Select one to make it the configured %s model."),
                Models.Num(),
                *GetSettingsProviderDisplayName(Provider));

            if (const TSharedPtr<SListView<TSharedPtr<FString>>> List =
                    DialogState->ModelList.Pin())
            {
                List->RequestListRefresh();
                if (DialogState->SelectedModel.IsValid())
                {
                    List->SetSelection(DialogState->SelectedModel, ESelectInfo::Direct);
                    List->RequestScrollIntoView(DialogState->SelectedModel);
                }
            }
        });

    if (!bStarted)
    {
        DialogState->StatusText =
            TEXT("Model discovery could not be started. Check the provider API key/endpoint. The currently configured model remains available.");
    }

    FSlateApplication::Get().AddModalWindow(
        DialogWindow.ToSharedRef(),
        FSlateApplication::Get().GetActiveTopLevelWindow(),
        false);

    if (bModelApplied)
    {
        ForceRefresh();
    }
}

void FN2CSettingsCustomization::DiscoverModelsForProfile(const FString& ProviderName)
{
    if (!Settings.IsValid() || !CustomProviderSettings.IsValid())
    {
        return;
    }

    const FN2CCustomProviderDefinition* ProviderDefinition =
        CustomProviderSettings->GetProvider(ProviderName);
    if (!ProviderDefinition)
    {
        return;
    }

    const FN2CCustomProviderDefinition Definition = *ProviderDefinition;
    const FString CurrentModel = Definition.Model.TrimStartAndEnd();
    const bool bBuiltInReference =
        Definition.ProfileSource == EN2CCustomProviderProfileSource::BuiltInProvider;

    FN2CResolvedRequestProvider DiscoveryProvider;
    FString DiscoveryLabel;
    FString CustomEndpoint;
    FString CustomApiKey;

    if (bBuiltInReference)
    {
        if (!FN2CModelDiscovery::SupportsProvider(Definition.BuiltInProvider))
        {
            return;
        }

        DiscoveryProvider.Provider = Definition.BuiltInProvider;
        DiscoveryProvider.ApiKey = GetSettingsProviderApiKey(
            *Settings.Get(),
            Definition.BuiltInProvider);
        DiscoveryProvider.Model = CurrentModel;
        DiscoveryLabel = GetSettingsProviderDisplayName(Definition.BuiltInProvider);
    }
    else
    {
        CustomEndpoint = Definition.Endpoint.TrimStartAndEnd();
        CustomApiKey = CustomProviderSettings->GetApiKey(ProviderName);
        DiscoveryLabel = TEXT("OpenAI-compatible");
    }

    const TSharedRef<FSettingsModelDiscoveryDialogState> DialogState =
        MakeShared<FSettingsModelDiscoveryDialogState>();

    if (!CurrentModel.IsEmpty())
    {
        DialogState->SelectedModel = MakeShared<FString>(CurrentModel);
        DialogState->Models.Add(DialogState->SelectedModel);
    }

    bool bModelApplied = false;
    const TWeakObjectPtr<UN2CCustomProviderSettings> CustomSettingsObject = CustomProviderSettings;

    TSharedPtr<SWindow> DialogWindow;
    TSharedPtr<SListView<TSharedPtr<FString>>> ModelList;

    SAssignNew(DialogWindow, SWindow)
        .Title(FText::FromString(FString::Printf(
            TEXT("Discover Models - %s"),
            *ProviderName)))
        .ClientSize(FVector2D(640.0f, 520.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false);

    DialogWindow->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 12.0f, 12.0f, 6.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([DialogState]()
            {
                return FText::FromString(DialogState->StatusText);
            })
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(12.0f, 0.0f, 12.0f, 10.0f)
        [
            SNew(SBox)
            .MinDesiredHeight(360.0f)
            [
                SAssignNew(ModelList, SListView<TSharedPtr<FString>>)
                .ListItemsSource(&DialogState->Models)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](
                    TSharedPtr<FString> Item,
                    const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
                        .Padding(FMargin(10.0f, 5.0f))
                        [
                            SNew(STextBlock)
                            .Text(Item.IsValid()
                                ? FText::FromString(*Item)
                                : FText::GetEmpty())
                        ];
                })
                .OnSelectionChanged_Lambda([DialogState](
                    TSharedPtr<FString> Item,
                    ESelectInfo::Type)
                {
                    DialogState->SelectedModel = Item;
                })
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(12.0f, 0.0f, 12.0f, 12.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Cancel")))
                .OnClicked_Lambda([DialogWindow]()
                {
                    DialogWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Use Selected Model")))
                .IsEnabled_Lambda([DialogState]()
                {
                    return DialogState->SelectedModel.IsValid() &&
                           !DialogState->SelectedModel->IsEmpty();
                })
                .OnClicked_Lambda([
                    DialogWindow,
                    DialogState,
                    CustomSettingsObject,
                    ProviderName,
                    &bModelApplied]()
                {
                    if (CustomSettingsObject.IsValid() && DialogState->SelectedModel.IsValid())
                    {
                        if (FN2CCustomProviderDefinition* Profile =
                                CustomSettingsObject->GetProvider(ProviderName))
                        {
                            Profile->Model = *DialogState->SelectedModel;
                            CustomSettingsObject->SaveDefinitions();
                            bModelApplied = true;
                        }
                    }
                    DialogWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]
    );

    DialogState->ModelList = ModelList;
    if (ModelList.IsValid() && DialogState->SelectedModel.IsValid())
    {
        ModelList->SetSelection(DialogState->SelectedModel, ESelectInfo::Direct);
    }

    const TWeakPtr<SWindow> WeakDialogWindow = DialogWindow;
    const FN2CModelDiscovery::FOnModelsResolved OnModelsResolved =
        [DialogState, WeakDialogWindow, CurrentModel, ProviderName, DiscoveryLabel](
            bool bSuccess,
            TArray<FString> Models,
            FString Error)
        {
            if (!WeakDialogWindow.IsValid())
            {
                return;
            }

            if (!bSuccess)
            {
                DialogState->StatusText = Error.IsEmpty()
                    ? TEXT("Model discovery failed. The profile's current model remains available.")
                    : FString::Printf(
                        TEXT("Model discovery failed: %s. The profile's current model remains available."),
                        *Error);
                return;
            }

            Models.Sort([](const FString& Left, const FString& Right)
            {
                return Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
            });

            DialogState->Models.Reset();
            for (const FString& Model : Models)
            {
                if (!DialogState->Models.ContainsByPredicate(
                        [&Model](const TSharedPtr<FString>& Existing)
                        {
                            return Existing.IsValid() &&
                                   Existing->Equals(Model, ESearchCase::IgnoreCase);
                        }))
                {
                    DialogState->Models.Add(MakeShared<FString>(Model));
                }
            }

            if (!CurrentModel.IsEmpty() &&
                !DialogState->Models.ContainsByPredicate(
                    [&CurrentModel](const TSharedPtr<FString>& Existing)
                    {
                        return Existing.IsValid() &&
                               Existing->Equals(CurrentModel, ESearchCase::IgnoreCase);
                    }))
            {
                DialogState->Models.Insert(MakeShared<FString>(CurrentModel), 0);
            }

            DialogState->SelectedModel.Reset();
            if (!CurrentModel.IsEmpty())
            {
                if (TSharedPtr<FString>* MatchingModel = DialogState->Models.FindByPredicate(
                        [&CurrentModel](const TSharedPtr<FString>& Existing)
                        {
                            return Existing.IsValid() &&
                                   Existing->Equals(CurrentModel, ESearchCase::IgnoreCase);
                        }))
                {
                    DialogState->SelectedModel = *MatchingModel;
                }
            }
            if (!DialogState->SelectedModel.IsValid() && !DialogState->Models.IsEmpty())
            {
                DialogState->SelectedModel = DialogState->Models[0];
            }

            DialogState->StatusText = FString::Printf(
                TEXT("Discovered %d %s model(s). Select one to update profile '%s'."),
                Models.Num(),
                *DiscoveryLabel,
                *ProviderName);

            if (const TSharedPtr<SListView<TSharedPtr<FString>>> List =
                    DialogState->ModelList.Pin())
            {
                List->RequestListRefresh();
                if (DialogState->SelectedModel.IsValid())
                {
                    List->SetSelection(DialogState->SelectedModel, ESelectInfo::Direct);
                    List->RequestScrollIntoView(DialogState->SelectedModel);
                }
            }
        };

    const bool bStarted = bBuiltInReference
        ? FN2CModelDiscovery::FetchAvailableModels(
            DiscoveryProvider,
            OnModelsResolved)
        : FN2CModelDiscovery::FetchOpenAICompatibleModels(
            CustomEndpoint,
            CustomApiKey,
            OnModelsResolved);

    if (!bStarted)
    {
        DialogState->StatusText = bBuiltInReference
            ? TEXT("Model discovery could not be started. Check the referenced provider API key/endpoint. The profile's current model remains available.")
            : TEXT("Model discovery could not be started. Check this profile's endpoint. The profile's current model remains available.");
    }

    FSlateApplication::Get().AddModalWindow(
        DialogWindow.ToSharedRef(),
        FSlateApplication::Get().GetActiveTopLevelWindow(),
        false);

    if (bModelApplied)
    {
        ForceRefresh();
    }
}

void FN2CSettingsCustomization::SaveBuiltInProviderProfile(EN2CLLMProvider Provider)
{
    if (!Settings.IsValid() || !CustomProviderSettings.IsValid())
    {
        return;
    }

    const FString Model = GetSettingsConfiguredBuiltInModel(
        *Settings.Get(),
        CustomProviderSettings.Get(),
        Provider);
    if (Model.IsEmpty())
    {
        return;
    }

    FString ProfileName;
    if (!CustomProviderSettings->AddBuiltInProviderProfile(
            Provider,
            Model,
            ProfileName))
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to save built-in provider/model profile"),
            TEXT("ProviderProfiles"));
        return;
    }

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Saved provider profile '%s' referencing %s model %s"),
            *ProfileName,
            *GetSettingsProviderDisplayName(Provider),
            *Model),
        EN2CLogSeverity::Info,
        TEXT("ProviderProfiles"));
    ForceRefresh();
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

    const FN2CCustomProviderDefinition& ProviderDefinition =
        CustomProviderSettings->Providers[ProviderIndex];
    const FString ProviderName = ProviderDefinition.Name;
    const bool bBuiltInReference =
        ProviderDefinition.ProfileSource == EN2CCustomProviderProfileSource::BuiltInProvider;
    const EN2CLLMProvider ReferencedBuiltInProvider = ProviderDefinition.BuiltInProvider;

    FString ReferencedProviderName = UEnum::GetValueAsString(ReferencedBuiltInProvider);
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        ReferencedProviderName = ProviderEnum->GetDisplayNameTextByValue(
            static_cast<int64>(ReferencedBuiltInProvider)).ToString();
    }

    const FText ProfileSourceText = FText::FromString(
        bBuiltInReference
            ? FString::Printf(TEXT("Built-in %s (shared configuration)"), *ReferencedProviderName)
            : TEXT("Custom OpenAI-compatible endpoint"));

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
                    FText::FromString(TEXT("Provider Name")),
                    SNew(SEditableTextBox)
                    .Text(FText::FromString(ProviderName))
                    .ToolTipText(FText::FromString(TEXT("Rename this saved provider profile.")))
                    .OnTextCommitted_Lambda([this, ProviderName](const FText& Text, ETextCommit::Type)
                    {
                        if (!CustomProviderSettings.IsValid())
                        {
                            return;
                        }

                        const FString NewName = Text.ToString().TrimStartAndEnd();
                        if (NewName.Equals(ProviderName, ESearchCase::CaseSensitive))
                        {
                            return;
                        }

                        if (!CustomProviderSettings->RenameProvider(ProviderName, NewName))
                        {
                            FMessageDialog::Open(
                                EAppMsgType::Ok,
                                FText::FromString(TEXT("Unable to rename custom provider. The name cannot be empty or duplicate another custom provider.")));
                        }

                        ForceRefresh();
                    }))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Profile Source")),
                    SNew(STextBlock)
                    .Text(ProfileSourceText),
                    FText::FromString(
                        bBuiltInReference
                            ? TEXT("This profile references the built-in provider's API key, endpoint, authentication, and provider configuration. Only the model selection is stored independently here.")
                            : TEXT("This profile owns its custom endpoint and API-key settings.")))
            ]
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

                        const FN2CCustomProviderDefinition& Definition =
                            CustomProviderSettings->Providers[ProviderIndex];
                        if (Definition.ProfileSource == EN2CCustomProviderProfileSource::BuiltInProvider)
                        {
                            if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
                            {
                                return ProviderEnum->GetDisplayNameTextByValue(
                                    static_cast<int64>(Definition.BuiltInProvider));
                            }
                            return FText::FromString(UEnum::GetValueAsString(Definition.BuiltInProvider));
                        }

                        return StaticEnum<EN2CCustomProviderApiType>()->GetDisplayNameTextByValue(
                            static_cast<int64>(Definition.ApiType));
                    }))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Provider Endpoint *")),
                    SNew(SEditableTextBox)
                    .IsEnabled(!bBuiltInReference)
                    .HintText(FText::FromString(
                        bBuiltInReference
                            ? TEXT("Uses referenced built-in provider endpoint/config")
                            : TEXT("Required, e.g. https://host/api/v1")))
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
                            CustomProviderSettings->Providers.IsValidIndex(ProviderIndex) &&
                            CustomProviderSettings->Providers[ProviderIndex].ProfileSource ==
                                EN2CCustomProviderProfileSource::CustomEndpoint)
                        {
                            CustomProviderSettings->Providers[ProviderIndex].Endpoint =
                                Text.ToString().TrimStartAndEnd();
                            CustomProviderSettings->SaveDefinitions();
                        }
                    }),
                    FText::FromString(
                        bBuiltInReference
                            ? TEXT("Owned by the referenced built-in provider and intentionally not duplicated into this profile.")
                            : TEXT("Required OpenAI-compatible API base endpoint. NodeToCode appends /chat/completions internally.")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("API Key (Optional)")),
                    SNew(SEditableTextBox)
                    .IsEnabled(!bBuiltInReference)
                    .IsPassword(true)
                    .HintText(FText::FromString(
                        bBuiltInReference
                            ? TEXT("Uses referenced built-in provider API key")
                            : TEXT("Optional")))
                    .Text_Lambda([this, ProviderName, bBuiltInReference]()
                    {
                        return CustomProviderSettings.IsValid() && !bBuiltInReference
                            ? FText::FromString(CustomProviderSettings->GetApiKey(ProviderName))
                            : FText::GetEmpty();
                    })
                    .OnTextCommitted_Lambda([this, ProviderName, bBuiltInReference](const FText& Text, ETextCommit::Type)
                    {
                        if (CustomProviderSettings.IsValid() && !bBuiltInReference)
                        {
                            CustomProviderSettings->SetApiKey(ProviderName, Text.ToString());
                        }
                    }),
                    FText::FromString(
                        bBuiltInReference
                            ? TEXT("The referenced built-in provider remains the single source of truth for its API key.")
                            : TEXT("Optional provider API key.")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Model Name")),
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .VAlign(VAlign_Center)
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
                                CustomProviderSettings->Providers[ProviderIndex].Model =
                                    Text.ToString().TrimStartAndEnd();
                                CustomProviderSettings->SaveDefinitions();
                            }
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Discover Models")))
                        .ToolTipText(FText::FromString(
                            bBuiltInReference
                                ? TEXT("Fetch models from the referenced built-in provider and update only this profile's model selection.")
                                : TEXT("Fetch models from this OpenAI-compatible profile's /models endpoint and update the profile model selection.")))
                        .OnClicked_Lambda([this, ProviderName]()
                        {
                            DiscoverModelsForProfile(ProviderName);
                            return FReply::Handled();
                        })
                    ],
                    FText::FromString(
                        bBuiltInReference
                            ? TEXT("Independent model override for this profile. All other provider configuration stays linked to the built-in provider.")
                            : TEXT("Provider model identifier. Use Discover Models to query this profile's OpenAI-compatible /models endpoint.")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Max Output Tokens")),
                    SNew(SSpinBox<int32>)
                    .IsEnabled(!bBuiltInReference)
                    .MinValue(1024)
                    .MaxValue(131072)
                    .MinSliderValue(1024)
                    .MaxSliderValue(65536)
                    .Value_Lambda([this, ProviderIndex]()
                    {
                        return CustomProviderSettings.IsValid() &&
                               CustomProviderSettings->Providers.IsValidIndex(ProviderIndex)
                            ? CustomProviderSettings->Providers[ProviderIndex].MaxOutputTokens
                            : 32768;
                    })
                    .OnValueCommitted_Lambda([this, ProviderIndex](int32 Value, ETextCommit::Type)
                    {
                        if (CustomProviderSettings.IsValid() &&
                            CustomProviderSettings->Providers.IsValidIndex(ProviderIndex) &&
                            CustomProviderSettings->Providers[ProviderIndex].ProfileSource ==
                                EN2CCustomProviderProfileSource::CustomEndpoint)
                        {
                            CustomProviderSettings->Providers[ProviderIndex].MaxOutputTokens =
                                FMath::Clamp(Value, 1024, 131072);
                            CustomProviderSettings->SaveDefinitions();
                        }
                    }),
                    FText::FromString(
                        bBuiltInReference
                            ? TEXT("Uses the referenced built-in provider's request behavior.")
                            : TEXT("Maximum completion tokens for this OpenAI-compatible provider. Reasoning models may consume part of this budget before emitting the final structured response. Default: 32768.")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Use System Prompts")),
                    SNew(SCheckBox)
                    .IsEnabled(!bBuiltInReference)
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
                            CustomProviderSettings->Providers.IsValidIndex(ProviderIndex) &&
                            CustomProviderSettings->Providers[ProviderIndex].ProfileSource ==
                                EN2CCustomProviderProfileSource::CustomEndpoint)
                        {
                            CustomProviderSettings->Providers[ProviderIndex].bUseSystemPrompts =
                                State == ECheckBoxState::Checked;
                            CustomProviderSettings->SaveDefinitions();
                        }
                    }),
                    FText::FromString(
                        bBuiltInReference
                            ? TEXT("System-prompt behavior is inherited from the referenced built-in provider.")
                            : TEXT("Whether this custom OpenAI-compatible provider accepts a separate system message.")))
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
                        .OnClicked_Lambda([this, ProviderName, bBuiltInReference, ReferencedBuiltInProvider]()
                        {
                            if (bBuiltInReference)
                            {
                                if (Settings.IsValid())
                                {
                                    FN2CConnectionTester::TestProvider(
                                        ReferencedBuiltInProvider,
                                        *Settings.Get());
                                }
                            }
                            else if (CustomProviderSettings.IsValid())
                            {
                                FN2CConnectionTester::TestCustomProvider(
                                    ProviderName,
                                    *CustomProviderSettings.Get());
                            }
                            return FReply::Handled();
                        })
                    ])
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                MakeLabeledRow(
                    FText::FromString(TEXT("Management")),
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Duplicate Provider")))
                        .ToolTipText(FText::FromString(
                            bBuiltInReference
                                ? TEXT("Duplicate this saved built-in provider/model profile. The duplicate keeps the same built-in configuration reference.")
                                : TEXT("Duplicate this custom provider profile, including its endpoint, model, options, and current API key.")))
                        .OnClicked_Lambda([this, ProviderName]()
                        {
                            if (!CustomProviderSettings.IsValid())
                            {
                                return FReply::Handled();
                            }

                            FString DuplicateName;
                            if (CustomProviderSettings->DuplicateProvider(ProviderName, DuplicateName))
                            {
                                ForceRefresh();
                            }
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Delete Provider")))
                        .ToolTipText(FText::FromString(TEXT("Permanently remove this saved provider profile.")))
                        .OnClicked_Lambda([this, ProviderName]()
                        {
                            if (!CustomProviderSettings.IsValid())
                            {
                                return FReply::Handled();
                            }

                            const FText ConfirmationMessage = FText::FromString(FString::Printf(
                                TEXT("Delete provider profile '%s'?\n\nThis removes the saved profile. Built-in provider settings referenced by linked profiles are not modified."),
                                *ProviderName));

                            if (FMessageDialog::Open(EAppMsgType::YesNo, ConfirmationMessage) != EAppReturnType::Yes)
                            {
                                return FReply::Handled();
                            }

                            if (CustomProviderSettings->RemoveProvider(ProviderName))
                            {
                                ForceRefresh();
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
        if (Provider.ProfileSource == EN2CCustomProviderProfileSource::CustomEndpoint)
        {
            ActiveProviderOptions.Add(MakeShared<FString>(Provider.Name));
        }
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
