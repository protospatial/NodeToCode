// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CRequestSettings.h"

#include "Core/N2CCustomProviderSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace N2CRetryProviderPickerPrivate
{
struct FRetryProviderChoice
{
    FN2CResolvedRequestProvider Provider;
    FString DisplayName;
};

FString ProviderDisplayName(EN2CLLMProvider Provider)
{
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        return ProviderEnum->GetDisplayNameTextByValue(static_cast<int64>(Provider)).ToString();
    }
    return UEnum::GetValueAsString(Provider);
}

FString ChoiceDisplayName(const FN2CResolvedRequestProvider& Provider)
{
    FString Result;
    if (!Provider.CustomProviderName.IsEmpty())
    {
        Result = Provider.Provider == EN2CLLMProvider::Custom
            ? FString::Printf(TEXT("Custom: %s"), *Provider.CustomProviderName)
            : FString::Printf(
                TEXT("Profile: %s (%s)"),
                *Provider.CustomProviderName,
                *ProviderDisplayName(Provider.Provider));
    }
    else
    {
        Result = ProviderDisplayName(Provider.Provider);
    }

    if (!Provider.Model.IsEmpty())
    {
        Result += TEXT(" - ") + Provider.Model;
    }
    return Result;
}

bool IsDefaultChoice(
    const FN2CResolvedRequestProvider& Provider,
    EN2CLLMProvider DefaultProvider,
    const FString& DefaultCustomProviderName,
    const FString& DefaultModel)
{
    if (Provider.Provider != DefaultProvider)
    {
        return false;
    }

    if (!DefaultCustomProviderName.IsEmpty() &&
        !Provider.CustomProviderName.Equals(DefaultCustomProviderName, ESearchCase::IgnoreCase))
    {
        return false;
    }

    return DefaultModel.IsEmpty() || Provider.Model.Equals(DefaultModel, ESearchCase::IgnoreCase);
}

void SortChoices(TArray<TSharedPtr<FRetryProviderChoice>>& Choices)
{
    Choices.Sort([](
        const TSharedPtr<FRetryProviderChoice>& Left,
        const TSharedPtr<FRetryProviderChoice>& Right)
    {
        if (!Left.IsValid())
        {
            return false;
        }
        if (!Right.IsValid())
        {
            return true;
        }

        const uint8 LeftProvider = static_cast<uint8>(Left->Provider.Provider);
        const uint8 RightProvider = static_cast<uint8>(Right->Provider.Provider);
        if (LeftProvider != RightProvider)
        {
            return LeftProvider < RightProvider;
        }

        return Left->DisplayName.Compare(Right->DisplayName, ESearchCase::IgnoreCase) < 0;
    });
}
}

bool FN2CRequestRuntime::ResolveProviderForRetry(
    EN2CLLMProvider DefaultProvider,
    const FString& DefaultCustomProviderName,
    const FString& DefaultModel,
    const TArray<EN2CLLMProvider>& AvailableProviders,
    FN2CResolvedRequestProvider& OutProvider)
{
    using namespace N2CRetryProviderPickerPrivate;

    if (!FSlateApplication::IsInitialized())
    {
        return false;
    }

    TArray<EN2CLLMProvider> SortedProviders = AvailableProviders;
    SortedProviders.Sort([](EN2CLLMProvider Left, EN2CLLMProvider Right)
    {
        return static_cast<uint8>(Left) < static_cast<uint8>(Right);
    });

    TArray<TSharedPtr<FRetryProviderChoice>> Choices;
    TSharedPtr<FRetryProviderChoice> DefaultChoice;
    const UN2CCustomProviderSettings* CustomSettings = GetDefault<UN2CCustomProviderSettings>();

    auto AddChoice = [&](const FN2CResolvedRequestProvider& Resolved)
    {
        TSharedPtr<FRetryProviderChoice> Choice = MakeShared<FRetryProviderChoice>();
        Choice->Provider = Resolved;
        Choice->DisplayName = ChoiceDisplayName(Resolved);
        Choices.Add(Choice);

        if (IsDefaultChoice(
                Resolved,
                DefaultProvider,
                DefaultCustomProviderName,
                DefaultModel))
        {
            DefaultChoice = Choice;
        }
    };

    for (const EN2CLLMProvider Provider : SortedProviders)
    {
        if (Provider == EN2CLLMProvider::Custom)
        {
            if (!CustomSettings)
            {
                continue;
            }

            for (const FN2CCustomProviderDefinition& Definition : CustomSettings->Providers)
            {
                FN2CResolvedRequestProvider Resolved;
                if (ResolveProviderConfig(Provider, Definition.Name, Resolved))
                {
                    AddChoice(Resolved);
                }
            }
            continue;
        }

        FN2CResolvedRequestProvider Resolved;
        if (ResolveProviderConfig(Provider, FString(), Resolved))
        {
            AddChoice(Resolved);
        }
    }

    if (Choices.IsEmpty())
    {
        return false;
    }

    SortChoices(Choices);
    if (!DefaultChoice.IsValid() && !DefaultCustomProviderName.IsEmpty())
    {
        TSharedPtr<FRetryProviderChoice>* SameProfileChoice = Choices.FindByPredicate(
            [&DefaultCustomProviderName](const TSharedPtr<FRetryProviderChoice>& Choice)
            {
                return Choice.IsValid() &&
                       Choice->Provider.CustomProviderName.Equals(
                           DefaultCustomProviderName,
                           ESearchCase::IgnoreCase);
            });
        if (SameProfileChoice)
        {
            DefaultChoice = *SameProfileChoice;
        }
    }

    if (!DefaultChoice.IsValid())
    {
        TSharedPtr<FRetryProviderChoice>* SameProviderChoice = Choices.FindByPredicate(
            [DefaultProvider](const TSharedPtr<FRetryProviderChoice>& Choice)
            {
                return Choice.IsValid() && Choice->Provider.Provider == DefaultProvider;
            });
        DefaultChoice = SameProviderChoice ? *SameProviderChoice : Choices[0];
    }

    TSharedPtr<FRetryProviderChoice> SelectedChoice = DefaultChoice;
    const TSharedRef<FString> PendingModel = MakeShared<FString>(
        DefaultModel.IsEmpty() ? SelectedChoice->Provider.Model : DefaultModel);
    bool bConfirmed = false;

    TSharedPtr<SWindow> DialogWindow;
    TSharedPtr<SListView<TSharedPtr<FRetryProviderChoice>>> ProviderList;

    SAssignNew(DialogWindow, SWindow)
        .Title(NSLOCTEXT("NodeToCode", "RetryProviderWindowTitle", "Resend Request With Provider / Model"))
        .ClientSize(FVector2D(680.0f, 520.0f))
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
                "RetryProviderDescription",
                "Choose the provider/profile for this replacement response. The original semantic request is reformatted for the selected provider. You may override the saved model identifier below."))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(12.0f, 0.0f, 12.0f, 10.0f)
        [
            SNew(SBox)
            .MinDesiredHeight(240.0f)
            [
                SAssignNew(ProviderList, SListView<TSharedPtr<FRetryProviderChoice>>)
                .ListItemsSource(&Choices)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](
                    TSharedPtr<FRetryProviderChoice> Item,
                    const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FRetryProviderChoice>>, OwnerTable)
                        .Padding(FMargin(10.0f, 5.0f))
                        [
                            SNew(STextBlock)
                            .Text(Item.IsValid()
                                ? FText::FromString(Item->DisplayName)
                                : FText::GetEmpty())
                        ];
                })
                .OnSelectionChanged_Lambda([&SelectedChoice, PendingModel](
                    TSharedPtr<FRetryProviderChoice> Item,
                    ESelectInfo::Type)
                {
                    if (Item.IsValid())
                    {
                        SelectedChoice = Item;
                        *PendingModel = Item->Provider.Model;
                    }
                })
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT("NodeToCode", "RetryModelLabel", "Model"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.0f, 0.0f, 12.0f, 12.0f)
        [
            SNew(SEditableTextBox)
            .Text_Lambda([PendingModel]()
            {
                return FText::FromString(*PendingModel);
            })
            .OnTextChanged_Lambda([PendingModel](const FText& NewText)
            {
                *PendingModel = NewText.ToString();
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
                .Text(NSLOCTEXT("NodeToCode", "RetryProviderCancel", "Cancel"))
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
                .Text(NSLOCTEXT("NodeToCode", "RetryProviderSend", "Resend & Re-parse"))
                .IsEnabled_Lambda([&SelectedChoice, PendingModel]()
                {
                    return SelectedChoice.IsValid() && !PendingModel->TrimStartAndEnd().IsEmpty();
                })
                .OnClicked_Lambda([DialogWindow, &bConfirmed]()
                {
                    bConfirmed = true;
                    DialogWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]);

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
        return false;
    }

    OutProvider = SelectedChoice->Provider;
    OutProvider.Model = PendingModel->TrimStartAndEnd();
    return !OutProvider.Model.IsEmpty();
}
