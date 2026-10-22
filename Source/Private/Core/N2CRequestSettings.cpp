// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CRequestSettings.h"

#include "Core/N2CCustomProviderSettings.h"
#include "Core/N2CSettings.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "LLM/N2CLLMModels.h"
#include "Misc/Paths.h"
#include "Styling/SlateTypes.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

FString FN2CRequestRuntime::SelectedCustomProviderName;
FString FN2CRequestRuntime::AdHocInstructions;
TArray<FString> FN2CRequestRuntime::AdditionalContextFilePaths;

namespace
{
struct FN2CProviderChoice
{
    FN2CResolvedRequestProvider ResolvedProvider;
    FString DisplayName;
};

struct FN2CProviderPickerState
{
    TArray<TSharedPtr<FN2CProviderChoice>> Choices;
    TSharedPtr<FN2CProviderChoice> SelectedChoice;
};

FString GetProviderDisplayName(EN2CLLMProvider Provider)
{
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        return ProviderEnum->GetDisplayNameTextByValue(static_cast<int64>(Provider)).ToString();
    }

    return UEnum::GetValueAsString(Provider);
}

FString GetRequestConfiguredBuiltInModel(
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

TSharedPtr<FN2CProviderChoice> MakeProviderChoice(
    const FN2CResolvedRequestProvider& Provider,
    const FString& Model)
{
    TSharedPtr<FN2CProviderChoice> Choice = MakeShared<FN2CProviderChoice>();
    Choice->ResolvedProvider = Provider;
    Choice->ResolvedProvider.Model = Model;

    if (!Provider.CustomProviderName.IsEmpty())
    {
        if (Provider.Provider == EN2CLLMProvider::Custom)
        {
            Choice->DisplayName = FString::Printf(
                TEXT("Custom: %s%s%s"),
                *Provider.CustomProviderName,
                Model.IsEmpty() ? TEXT("") : TEXT(" - "),
                *Model);
        }
        else
        {
            Choice->DisplayName = FString::Printf(
                TEXT("Profile: %s (%s)%s%s"),
                *Provider.CustomProviderName,
                *GetProviderDisplayName(Provider.Provider),
                Model.IsEmpty() ? TEXT("") : TEXT(" - "),
                *Model);
        }
    }
    else
    {
        Choice->DisplayName = GetProviderDisplayName(Provider.Provider);
        if (!Model.IsEmpty())
        {
            Choice->DisplayName += TEXT(" - ") + Model;
        }
    }

    return Choice;
}

void SortProviderChoices(TArray<TSharedPtr<FN2CProviderChoice>>& Choices)
{
    Choices.Sort([](
        const TSharedPtr<FN2CProviderChoice>& Left,
        const TSharedPtr<FN2CProviderChoice>& Right)
    {
        if (!Left.IsValid())
        {
            return false;
        }
        if (!Right.IsValid())
        {
            return true;
        }

        const uint8 LeftProvider = static_cast<uint8>(Left->ResolvedProvider.Provider);
        const uint8 RightProvider = static_cast<uint8>(Right->ResolvedProvider.Provider);
        if (LeftProvider != RightProvider)
        {
            return LeftProvider < RightProvider;
        }

        const bool bLeftProfile = !Left->ResolvedProvider.CustomProviderName.IsEmpty();
        const bool bRightProfile = !Right->ResolvedProvider.CustomProviderName.IsEmpty();
        if (bLeftProfile != bRightProfile)
        {
            return !bLeftProfile;
        }

        if (bLeftProfile && bRightProfile)
        {
            const int32 NameCompare = Left->ResolvedProvider.CustomProviderName.Compare(
                Right->ResolvedProvider.CustomProviderName,
                ESearchCase::IgnoreCase);
            if (NameCompare != 0)
            {
                return NameCompare < 0;
            }
        }

        return Left->ResolvedProvider.Model.Compare(
            Right->ResolvedProvider.Model,
            ESearchCase::IgnoreCase) < 0;
    });
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

    const UN2CCustomProviderSettings* CustomSettings = GetDefault<UN2CCustomProviderSettings>();

    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            OutProvider.ApiKey = Settings->OpenAI_API_Key_UI;
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::Anthropic:
            OutProvider.ApiKey = Settings->Anthropic_API_Key_UI;
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::Gemini:
            OutProvider.ApiKey = Settings->Gemini_API_Key_UI;
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::DeepSeek:
            OutProvider.ApiKey = Settings->DeepSeek_API_Key_UI;
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::Ollama:
            OutProvider.ApiKey = Settings->OllamaConfig.ApiKey;
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::LMStudio:
            OutProvider.ApiKey = TEXT("lm-studio");
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::MiniMax:
            OutProvider.ApiKey = Settings->MiniMax_API_Key_UI;
            OutProvider.Model = GetRequestConfiguredBuiltInModel(*Settings, CustomSettings, Provider);
            return true;

        case EN2CLLMProvider::Custom:
        {
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

            if (Definition->ProfileSource == EN2CCustomProviderProfileSource::BuiltInProvider)
            {
                if (Definition->BuiltInProvider == EN2CLLMProvider::Custom)
                {
                    FN2CLogger::Get().LogError(
                        FString::Printf(
                            TEXT("Saved profile '%s' has an invalid Custom built-in reference"),
                            *Definition->Name));
                    return false;
                }

                FN2CResolvedRequestProvider ReferencedProvider;
                if (!ResolveProviderConfig(
                        Definition->BuiltInProvider,
                        FString(),
                        ReferencedProvider))
                {
                    return false;
                }

                ReferencedProvider.CustomProviderName = Definition->Name;
                if (!Definition->Model.TrimStartAndEnd().IsEmpty())
                {
                    ReferencedProvider.Model = Definition->Model.TrimStartAndEnd();
                }

                OutProvider = MoveTemp(ReferencedProvider);
                return true;
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
    AdHocInstructions.Empty();
    AdditionalContextFilePaths.Reset();

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

    const TSharedRef<FN2CProviderPickerState> PickerState = MakeShared<FN2CProviderPickerState>();
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

                TSharedPtr<FN2CProviderChoice> Choice = MakeProviderChoice(
                    ResolvedProvider,
                    ResolvedProvider.Model);
                PickerState->Choices.Add(Choice);

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

        TSharedPtr<FN2CProviderChoice> Choice = MakeProviderChoice(
            ResolvedProvider,
            ResolvedProvider.Model);
        PickerState->Choices.Add(Choice);

        if (Provider == DefaultProvider)
        {
            DefaultChoice = Choice;
        }
    }

    if (PickerState->Choices.IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("No registered LLM providers are available for request selection"));
        return false;
    }

    SortProviderChoices(PickerState->Choices);
    PickerState->SelectedChoice = DefaultChoice.IsValid()
        ? DefaultChoice
        : PickerState->Choices[0];

    bool bConfirmed = false;
    FString PendingAdHocInstructions;
    TArray<TSharedPtr<FString>> AttachedFileItems;
    TSharedPtr<FString> SelectedAttachedFile;

    TSharedPtr<SWindow> DialogWindow;
    TSharedPtr<SListView<TSharedPtr<FN2CProviderChoice>>> ProviderList;
    TSharedPtr<SListView<TSharedPtr<FString>>> AttachedFileList;

    SAssignNew(DialogWindow, SWindow)
        .Title(NSLOCTEXT("NodeToCode", "SelectProviderWindowTitle", "Configure Translation Request"))
        .ClientSize(FVector2D(720.0f, 700.0f))
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
                "Configure optional per-request context, then choose a configured provider/model or saved profile. Manage live model discovery in Project Settings > Plugins > Node to Code > LLM Services."))
            .AutoWrapText(true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT(
                "NodeToCode",
                "AdHocInstructionsLabel",
                "Ad Hoc Instructions (optional)"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 10.0f)
        [
            SNew(SBox)
            .HeightOverride(125.0f)
            [
                SNew(SMultiLineEditableTextBox)
                .HintText(NSLOCTEXT(
                    "NodeToCode",
                    "AdHocInstructionsHint",
                    "Add instructions that apply only to this translation request..."))
                .OnTextChanged_Lambda([&PendingAdHocInstructions](const FText& NewText)
                {
                    PendingAdHocInstructions = NewText.ToString();
                })
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT(
                "NodeToCode",
                "AdHocContextFilesLabel",
                "Additional Context Files (optional)"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 6.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "AttachContextFiles", "Attach Files..."))
                .ToolTipText(NSLOCTEXT(
                    "NodeToCode",
                    "AttachContextFilesToolTip",
                    "Attach text/source files as context for this translation request only."))
                .OnClicked_Lambda([DialogWindow, &AttachedFileItems, &AttachedFileList]()
                {
                    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
                    if (!DesktopPlatform)
                    {
                        FN2CLogger::Get().LogError(TEXT("Desktop platform file dialog is unavailable"));
                        return FReply::Handled();
                    }

                    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(
                        DialogWindow,
                        ESlateParentWindowSearchMethod::ActiveWindow);

                    TArray<FString> SelectedFiles;
                    const FString FileTypes =
                        TEXT("Text and Source Files|*.h;*.hpp;*.hh;*.cpp;*.c;*.cc;*.cxx;*.inl;*.cs;*.py;*.js;*.ts;*.json;*.md;*.txt;*.ini;*.yaml;*.yml;*.toml;*.usf;*.ush|")
                        TEXT("All Files|*.*");

                    if (DesktopPlatform->OpenFileDialog(
                            ParentWindowHandle,
                            TEXT("Attach Additional Context Files"),
                            FPaths::ProjectDir(),
                            FString(),
                            FileTypes,
                            EFileDialogFlags::Multiple,
                            SelectedFiles))
                    {
                        for (FString FilePath : SelectedFiles)
                        {
                            FPaths::NormalizeFilename(FilePath);

                            const bool bAlreadyAttached = AttachedFileItems.ContainsByPredicate(
                                [&FilePath](const TSharedPtr<FString>& Existing)
                                {
                                    return Existing.IsValid() && Existing->Equals(FilePath, ESearchCase::IgnoreCase);
                                });

                            if (!bAlreadyAttached)
                            {
                                AttachedFileItems.Add(MakeShared<FString>(MoveTemp(FilePath)));
                            }
                        }

                        if (AttachedFileList.IsValid())
                        {
                            AttachedFileList->RequestListRefresh();
                        }
                    }

                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "RemoveAttachedContextFile", "Remove Selected"))
                .IsEnabled_Lambda([&SelectedAttachedFile]()
                {
                    return SelectedAttachedFile.IsValid();
                })
                .OnClicked_Lambda([&AttachedFileItems, &SelectedAttachedFile, &AttachedFileList]()
                {
                    if (SelectedAttachedFile.IsValid())
                    {
                        AttachedFileItems.Remove(SelectedAttachedFile);
                        SelectedAttachedFile.Reset();
                        if (AttachedFileList.IsValid())
                        {
                            AttachedFileList->ClearSelection();
                            AttachedFileList->RequestListRefresh();
                        }
                    }
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "ClearAttachedContextFiles", "Clear"))
                .IsEnabled_Lambda([&AttachedFileItems]()
                {
                    return !AttachedFileItems.IsEmpty();
                })
                .OnClicked_Lambda([&AttachedFileItems, &SelectedAttachedFile, &AttachedFileList]()
                {
                    AttachedFileItems.Reset();
                    SelectedAttachedFile.Reset();
                    if (AttachedFileList.IsValid())
                    {
                        AttachedFileList->ClearSelection();
                        AttachedFileList->RequestListRefresh();
                    }
                    return FReply::Handled();
                })
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 10.0f)
        [
            SNew(SBox)
            .HeightOverride(100.0f)
            [
                SAssignNew(AttachedFileList, SListView<TSharedPtr<FString>>)
                .ListItemsSource(&AttachedFileItems)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](
                    TSharedPtr<FString> Item,
                    const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
                        .Padding(FMargin(8.0f, 4.0f))
                        [
                            SNew(STextBlock)
                            .Text(Item.IsValid()
                                ? FText::FromString(FPaths::GetCleanFilename(*Item))
                                : FText::GetEmpty())
                            .ToolTipText(Item.IsValid()
                                ? FText::FromString(*Item)
                                : FText::GetEmpty())
                        ];
                })
                .OnSelectionChanged_Lambda([&SelectedAttachedFile](
                    TSharedPtr<FString> Item,
                    ESelectInfo::Type)
                {
                    SelectedAttachedFile = Item;
                })
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT(
                "NodeToCode",
                "ProviderModelListLabel",
                "Provider / Model"))
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(12.0f, 0.0f, 12.0f, 10.0f)
        [
            SNew(SBox)
            .MinDesiredHeight(175.0f)
            [
                SAssignNew(ProviderList, SListView<TSharedPtr<FN2CProviderChoice>>)
                .ListItemsSource(&PickerState->Choices)
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
                .OnSelectionChanged_Lambda([PickerState](
                    TSharedPtr<FN2CProviderChoice> Item,
                    ESelectInfo::Type)
                {
                    if (Item.IsValid())
                    {
                        PickerState->SelectedChoice = Item;
                    }
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
                .IsEnabled_Lambda([PickerState]()
                {
                    return PickerState->SelectedChoice.IsValid();
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

    if (ProviderList.IsValid() && PickerState->SelectedChoice.IsValid())
    {
        ProviderList->SetSelection(PickerState->SelectedChoice, ESelectInfo::Direct);
        ProviderList->RequestScrollIntoView(PickerState->SelectedChoice);
    }

    FSlateApplication::Get().AddModalWindow(
        DialogWindow.ToSharedRef(),
        FSlateApplication::Get().GetActiveTopLevelWindow(),
        false);

    if (!bConfirmed || !PickerState->SelectedChoice.IsValid())
    {
        AdHocInstructions.Empty();
        AdditionalContextFilePaths.Reset();
        FN2CLogger::Get().Log(TEXT("Translation request cancelled during provider selection"), EN2CLogSeverity::Info);
        return false;
    }

    OutProvider = PickerState->SelectedChoice->ResolvedProvider;
    SelectedCustomProviderName = OutProvider.Provider == EN2CLLMProvider::Custom
        ? OutProvider.CustomProviderName
        : FString();
    AdHocInstructions = PendingAdHocInstructions.TrimStartAndEnd();

    AdditionalContextFilePaths.Reserve(AttachedFileItems.Num());
    for (const TSharedPtr<FString>& AttachedFile : AttachedFileItems)
    {
        if (AttachedFile.IsValid())
        {
            AdditionalContextFilePaths.Add(*AttachedFile);
        }
    }

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Selected request provider: %s, model: %s, profile: %s, ad-hoc instructions: %s, ad-hoc context files: %d"),
            *GetProviderDisplayName(OutProvider.Provider),
            *OutProvider.Model,
            OutProvider.CustomProviderName.IsEmpty() ? TEXT("none") : *OutProvider.CustomProviderName,
            AdHocInstructions.IsEmpty() ? TEXT("no") : TEXT("yes"),
            AdditionalContextFilePaths.Num()),
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
