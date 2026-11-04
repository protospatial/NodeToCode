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
#include "LLM/N2CTranslationArchive.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Dom/JsonObject.h"

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
					// Edit toggle (card 0257): the code is read-only until it is on
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, FN2CSpacing::SM, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&N2CStyle::GetButtonStyle())
						.ContentPadding(FMargin(FN2CSpacing::MD, FN2CSpacing::XS))
						.IsEnabled_Lambda([this]() { return bHasTranslation && ActiveFileType != TEXT("json"); })
						.OnClicked_Lambda([this]() { SetEditing(!bEditing); return FReply::Handled(); })
						.ToolTipText(LOCTEXT("EditTooltip", "Edit the code here. Edits stay in the viewer (Copy copies them) until another translation is loaded; the saved files do not change."))
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return bEditing ? LOCTEXT("EditOn", "Editing") : LOCTEXT("EditOff", "Edit"); })
							.Font(FN2CFonts::Medium())
							.ColorAndOpacity_Lambda([this]()
							{
								return bEditing ? N2C::UI::Linear(N2C::UI::Colors().AccentOrange) : N2C::UI::Linear(N2C::UI::Colors().TextSecondary);
							})
						]
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

			// Code viewer, with the translation's graphs listed beside it when there is more than one (card 0257)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(200.0f)
					.Visibility_Lambda([this]() { return Entries.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SN2CPanel)
						.Variant(EN2CPanelVariant::Light)
						.Padding(FMargin(0.0f, FN2CSpacing::SM))
						[
							SAssignNew(EntryList, SListView<TSharedPtr<int32>>)
							.ListItemsSource(&EntryItems)
							.SelectionMode(ESelectionMode::Single)
							.OnGenerateRow(this, &SN2CTranslationViewer::GenerateEntryRow)
							.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
							{
								if (Item.IsValid() && SelectInfo != ESelectInfo::Direct)
								{
									SelectEntry(*Item);
								}
							})
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
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

	// Read-only until Edit (card 0257)
	if (CodeEditor.IsValid())
	{
		CodeEditor->SetReadOnly(true);
	}

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

		// The whole run (every graph of the response) through its manifest, else the recorded graph alone
		const FN2CGraphState* State = UN2CGraphStateManager::Get().FindGraphState(Key);
		const FString Manifest = State && !State->Translation.ManifestPath.IsEmpty()
			? FN2CPathUtils::ResolveTranslationRecordPath(State->Translation.ManifestPath) : FString();
		if (Manifest.IsEmpty() || !LoadRunManifest(Manifest, GraphInfo.GraphGuid, GraphInfo.GraphName, JsonContent))
		{
			LoadTranslation(Translation, GraphInfo.GraphName, JsonContent, Language);
		}

		// How Verify and Refine ended for it, when it ran (Verify.json beside the entry folder)
		FN2CVerifyOutcome Verify;
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
	TArray<FN2CViewerEntry> One;
	FN2CViewerEntry& Entry = One.AddDefaulted_GetRef();
	Entry.Translation = Translation;
	Entry.Role = TEXT("root");
	SetEntries(MoveTemp(One), GraphName, JsonContent, Language);
}

void SN2CTranslationViewer::LoadResponse(const FN2CTranslationResponse& Response, const FString& GraphName, const FString& JsonContent,
	EN2CCodeLanguage Language, int32 RootIndex)
{
	const int32 Root = Response.Graphs.IsValidIndex(RootIndex) ? RootIndex : 0;
	TArray<FN2CViewerEntry> List;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Index = 0; Index < Response.Graphs.Num(); ++Index)
		{
			if ((Pass == 0) != (Index == Root))
			{
				continue;
			}
			FN2CViewerEntry& Entry = List.AddDefaulted_GetRef();
			Entry.Translation = Response.Graphs[Index];
			Entry.Role = Index == Root ? FString(TEXT("root")) : FN2CTranslationArchive::GetEntryRole(Entry.Translation.GraphType);
		}
	}
	SetEntries(MoveTemp(List), GraphName, JsonContent, Language);
}

bool SN2CTranslationViewer::LoadRunManifest(const FString& ManifestPath, const FString& GraphGuid, const FString& GraphName,
	const FString& JsonContent)
{
	FString Text;
	TSharedPtr<FJsonObject> Manifest;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest) || !Manifest.IsValid())
	{
		return false;
	}
	const FString RunFolder = FPaths::GetPath(ManifestPath);
	auto ReadFile = [&RunFolder](const TSharedPtr<FJsonObject>& Files, const TCHAR* Field)
	{
		FString Relative;
		FString Content;
		if (Files.IsValid() && Files->TryGetStringField(Field, Relative) && !Relative.IsEmpty())
		{
			const FString Path = FPaths::IsRelative(Relative) ? FPaths::Combine(RunFolder, Relative) : Relative;
			FFileHelper::LoadFileToString(Content, *Path);
		}
		return Content;
	};

	// The item of this graph (a batch run has one per graph)
	int32 Item = 0;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Manifest->TryGetArrayField(TEXT("items"), Items) && Items->Num() > 1)
	{
		Item = INDEX_NONE;
		FGuid Wanted;
		FGuid::Parse(GraphGuid, Wanted);
		for (int32 Index = 0; Index < Items->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> ItemObject = (*Items)[Index]->AsObject();
			FString Guid;
			FGuid Parsed;
			if (ItemObject.IsValid() && ItemObject->TryGetStringField(TEXT("graphGuid"), Guid)
				&& (Guid.Equals(GraphGuid, ESearchCase::IgnoreCase) || (FGuid::Parse(Guid, Parsed) && Parsed.IsValid() && Parsed == Wanted)))
			{
				Item = Index;
				break;
			}
		}
		if (Item == INDEX_NONE)
		{
			return false;
		}
	}

	TArray<FN2CViewerEntry> Roots;
	TArray<FN2CViewerEntry> Others;
	const TArray<TSharedPtr<FJsonValue>>* EntryValues = nullptr;
	if (Manifest->TryGetArrayField(TEXT("entries"), EntryValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *EntryValues)
		{
			const TSharedPtr<FJsonObject> EntryObject = Value.IsValid() ? Value->AsObject() : nullptr;
			int32 EntryItem = 0;
			if (!EntryObject.IsValid() || (EntryObject->TryGetNumberField(TEXT("item"), EntryItem) && EntryItem != Item))
			{
				continue;
			}
			FN2CViewerEntry Entry;
			EntryObject->TryGetStringField(TEXT("graphName"), Entry.Translation.GraphName);
			EntryObject->TryGetStringField(TEXT("graphType"), Entry.Translation.GraphType);
			EntryObject->TryGetStringField(TEXT("role"), Entry.Role);
			const TSharedPtr<FJsonObject>* Files = nullptr;
			const TSharedPtr<FJsonObject> FilesObject = EntryObject->TryGetObjectField(TEXT("files"), Files) ? *Files : nullptr;
			Entry.Translation.Code.GraphDeclaration = ReadFile(FilesObject, TEXT("declaration"));
			Entry.Translation.Code.GraphImplementation = ReadFile(FilesObject, TEXT("implementation"));
			Entry.Translation.Code.ImplementationNotes = ReadFile(FilesObject, TEXT("notes"));
			(Entry.Role == TEXT("root") ? Roots : Others).Add(MoveTemp(Entry));
		}
	}
	Roots.Append(MoveTemp(Others));
	if (Roots.Num() == 0)
	{
		return false;
	}

	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
	FString LanguageId;
	if (Manifest->TryGetStringField(TEXT("language"), LanguageId))
	{
		FN2CPathUtils::ParseLanguageId(LanguageId, Language);
	}
	SetEntries(MoveTemp(Roots), GraphName, JsonContent, Language);
	return true;
}

void SN2CTranslationViewer::SetEntries(TArray<FN2CViewerEntry>&& InEntries, const FString& GraphName, const FString& JsonContent,
	EN2CCodeLanguage Language)
{
	Entries = MoveTemp(InEntries);
	EntryItems.Reset();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		EntryItems.Add(MakeShared<int32>(Index));
	}
	if (EntryList.IsValid())
	{
		EntryList->RequestListRefresh();
	}
	WorkingCopies.Reset();
	bEditing = false;
	if (CodeEditor.IsValid())
	{
		CodeEditor->SetReadOnly(true);
	}
	SelectedEntry = 0;
	const FN2CGraphTranslation Translation = Entries.Num() > 0 ? Entries[0].Translation : FN2CGraphTranslation();
	if (EntryList.IsValid() && EntryItems.Num() > 0)
	{
		EntryList->SetSelection(EntryItems[0], ESelectInfo::Direct);
	}

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

void SN2CTranslationViewer::SelectEntry(int32 Index)
{
	if (!Entries.IsValidIndex(Index))
	{
		return;
	}
	StashWorkingCopy();
	SelectedEntry = Index;
	CurrentTranslation = Entries[Index].Translation;
	if (EntryList.IsValid() && EntryItems.IsValidIndex(Index) && !EntryList->IsItemSelected(EntryItems[Index]))
	{
		EntryList->SetSelection(EntryItems[Index], ESelectInfo::Direct);
	}
	if (NotesText.IsValid())
	{
		const FString& Notes = CurrentTranslation.Code.ImplementationNotes;
		NotesText->SetText(Notes.IsEmpty() ? LOCTEXT("NoNotes", "No implementation notes available.") : FText::FromString(Notes));
	}
	// An entry without the open tab's file (a struct has no implementation) opens on what it has
	if (ActiveFileType == TEXT("h") && CurrentTranslation.Code.GraphDeclaration.IsEmpty())
	{
		ActiveFileType = TEXT("cpp");
	}
	else if (ActiveFileType == TEXT("cpp") && CurrentTranslation.Code.GraphImplementation.IsEmpty() && !CurrentTranslation.Code.GraphDeclaration.IsEmpty())
	{
		ActiveFileType = TEXT("h");
	}
	UpdateTabStyles();
	UpdateCodeDisplay();
}

FString SN2CTranslationViewer::GetEntryLabel(const FN2CViewerEntry& Entry)
{
	const FString Name = Entry.Translation.GraphName.IsEmpty() ? FString(TEXT("(unnamed)")) : Entry.Translation.GraphName;
	return Entry.Role.IsEmpty() || Entry.Role == TEXT("root") ? Name : FString::Printf(TEXT("%s (%s)"), *Name, *Entry.Role);
}

TSharedRef<ITableRow> SN2CTranslationViewer::GenerateEntryRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;
	return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
		.Padding(FMargin(FN2CSpacing::MD, FN2CSpacing::XS))
		.ToolTipText_Lambda([this, Index]() { return Entries.IsValidIndex(Index) ? FText::FromString(Entries[Index].Translation.GraphType) : FText::GetEmpty(); })
		[
			SNew(STextBlock)
			.Text_Lambda([this, Index]() { return Entries.IsValidIndex(Index) ? FText::FromString(GetEntryLabel(Entries[Index])) : FText::GetEmpty(); })
			.Font(FN2CFonts::MonoSmall())
			.ColorAndOpacity_Lambda([this, Index]()
			{
				return Index == SelectedEntry ? N2C::UI::Linear(N2C::UI::Colors().TextPrimary) : N2C::UI::Linear(N2C::UI::Colors().TextSecondary);
			})
		];
}

void SN2CTranslationViewer::SetEditing(bool bInEditing)
{
	if (bInEditing == bEditing)
	{
		return;
	}
	if (!bInEditing)
	{
		// Keep what was typed: Copy and the tabs still show it
		StashWorkingCopy();
	}
	bEditing = bInEditing;
	if (CodeEditor.IsValid())
	{
		CodeEditor->SetReadOnly(!bEditing || ActiveFileType == TEXT("json"));
	}
}

void SN2CTranslationViewer::StashWorkingCopy()
{
	if (!bEditing || !CodeEditor.IsValid() || ActiveFileType == TEXT("json") || !bHasTranslation)
	{
		return;
	}
	const FString Text = CodeEditor->GetText().ToString();
	if (Text != GetContentForActiveTab())
	{
		WorkingCopies.Add(WorkingCopyKey(ActiveFileType), Text);
	}
}

void SN2CTranslationViewer::SelectTab(const FString& FileType)
{
	HandleFileTabClicked(FileType);
}

FString SN2CTranslationViewer::GetCopyText() const
{
	if (bEditing && CodeEditor.IsValid() && ActiveFileType != TEXT("json"))
	{
		return CodeEditor->GetText().ToString();
	}
	// The tab's working copy once it was edited
	if (const FString* Edited = ActiveFileType != TEXT("json") ? WorkingCopies.Find(WorkingCopyKey(ActiveFileType)) : nullptr)
	{
		return *Edited;
	}
	return GetContentForActiveTab();
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
	Entries.Reset();
	EntryItems.Reset();
	if (EntryList.IsValid())
	{
		EntryList->RequestListRefresh();
	}
	SelectedEntry = 0;
	WorkingCopies.Reset();
	SetEditing(false);
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
	Entries.Reset();
	EntryItems.Reset();
	if (EntryList.IsValid())
	{
		EntryList->RequestListRefresh();
	}
	SelectedEntry = 0;
	WorkingCopies.Reset();
	bEditing = false;
	if (CodeEditor.IsValid())
	{
		CodeEditor->SetReadOnly(true);
	}
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
		// The tab's edits stay as its working copy
		StashWorkingCopy();
		ActiveFileType = FileType;
		if (CodeEditor.IsValid())
		{
			CodeEditor->SetReadOnly(!bEditing || ActiveFileType == TEXT("json"));
		}
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
	// While editing, what is in the editor (card 0257)
	FString Content = GetCopyText();
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

	// The tab's working copy when it was edited (card 0257)
	const FString* Edited = ActiveFileType != TEXT("json") ? WorkingCopies.Find(WorkingCopyKey(ActiveFileType)) : nullptr;
	FString Content = Edited ? *Edited : GetContentForActiveTab();

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
