// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Widgets/SOverlay.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Widgets/SN2CHeaderBar.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Core/N2CGraphStateManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Core/Style/N2CStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/N2CTargetLanguage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Utils/N2CPathUtils.h"

#define LOCTEXT_NAMESPACE "SN2CTranslationViewer"

void SN2CTranslationViewer::Construct(const FArguments& InArgs)
{
	OnCloseRequestedDelegate = InArgs._OnCloseRequested;
	ActiveFileType = TEXT("cpp"); // Open on the implementation, which every language has
	CurrentLanguage = EN2CCodeLanguage::Cpp;
	bHasTranslation = false;

	ChildSlot
	[
		SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Light)
		.Padding(0)
		[
			SNew(SVerticalBox)

			// Header with graph name and close button
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(HeaderBar, SN2CHeaderBar)
				.Title(LOCTEXT("NoTranslation", "No Translation Loaded"))
				.ShowCloseButton(true)
				.OnClose(this, &SN2CTranslationViewer::HandleCloseClicked)
			]

			// Toolbar with file tabs and copy button
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SN2CPanel)
				.Variant(EN2CPanelVariant::Light)
				.Padding(FMargin(FN2CSpacing::LG, FN2CSpacing::MD))
				[
					SNew(SHorizontalBox)
					// File tabs
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
						[
							CreateFileTab(TAttribute<FText>::CreateSP(this, &SN2CTranslationViewer::GetImplementationTabLabel), TEXT("cpp"), CppTabButton)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
						[
							CreateFileTab(TAttribute<FText>::CreateSP(this, &SN2CTranslationViewer::GetDeclarationTabLabel), TEXT("h"), HeaderTabButton)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							CreateFileTab(FText::FromString(TEXT("JSON")), TEXT("json"), JsonTabButton)
						]
					]
					// Verify and Refine's status (card 0313), in the space between the tabs and Copy
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::LG, 0.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return VerifyStatusText; })
						.ToolTipText_Lambda([this]() { return VerifyStatusTooltip; })
						.Font(FN2CFonts::Medium())
						.ColorAndOpacity_Lambda([this]()
						{
							switch (VerifyStatus)
							{
							case EN2CVerifyStatus::Passed: return N2C::UI::Linear(N2C::UI::Colors().AccentGreen);
							case EN2CVerifyStatus::Failed: return N2C::UI::Linear(N2C::UI::Colors().AccentRed);
							default: return N2C::UI::Linear(N2C::UI::Colors().TextMuted);
							}
						})
						.Visibility_Lambda([this]() { return VerifyStatusText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					]
					// Copy button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SN2CIconButton)
						.IconBrush(FAppStyle::GetBrush("Icons.Clipboard"))
						.IconSize(FVector2D(FN2CSizing::IconSM, FN2CSizing::IconSM))
						.ButtonVariant(EN2CButtonVariant::Standard)
						.ContentPadding(FMargin(FN2CSpacing::SM))
						.OnClicked(this, &SN2CTranslationViewer::HandleCopyCodeClicked)
						.ToolTipText(LOCTEXT("CopyCodeTooltip", "Copy code to clipboard"))
					]
				]
			]

			// Code viewer
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SN2CPanel)
				.Variant(EN2CPanelVariant::Dark)
				.Padding(0)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(CodeEditor, SN2CCodeEditor)
						.Language(EN2CCodeLanguage::Cpp)
					]
					// Nothing loaded: say how to get a translation here instead of a blank editor (card 0269)
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::XXL)
					[
						SNew(STextBlock)
						.Text(GetEmptyText())
						.Visibility_Lambda([this]() { return IsShowingEmptyState() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						.AutoWrapText(true)
						.Justification(ETextJustify::Center)
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
					]
				]
			]

			// Notes section (collapsible)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(NotesSection, SExpandableArea)
				.AreaTitle(LOCTEXT("NotesHeader", "Translation Notes"))
				.InitiallyCollapsed(false)
				.BorderImage(&N2CStyle::GetPanelBorderBrush())
				.BorderBackgroundColor(N2C::UI::Bind(&FN2CUIColors::BgPanelDarker))
				.HeaderPadding(FMargin(FN2CSpacing::LG, FN2CSpacing::MD))
				.Padding(FMargin(0.0f))
				.BodyContent()
				[
					SNew(SBox)
					.MaxDesiredHeight(150.0f)
					[
						SNew(SN2CPanel)
						.Variant(EN2CPanelVariant::Dark)
						.Padding(FMargin(FN2CSpacing::XL, FN2CSpacing::LG))
						[
							SNew(SHorizontalBox)
							// Notes text
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(NotesText, STextBlock)
									.Text(LOCTEXT("NoNotes", "No implementation notes available."))
									.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
									.AutoWrapText(true)
								]
							]
							// Copy notes button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Top)
							.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
							[
								SNew(SN2CIconButton)
								.IconBrush(FAppStyle::GetBrush("Icons.Clipboard"))
								.IconSize(FVector2D(FN2CSizing::IconSM, FN2CSizing::IconSM))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.ContentPadding(FMargin(FN2CSpacing::SM))
								.OnClicked(this, &SN2CTranslationViewer::HandleCopyNotesClicked)
								.ToolTipText(LOCTEXT("CopyNotesTooltip", "Copy notes to clipboard"))
							]
						]
					]
				]
			]
		]
	];

	// Only C++ translations have a declaration; hide the tab (and its slot) for the other languages
	HeaderTabButton->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SN2CTranslationViewer::GetHeaderTabVisibility));

	UpdateTabStyles();
}

TSharedRef<SWidget> SN2CTranslationViewer::CreateFileTab(const TAttribute<FText>& Label, const FString& FileType, TSharedPtr<SButton>& OutButton)
{
	return SAssignNew(OutButton, SButton)
		.ButtonStyle(&N2CStyle::GetButtonStyle())
		.ContentPadding(FMargin(FN2CSpacing::LG, FN2CSpacing::XS))
		.OnClicked(this, &SN2CTranslationViewer::HandleFileTabClicked, FileType)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FN2CFonts::Medium())
			.ColorAndOpacity(this, &SN2CTranslationViewer::GetTabTextColor, FileType)
		];
}

bool SN2CTranslationViewer::LoadTranslation(const FN2CTagInfo& GraphInfo)
{
	// Try to load translation from state manager
	FN2CGraphTranslation Translation;
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
	// The row's own Blueprint: a duplicated Blueprint's graph has the same GUID (card 0192)
	const FN2CGraphKey Key = FN2CGraphKey::FromStrings(GraphInfo.BlueprintPath, GraphInfo.GraphGuid);
	if (!Key.IsValid())
	{
		return false;
	}

	if (UN2CGraphStateManager::Get().LoadTranslation(Key, Translation, Language))
	{
		// Try to get JSON content as well
		FString JsonContent;
		UN2CGraphStateManager::Get().LoadN2CJson(Key, JsonContent);

		LoadTranslation(Translation, GraphInfo.GraphName, JsonContent, Language);

		// How Verify and Refine ended for it, when it ran (Verify.json beside the entry folder)
		FN2CVerifyOutcome Verify;
		const FN2CGraphState* State = UN2CGraphStateManager::Get().FindGraphState(Key);
		SetVerifyOutcome(State && LoadRecordedVerifyOutcome(State->Translation.OutputPath, Verify) ? &Verify : nullptr);
		return true;
	}

	return false;
}

void SN2CTranslationViewer::LoadTranslation(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent)
{
	// A single translate writes in the settings' Target Language (UN2CLLMModule reads the same setting)
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	LoadTranslation(Translation, GraphName, JsonContent, Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp);
}

void SN2CTranslationViewer::LoadTranslation(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent,
	EN2CCodeLanguage Language)
{
	CurrentTranslation = Translation;
	CurrentLanguage = Language;
	CurrentGraphName = GraphName;
	CurrentJsonContent = JsonContent;
	bHasTranslation = true;
	SetVerifyOutcome(nullptr);

	// Update graph name display
	if (HeaderBar.IsValid())
	{
		HeaderBar->SetTitle(FText::FromString(GraphName));
	}

	// Update notes
	if (NotesText.IsValid())
	{
		FString Notes = Translation.Code.ImplementationNotes;
		if (Notes.IsEmpty())
		{
			Notes = TEXT("No implementation notes available.");
		}
		NotesText->SetText(FText::FromString(Notes));
	}

	// Open on the implementation (the .h tab is hidden when there is no declaration)
	ActiveFileType = TEXT("cpp");
	UpdateTabStyles();
	UpdateCodeDisplay();
}

void SN2CTranslationViewer::SetVerifyOutcome(const FN2CVerifyOutcome* Outcome)
{
	VerifyStatus = Outcome ? Outcome->Status : EN2CVerifyStatus::Off;
	VerifyStatusText = Outcome ? Outcome->GetStatusText() : FText::GetEmpty();
	VerifyStatusTooltip = FText::GetEmpty();
	if (!Outcome || Outcome->Attempts.Num() == 0)
	{
		return;
	}
	// The last attempt's findings, a few lines
	TArray<FString> Lines;
	for (const FN2CVerifyReport& Report : Outcome->Attempts.Last().Reports)
	{
		for (const FN2CVerifyDiagnostic& Diagnostic : Report.Diagnostics)
		{
			if (Lines.Num() < 8)
			{
				Lines.Add(FString::Printf(TEXT("%s %s: %s"), Diagnostic.Severity == EN2CDiagnosticSeverity::Error ? TEXT("Error") : TEXT("Warning"),
					*Diagnostic.Code, *Diagnostic.Message));
			}
		}
	}
	FString Tooltip = Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : FString(TEXT("No findings."));
	if (!Outcome->Error.IsEmpty())
	{
		Tooltip += TEXT("\n") + Outcome->Error;
	}
	VerifyStatusTooltip = FText::FromString(Tooltip);
}

bool SN2CTranslationViewer::LoadRecordedVerifyOutcome(const FString& RecordOutputPath, FN2CVerifyOutcome& OutOutcome)
{
	if (RecordOutputPath.IsEmpty())
	{
		return false;
	}
	// The record's OutputPath is the root entry's folder; Verify.json is in the folder above it (the run, or the batch item)
	const FString EntryFolder = FN2CPathUtils::ResolveTranslationRecordPath(RecordOutputPath);
	FString Text;
	return FFileHelper::LoadFileToString(Text, *(FPaths::GetPath(EntryFolder) / FN2CVerifyOutcome::FileName()))
		&& FN2CVerifyOutcome::FromJsonString(Text, OutOutcome);
}

void SN2CTranslationViewer::SetJsonContent(const FString& JsonContent, const FString& GraphName)
{
	CurrentJsonContent = JsonContent;
	CurrentGraphName = GraphName;

	// Clear translation data
	CurrentTranslation = FN2CGraphTranslation();
	bHasTranslation = false;

	// Update graph name display
	if (HeaderBar.IsValid())
	{
		HeaderBar->SetTitle(FText::FromString(GraphName));
	}

	// Clear notes
	if (NotesText.IsValid())
	{
		NotesText->SetText(LOCTEXT("NoNotes", "No implementation notes available."));
	}

	// Switch to JSON tab
	ActiveFileType = TEXT("json");
	UpdateTabStyles();
	UpdateCodeDisplay();
}

FText SN2CTranslationViewer::GetEmptyText()
{
	return LOCTEXT("ViewerEmpty", "No translation is loaded.\n\nTranslate a graph with its row's Translate button, Batch Translate or the Translate button on the N2C bar of a graph; then double-click its row or use View Translation.");
}

void SN2CTranslationViewer::Clear()
{
	CurrentTranslation = FN2CGraphTranslation();
	CurrentGraphName = FString();
	CurrentJsonContent = FString();
	CurrentLanguage = EN2CCodeLanguage::Cpp;
	ActiveFileType = TEXT("cpp");
	bHasTranslation = false;
	UpdateTabStyles();

	if (HeaderBar.IsValid())
	{
		HeaderBar->SetTitle(LOCTEXT("NoTranslation", "No Translation Loaded"));
	}

	if (NotesText.IsValid())
	{
		NotesText->SetText(LOCTEXT("NoNotes", "No implementation notes available."));
	}

	if (CodeEditor.IsValid())
	{
		CodeEditor->SetText(FText::GetEmpty());
	}
}

FReply SN2CTranslationViewer::HandleFileTabClicked(FString FileType)
{
	if (ActiveFileType != FileType)
	{
		ActiveFileType = FileType;
		UpdateTabStyles();
		UpdateCodeDisplay();
	}
	return FReply::Handled();
}

FReply SN2CTranslationViewer::HandleCloseClicked()
{
	OnCloseRequestedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply SN2CTranslationViewer::HandleCopyCodeClicked()
{
	FString Content = GetContentForActiveTab();
	if (!Content.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Content);
	}
	return FReply::Handled();
}

FReply SN2CTranslationViewer::HandleCopyNotesClicked()
{
	if (!CurrentTranslation.Code.ImplementationNotes.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*CurrentTranslation.Code.ImplementationNotes);
	}
	return FReply::Handled();
}

void SN2CTranslationViewer::UpdateCodeDisplay()
{
	if (!CodeEditor.IsValid())
	{
		return;
	}

	FString Content = GetContentForActiveTab();

	// Set appropriate language for syntax highlighting
	if (ActiveFileType == TEXT("json"))
	{
		// The N2C JSON the translation was made from, highlighted as JSON
		CodeEditor->SetJsonSyntax();
	}
	else if (ActiveFileType == TEXT("h"))
	{
		CodeEditor->SetLanguage(EN2CCodeLanguage::Cpp);
	}
	else
	{
		// The implementation, in the language it was written in
		CodeEditor->SetLanguage(CurrentLanguage);
	}

	CodeEditor->SetText(FText::FromString(Content));
}

void SN2CTranslationViewer::UpdateTabStyles()
{
	// Tab styles are handled via GetTabTextColor binding
	// Force a refresh by invalidating the widget
	if (CppTabButton.IsValid())
	{
		CppTabButton->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (HeaderTabButton.IsValid())
	{
		HeaderTabButton->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (JsonTabButton.IsValid())
	{
		JsonTabButton->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FString SN2CTranslationViewer::GetContentForActiveTab() const
{
	if (ActiveFileType == TEXT("cpp"))
	{
		return CurrentTranslation.Code.GraphImplementation;
	}
	else if (ActiveFileType == TEXT("h"))
	{
		return CurrentTranslation.Code.GraphDeclaration;
	}
	else if (ActiveFileType == TEXT("json"))
	{
		// Prettify the JSON for display (don't modify the stored content)
		if (!CurrentJsonContent.IsEmpty())
		{
			TSharedPtr<FJsonValue> JsonValue;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CurrentJsonContent);
			if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid())
			{
				FString PrettyJson;
				TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
					TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&PrettyJson);
				if (FJsonSerializer::Serialize(JsonValue, TEXT(""), Writer))
				{
					return PrettyJson;
				}
			}
		}
		// Fall back to raw content if prettifying fails
		return CurrentJsonContent;
	}
	return FString();
}

FText SN2CTranslationViewer::GetImplementationTabLabel() const
{
	return FText::FromString(N2C::GetLanguage(CurrentLanguage).ImplementationExtension);
}

FText SN2CTranslationViewer::GetDeclarationTabLabel() const
{
	// The tab shows only when a declaration was loaded, which only a language with a declaration file has
	const TCHAR* Extension = N2C::GetLanguage(CurrentLanguage).DeclarationExtension;
	return FText::FromString(*Extension ? Extension : TEXT(".h"));
}

EVisibility SN2CTranslationViewer::GetHeaderTabVisibility() const
{
	return CurrentTranslation.Code.GraphDeclaration.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

const FButtonStyle* SN2CTranslationViewer::GetTabButtonStyle(bool bIsActive) const
{
	return &N2CStyle::GetButtonStyle();
}

FSlateColor SN2CTranslationViewer::GetTabTextColor(FString FileType) const
{
	if (FileType == ActiveFileType)
	{
		return N2C::UI::Linear(N2C::UI::Colors().AccentOrange);
	}
	return N2C::UI::Linear(N2C::UI::Colors().TextSecondary);
}

FSlateColor SN2CTranslationViewer::GetTabBorderColor(FString FileType) const
{
	if (FileType == ActiveFileType)
	{
		return N2C::UI::Linear(N2C::UI::Colors().AccentOrange);
	}
	return N2C::UI::Linear(N2C::UI::Colors().BorderColor);
}

#undef LOCTEXT_NAMESPACE
