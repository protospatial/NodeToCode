// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CRequestSettings.h"

#include "Core/N2CCustomProviderSettings.h"
#include "Core/N2CSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "LLM/N2CLLMModels.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

FString FN2CRequestRuntime::SelectedCustomProviderName;

namespace
{
struct FN2CProviderChoice
{
    FN2CResolvedRequestProvider ResolvedProvider;
    FString DisplayName;
};

FString GetProviderDisplayName(EN2CLLMProvider Provider)
{
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        return ProviderEnum->GetDisplayNameTextByValue(static_cast<int64>(Provider)).ToString();
    }

    return UEnum::GetValueAsString(Provider);
}
}

FString UN2CRequestSettings::GetEffectiveCustomInstructions(
    EN2CLLMProvider Provider,
    const FString& Model) const
{
    FString GlobalInstructions;
    if (bEnableGlobalCustomInstructions)
    {
        GlobalInstructions = GlobalCustomInstructions.TrimStartAndEnd();
    }

    const FString ModelToMatch = Model.TrimStartAndEnd();
    const FN2CModelCustomInstructions* MatchingEntry = nullptr;

    // Later entries intentionally win so a user can append a temporary override without deleting
    // an older configuration entry.
    for (int32 Index = ModelCustomInstructions.Num() - 1; Index >= 0; --Index)
    {
        const FN2CModelCustomInstructions& Entry = ModelCustomInstructions[Index];
        if (!Entry.bEnabled || Entry.Provider != Provider)
        {
            continue;
        }

        const FString EntryModel = Entry.Model.TrimStartAndEnd();
        if (!EntryModel.IsEmpty() && EntryModel.Equals(ModelToMatch, ESearchCase::IgnoreCase))
        {
            MatchingEntry = &Entry;
            break;
        }
    }

    if (!MatchingEntry)
    {
        return GlobalInstructions;
    }

    const FString ModelInstructions = MatchingEntry->Instructions.TrimStartAndEnd();
    if (ModelInstructions.IsEmpty())
    {
        return GlobalInstructions;
    }

    if (MatchingEntry->Mode == EN2CModelInstructionMode::ReplaceGlobalInstructions)
    {
        return ModelInstructions;
    }

    if (GlobalInstructions.IsEmpty())
    {
        return ModelInstructions;
    }

    return GlobalInstructions + TEXT("\n\n") + ModelInstructions;
}

bool FN2CRequestRuntime::ResolveProviderConfig(
    EN2CLLMProvider Provider,
    const FString& CustomProviderName,
    FN2CResolvedRequestProvider& OutProvider)
{
    OutProvider = FN2CResolvedRequestProvider();
    OutProvider.Provider = Provider;

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load Node to Code settings while resolving request provider"));
        return false;
    }

    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            OutProvider.ApiKey = Settings->OpenAI_API_Key_UI;
            OutProvider.Model = FN2CLLMModelUtils::GetOpenAIModelValue(Settings->OpenAI_Model);
            return true;

        case EN2CLLMProvider::Anthropic:
            OutProvider.ApiKey = Settings->Anthropic_API_Key_UI;
            OutProvider.Model = FN2CLLMModelUtils::GetAnthropicModelValue(Settings->AnthropicModel);
            return true;

        case EN2CLLMProvider::Gemini:
            OutProvider.ApiKey = Settings->Gemini_API_Key_UI;
            OutProvider.Model = FN2CLLMModelUtils::GetGeminiModelValue(Settings->Gemini_Model);
            return true;

        case EN2CLLMProvider::DeepSeek:
            OutProvider.ApiKey = Settings->DeepSeek_API_Key_UI;
            OutProvider.Model = FN2CLLMModelUtils::GetDeepSeekModelValue(Settings->DeepSeekModel);
            return true;

        case EN2CLLMProvider::Ollama:
            OutProvider.ApiKey = Settings->OllamaConfig.ApiKey;
            OutProvider.Model = Settings->OllamaModel;
            return true;

        case EN2CLLMProvider::LMStudio:
            OutProvider.ApiKey = TEXT("lm-studio");
            OutProvider.Model = Settings->LMStudioModel;
            return true;

        case EN2CLLMProvider::MiniMax:
            OutProvider.ApiKey = Settings->MiniMax_API_Key_UI;
            OutProvider.Model = Settings->MiniMaxModel;
            return true;

        case EN2CLLMProvider::Custom:
        {
            const UN2CCustomProviderSettings* CustomSettings = GetDefault<UN2CCustomProviderSettings>();
            if (!CustomSettings)
            {
                FN2CLogger::Get().LogError(TEXT("Failed to load custom provider settings"));
                return false;
            }

            const FN2CCustomProviderDefinition* Definition = CustomProviderName.IsEmpty()
                ? CustomSettings->GetActiveProvider()
                : CustomSettings->GetProvider(CustomProviderName);

            if (!Definition)
            {
                FN2CLogger::Get().LogError(TEXT("No custom provider is available for this request"));
                return false;
            }

            OutProvider.CustomProviderName = Definition->Name;
            OutProvider.ApiKey = CustomSettings->GetApiKey(Definition->Name);
            OutProvider.Model = Definition->Model;
            return true;
        }

        default:
            FN2CLogger::Get().LogError(TEXT("Unsupported LLM provider while resolving request configuration"));
            return false;
    }
}

bool FN2CRequestRuntime::ResolveProviderForRequest(
    EN2CLLMProvider DefaultProvider,
    const TArray<EN2CLLMProvider>& AvailableProviders,
    FN2CResolvedRequestProvider& OutProvider)
{
    SelectedCustomProviderName.Empty();

    const UN2CSettings* RequestSettings = GetDefault<UN2CSettings>();
    const bool bSelectProvider = RequestSettings &&
        RequestSettings->RequestDispatchMode == EN2CRequestDispatchMode::SelectProviderBeforeSending;

    if (!bSelectProvider)
    {
        if (!ResolveProviderConfig(DefaultProvider, FString(), OutProvider))
        {
            return false;
        }

        SelectedCustomProviderName = OutProvider.CustomProviderName;
        return true;
    }

    if (!FSlateApplication::IsInitialized())
    {
        FN2CLogger::Get().LogError(TEXT("Cannot select an LLM provider because Slate is not initialized"));
        return false;
    }

    TArray<EN2CLLMProvider> SortedProviders = AvailableProviders;
    SortedProviders.Sort([](EN2CLLMProvider Left, EN2CLLMProvider Right)
    {
        return static_cast<uint8>(Left) < static_cast<uint8>(Right);
    });

    TArray<TSharedPtr<FN2CProviderChoice>> Choices;
    TSharedPtr<FN2CProviderChoice> DefaultChoice;

    const UN2CCustomProviderSettings* CustomSettings = GetDefault<UN2CCustomProviderSettings>();
    const FString ActiveCustomProviderName = CustomSettings
        ? CustomSettings->ActiveProviderName
        : FString();

    for (EN2CLLMProvider Provider : SortedProviders)
    {
        if (Provider == EN2CLLMProvider::Custom)
        {
            if (!CustomSettings)
            {
                continue;
            }

            for (const FN2CCustomProviderDefinition& Definition : CustomSettings->Providers)
            {
                FN2CResolvedRequestProvider ResolvedProvider;
                if (!ResolveProviderConfig(Provider, Definition.Name, ResolvedProvider))
                {
                    continue;
                }

                TSharedPtr<FN2CProviderChoice> Choice = MakeShared<FN2CProviderChoice>();
                Choice->ResolvedProvider = ResolvedProvider;
                Choice->DisplayName = FString::Printf(
                    TEXT("Custom: %s%s%s"),
                    *Definition.Name,
                    ResolvedProvider.Model.IsEmpty() ? TEXT("") : TEXT(" - "),
                    *ResolvedProvider.Model);
                Choices.Add(Choice);

                if (DefaultProvider == EN2CLLMProvider::Custom &&
                    Definition.Name.Equals(ActiveCustomProviderName, ESearchCase::IgnoreCase))
                {
                    DefaultChoice = Choice;
                }
            }

            continue;
        }

        FN2CResolvedRequestProvider ResolvedProvider;
        if (!ResolveProviderConfig(Provider, FString(), ResolvedProvider))
        {
            continue;
        }

        TSharedPtr<FN2CProviderChoice> Choice = MakeShared<FN2CProviderChoice>();
        Choice->ResolvedProvider = ResolvedProvider;
        Choice->DisplayName = GetProviderDisplayName(Provider);
        if (!ResolvedProvider.Model.IsEmpty())
        {
            Choice->DisplayName += TEXT(" - ") + ResolvedProvider.Model;
        }
        Choices.Add(Choice);

        if (Provider == DefaultProvider)
        {
            DefaultChoice = Choice;
        }
    }

    if (Choices.IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("No registered LLM providers are available for request selection"));
        return false;
    }

    TSharedPtr<FN2CProviderChoice> SelectedChoice = DefaultChoice.IsValid()
        ? DefaultChoice
        : Choices[0];
    bool bConfirmed = false;

    TSharedPtr<SWindow> DialogWindow;
    TSharedPtr<SListView<TSharedPtr<FN2CProviderChoice>>> ProviderList;

    SAssignNew(DialogWindow, SWindow)
        .Title(NSLOCTEXT("NodeToCode", "SelectProviderWindowTitle", "Select LLM Provider"))
        .ClientSize(FVector2D(620.0f, 390.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false);

    DialogWindow->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 12.0f, 12.0f, 8.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT(
                "NodeToCode",
                "SelectProviderDescription",
                "Choose the provider for this translation request. Your saved default provider will not be changed."))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(12.0f, 0.0f, 12.0f, 12.0f)
        [
            SAssignNew(ProviderList, SListView<TSharedPtr<FN2CProviderChoice>>)
            .ListItemsSource(&Choices)
            .SelectionMode(ESelectionMode::Single)
            .OnGenerateRow_Lambda([](
                TSharedPtr<FN2CProviderChoice> Item,
                const TSharedRef<STableViewBase>& OwnerTable)
            {
                return SNew(STableRow<TSharedPtr<FN2CProviderChoice>>, OwnerTable)
                    .Padding(FMargin(10.0f, 5.0f))
                    [
                        SNew(STextBlock)
                        .Text(Item.IsValid()
                            ? FText::FromString(Item->DisplayName)
                            : FText::GetEmpty())
                    ];
            })
            .OnSelectionChanged_Lambda([&SelectedChoice](
                TSharedPtr<FN2CProviderChoice> Item,
                ESelectInfo::Type)
            {
                if (Item.IsValid())
                {
                    SelectedChoice = Item;
                }
            })
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
                .Text(NSLOCTEXT("NodeToCode", "SelectProviderCancel", "Cancel"))
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
                .Text(NSLOCTEXT("NodeToCode", "SelectProviderSend", "Send Request"))
                .IsEnabled_Lambda([&SelectedChoice]()
                {
                    return SelectedChoice.IsValid();
                })
                .OnClicked_Lambda([DialogWindow, &bConfirmed]()
                {
                    bConfirmed = true;
                    DialogWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]
    );

    if (ProviderList.IsValid() && SelectedChoice.IsValid())
    {
        ProviderList->SetSelection(SelectedChoice, ESelectInfo::Direct);
        ProviderList->RequestScrollIntoView(SelectedChoice);
    }

    FSlateApplication::Get().AddModalWindow(
        DialogWindow.ToSharedRef(),
        FSlateApplication::Get().GetActiveTopLevelWindow(),
        false);

    if (!bConfirmed || !SelectedChoice.IsValid())
    {
        FN2CLogger::Get().Log(TEXT("Translation request cancelled during provider selection"), EN2CLogSeverity::Info);
        return false;
    }

    OutProvider = SelectedChoice->ResolvedProvider;
    SelectedCustomProviderName = OutProvider.CustomProviderName;

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Selected request provider: %s, model: %s"),
            *GetProviderDisplayName(OutProvider.Provider),
            *OutProvider.Model),
        EN2CLogSeverity::Info,
        TEXT("RequestSelection"));

    return true;
}

FString FN2CRequestRuntime::ConsumeSelectedCustomProviderName()
{
    FString Result = SelectedCustomProviderName;
    SelectedCustomProviderName.Empty();
    return Result;
}
