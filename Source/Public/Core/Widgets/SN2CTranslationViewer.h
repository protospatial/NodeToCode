// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Models/N2CTranslation.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

class SN2CCodeEditor;
class SN2CHeaderBar;
class SExpandableArea;

/**
 * Translation viewer overlay widget
 * Displays translated code with file tabs (the implementation file, labelled with its language's extension; .h when
 * the translation has a declaration; JSON), syntax highlighting in the translation's language, and notes
 */
class NODETOCODE_API SN2CTranslationViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CTranslationViewer) {}
		/** Called when the close button is clicked */
		SLATE_EVENT(FSimpleDelegate, OnCloseRequested)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/**
	 * Load translation for a specific graph from the state manager, in the language recorded for it
	 * @param GraphInfo The tag info for the graph to load translation for
	 * @return True if translation was loaded successfully
	 */
	bool LoadTranslation(const FN2CTagInfo& GraphInfo);

	/**
	 * Load translation directly from a translation object, in the Target Language from the plugin settings (what a
	 * single translate just used)
	 * @param Translation The translation data
	 * @param GraphName The name of the graph for display
	 * @param JsonContent Optional JSON content to display in the JSON tab
	 */
	void LoadTranslation(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent = TEXT(""));

	/**
	 * Load translation directly from a translation object written in Language: the implementation tab is labelled
	 * with the language's extension and highlighted in it, the .h tab shows only when there is a declaration, and the
	 * viewer opens on the implementation
	 */
	void LoadTranslation(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent,
		EN2CCodeLanguage Language);

	/**
	 * Set only the JSON content (for JSON-only viewing)
	 * @param JsonContent The JSON content to display
	 * @param GraphName The name of the graph for display
	 */
	void SetJsonContent(const FString& JsonContent, const FString& GraphName);

	/** Clear the viewer */
	void Clear();

	/** Get the current graph name */
	FString GetCurrentGraphName() const { return CurrentGraphName; }

	/** Check if translation is currently loaded */
	bool HasTranslation() const { return bHasTranslation; }

private:
	/** Delegate for close button */
	FSimpleDelegate OnCloseRequestedDelegate;

	// Header widgets
	TSharedPtr<SN2CHeaderBar> HeaderBar;

	// File tab buttons
	TSharedPtr<SButton> CppTabButton;
	TSharedPtr<SButton> HeaderTabButton;
	TSharedPtr<SButton> JsonTabButton;

	// Code viewer
	TSharedPtr<SN2CCodeEditor> CodeEditor;

	// Notes section
	TSharedPtr<SExpandableArea> NotesSection;
	TSharedPtr<STextBlock> NotesText;

	// Current state
	FN2CGraphTranslation CurrentTranslation;
	FString CurrentGraphName;
	FString CurrentJsonContent;
	FString ActiveFileType; // "cpp" (the implementation, any language), "h", or "json"
	EN2CCodeLanguage CurrentLanguage;
	bool bHasTranslation;

	/** Create a file tab button */
	TSharedRef<SWidget> CreateFileTab(const TAttribute<FText>& Label, const FString& FileType, TSharedPtr<SButton>& OutButton);

	/** The implementation tab's label: the current language's file extension (.cpp, .py, .js, .cs, .swift, .md) */
	FText GetImplementationTabLabel() const;

	/** The declaration tab's label: the language's declaration extension from the target-language table (.h) */
	FText GetDeclarationTabLabel() const;

	/** The .h tab shows only when the translation has a declaration */
	EVisibility GetHeaderTabVisibility() const;

	/** Handle file tab clicked */
	FReply HandleFileTabClicked(FString FileType);

	/** Handle close button clicked */
	FReply HandleCloseClicked();

	/** Handle copy code button clicked */
	FReply HandleCopyCodeClicked();

	/** Handle copy notes button clicked */
	FReply HandleCopyNotesClicked();

	/** Update the code display based on active tab */
	void UpdateCodeDisplay();

	/** Update tab button styles */
	void UpdateTabStyles();

	/** Get the appropriate content for the current tab */
	FString GetContentForActiveTab() const;

	/** Get button style for tab (active vs inactive) */
	const FButtonStyle* GetTabButtonStyle(bool bIsActive) const;

	/** Get the text color for a tab */
	FSlateColor GetTabTextColor(FString FileType) const;

	/** Get the border color for a tab */
	FSlateColor GetTabBorderColor(FString FileType) const;
};
