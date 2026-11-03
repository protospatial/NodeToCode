// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CSettingsCustomization.h"
#include "Core/N2CSettings.h"
#include "Core/N2CSecretStore.h"
#include "Core/N2CUserSettings.h"
#include "DetailLayoutBuilder.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Types/ISlateMetaData.h"

#define LOCTEXT_NAMESPACE "N2CSettingsCustomization"

namespace N2CSettingsCustomizationPrivate
{
	/** What the row's widgets share: the list's items, the chosen client and the token box */
	struct FRowState
	{
		TArray<TSharedPtr<EN2CClientConfigChoice>> Choices;
		TSharedPtr<EN2CClientConfigChoice> Selected;
		bool bWithToken = false;
	};

	EN2CMcpClient ToClient(EN2CClientConfigChoice Choice)
	{
		switch (Choice)
		{
		case EN2CClientConfigChoice::Cursor: return EN2CMcpClient::Cursor;
		case EN2CClientConfigChoice::VSCode: return EN2CMcpClient::VSCode;
		case EN2CClientConfigChoice::Gemini: return EN2CMcpClient::Gemini;
		case EN2CClientConfigChoice::Codex:  return EN2CMcpClient::Codex;
		default:                             return EN2CMcpClient::ClaudeCode;
		}
	}

	void Notify(const FText& Text, bool bSuccess)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 8.0f;
		Info.bUseLargeFont = false;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FN2CSettingsCustomization::MakeInstance()
{
	return MakeShared<FN2CSettingsCustomization>();
}

void FN2CSettingsCustomization::Register()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomPropertyTypeLayout(FN2CMcpClientConfigWriter::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FN2CSettingsCustomization::MakeInstance));
	PropertyEditor.RegisterCustomPropertyTypeLayout(FN2CApiKeyRemover::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FN2CApiKeyRemoverCustomization::MakeInstance));
}

void FN2CSettingsCustomization::Unregister()
{
	if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomPropertyTypeLayout(FN2CMcpClientConfigWriter::StaticStruct()->GetFName());
		PropertyEditor->UnregisterCustomPropertyTypeLayout(FN2CApiKeyRemover::StaticStruct()->GetFName());
	}
}

TSharedRef<IPropertyTypeCustomization> FN2CApiKeyRemoverCustomization::MakeInstance()
{
	return MakeShared<FN2CApiKeyRemoverCustomization>();
}

FText FN2CApiKeyRemoverCustomization::GetButtonLabel(EN2CLLMProvider Provider)
{
	return FText::Format(LOCTEXT("RemoveSavedKey", "Remove Saved {0} Key"), FText::FromString(FN2CSecretStore::GetProviderId(Provider)));
}

bool FN2CApiKeyRemoverCustomization::CanRemove(EN2CLLMProvider Provider)
{
	return !FN2CSecretStore::GetProviderId(Provider).IsEmpty()
		&& FN2CSecretStore::Get().GetApiKeySource(Provider) == EN2CApiKeySource::Store;
}

void FN2CApiKeyRemoverCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Which provider the row is for: the struct's only field
	EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;
	TArray<void*> RawData;
	PropertyHandle->AccessRawData(RawData);
	if (RawData.Num() > 0 && RawData[0])
	{
		Provider = static_cast<const FN2CApiKeyRemover*>(RawData[0])->Provider;
	}

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(220.0f)
	[
		SNew(SButton)
		.Text(GetButtonLabel(Provider))
		.ToolTipText(PropertyHandle->GetToolTipText())
		.IsEnabled_Lambda([Provider]() { return CanRemove(Provider); })
		.OnClicked_Lambda([Provider]()
		{
			const bool bRemoved = UN2CSettings::SetApiKey(Provider, FString());
			const FString Id = FN2CSecretStore::GetProviderId(Provider);
			N2CSettingsCustomizationPrivate::Notify(bRemoved
				? FText::Format(LOCTEXT("KeyRemoved", "Removed the saved {0} key."), FText::FromString(Id))
				: FText::Format(LOCTEXT("KeyNotRemoved", "Could not remove the saved {0} key; the Output Log says why."), FText::FromString(Id)),
				bRemoved);
			return FReply::Handled();
		})
	];
}

FText FN2CSettingsCustomization::GetChoiceLabel(EN2CClientConfigChoice Choice)
{
	switch (Choice)
	{
	case EN2CClientConfigChoice::All:           return LOCTEXT("ChoiceAll", "All clients");
	case EN2CClientConfigChoice::ClaudeCode:    return LOCTEXT("ChoiceClaudeCode", "Claude Code (.mcp.json)");
	case EN2CClientConfigChoice::Cursor:        return LOCTEXT("ChoiceCursor", "Cursor (.cursor/mcp.json)");
	case EN2CClientConfigChoice::VSCode:        return LOCTEXT("ChoiceVSCode", "VS Code (.vscode/mcp.json)");
	case EN2CClientConfigChoice::Gemini:        return LOCTEXT("ChoiceGemini", "Gemini CLI (.gemini/settings.json)");
	case EN2CClientConfigChoice::Codex:         return LOCTEXT("ChoiceCodex", "Codex (.codex/config.toml)");
	case EN2CClientConfigChoice::ClaudeDesktop: return LOCTEXT("ChoiceDesktop", "Claude Desktop (copy snippet)");
	default:                                    return FText::GetEmpty();
	}
}

TArray<FString> FN2CSettingsCustomization::MakeCommandArgs(EN2CClientConfigChoice Choice, bool bWithToken, const FString& BaseDir)
{
	using namespace N2CSettingsCustomizationPrivate;
	TArray<FString> Args;
	if (Choice == EN2CClientConfigChoice::All)
	{
		Args.Add(TEXT("all"));
	}
	else if (Choice == EN2CClientConfigChoice::ClaudeDesktop)
	{
		Args.Add(TEXT("desktop"));
	}
	else
	{
		Args.Add(FN2CMcpClientConfig::GetClientName(ToClient(Choice)));
	}
	Args.Add(bWithToken ? TEXT("token") : TEXT("notoken"));
	// dir= takes the rest of the line, so it goes last
	Args.Add(TEXT("dir=") + BaseDir);
	return Args;
}

FN2CClientConfigOutcome FN2CSettingsCustomization::WriteClientConfig(EN2CClientConfigChoice Choice, bool bWithToken, const FString& BaseDir)
{
	FN2CClientConfigOutcome Outcome;
	Outcome.Results = FN2CMcpClientConfig::RunCommand(MakeCommandArgs(Choice, bWithToken, BaseDir));
	if (Choice == EN2CClientConfigChoice::ClaudeDesktop)
	{
		FString UProject = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		FPaths::MakePlatformFilename(UProject);
		Outcome.DesktopSnippet = FN2CMcpClientConfig::MakeClaudeDesktopSnippet(FN2CMcpClientConfig::GetBridgeLauncherPath(), UProject);
	}
	return Outcome;
}

FText FN2CSettingsCustomization::Summarize(const FN2CClientConfigOutcome& Outcome, bool bWithToken)
{
	TArray<FString> Lines;
	for (const FN2CMcpClientConfigResult& Result : Outcome.Results)
	{
		const FString Name = FN2CMcpClientConfig::GetClientName(Result.Client);
		if (!Result.bOk)
		{
			Lines.Add(FString::Printf(TEXT("%s: not written (%s)"), *Name, *Result.Error));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("%s: %s %s"), *Name, Result.bUnchanged ? TEXT("already up to date,") : TEXT("wrote"),
				*Result.FilePath));
		}
	}
	if (!Outcome.DesktopSnippet.IsEmpty())
	{
		Lines.Add(TEXT("Claude Desktop: snippet copied to the clipboard; merge it into claude_desktop_config.json (Settings > Developer > Edit Config)"));
	}
	if (Outcome.Results.Num() > 0 && bWithToken)
	{
		Lines.Add(FString::Printf(TEXT("The configs read the bearer token from %s; set it in each client's environment (N2C.Mcp.CopyToken copies it)"),
			FN2CMcpClientConfig::TokenEnvVar));
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

void FN2CSettingsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace N2CSettingsCustomizationPrivate;

	const TSharedRef<FRowState> State = MakeShared<FRowState>();
	for (uint8 Index = 0; Index < static_cast<uint8>(EN2CClientConfigChoice::Num); ++Index)
	{
		State->Choices.Add(MakeShared<EN2CClientConfigChoice>(static_cast<EN2CClientConfigChoice>(Index)));
	}
	State->Selected = State->Choices[0];
	State->bWithToken = UN2CUserSettings::IsMcpBearerTokenRequired();

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(420.0f)
	.MaxDesiredWidth(640.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SComboBox<TSharedPtr<EN2CClientConfigChoice>>)
			.OptionsSource(&State->Choices)
			.InitiallySelectedItem(State->Selected)
			.ToolTipText(LOCTEXT("ClientChoiceTip", "Which client's config to write"))
			.OnGenerateWidget_Lambda([](TSharedPtr<EN2CClientConfigChoice> Item)
			{
				return SNew(STextBlock).Text(GetChoiceLabel(*Item)).Font(IDetailLayoutBuilder::GetDetailFont());
			})
			.OnSelectionChanged_Lambda([State](TSharedPtr<EN2CClientConfigChoice> Item, ESelectInfo::Type)
			{
				if (Item.IsValid())
				{
					State->Selected = Item;
				}
			})
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([State]() { return GetChoiceLabel(*State->Selected); })
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([State]() { return State->bWithToken ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([State](ECheckBoxState NewState) { State->bWithToken = NewState == ECheckBoxState::Checked; })
			.IsEnabled_Lambda([State]() { return *State->Selected != EN2CClientConfigChoice::ClaudeDesktop; })
			.ToolTipText(LOCTEXT("TokenTip", "Add the bearer token header, read from the NODETOCODE_MCP_TOKEN environment variable. Starts as Require MCP Bearer Token (Editor Preferences > Node to Code (User)). Claude Desktop's bridge reads the token file itself."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TokenLabel", "Bearer token"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("N2C.WriteClientConfig")))
			.ToolTipText(LOCTEXT("WriteButtonTip", "Write or merge the chosen client's config in the project folder (Claude Desktop: copy its snippet to the clipboard)"))
			.OnClicked_Lambda([State]()
			{
				const EN2CClientConfigChoice Choice = *State->Selected;
				const FN2CClientConfigOutcome Outcome = WriteClientConfig(Choice, State->bWithToken, FN2CMcpClientConfig::GetDefaultBaseDir());
				if (!Outcome.DesktopSnippet.IsEmpty())
				{
					FPlatformApplicationMisc::ClipboardCopy(*Outcome.DesktopSnippet);
				}
				const bool bAllOk = Outcome.Results.FindByPredicate([](const FN2CMcpClientConfigResult& R) { return !R.bOk; }) == nullptr;
				Notify(Summarize(Outcome, State->bWithToken && Choice != EN2CClientConfigChoice::ClaudeDesktop), bAllOk);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WriteButton", "Write MCP Client Config"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
