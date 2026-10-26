// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CGraphOverlay.h"
#include "Core/Widgets/SN2CCircularProgressBar.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CEditorWindow.h"
#include "Core/N2CTagManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Core/Style/N2CStyle.h"
#include "Utils/N2CLogger.h"
#include "BlueprintEditor.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "Framework/Docking/TabManager.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Core/Style/N2CUIColors.h"

#define LOCTEXT_NAMESPACE "SN2CGraphOverlay"

void SN2CGraphOverlay::Construct(const FArguments& InArgs)
{
	FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: ENTER"), EN2CLogSeverity::Warning);

	FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: Storing arguments"), EN2CLogSeverity::Warning);
	GraphGuid = InArgs._GraphGuid;
	BlueprintPath = InArgs._BlueprintPath;
	GraphName = InArgs._GraphName;
	BlueprintEditor = InArgs._BlueprintEditor;

	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphOverlay::Construct: GraphName=%s, BlueprintPath=%s"), *GraphName, *BlueprintPath), EN2CLogSeverity::Warning);
	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphOverlay::Construct: BlueprintEditor valid=%d"), BlueprintEditor.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);

	// Get initial tag count
	FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: Getting initial tag count"), EN2CLogSeverity::Warning);
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsForGraph(GraphGuid);
	CachedTagCount = Tags.Num();
	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphOverlay::Construct: CachedTagCount=%d"), CachedTagCount), EN2CLogSeverity::Warning);

	// Subscribe to tag manager events
	FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: Subscribing to tag manager events"), EN2CLogSeverity::Warning);
	OnTagAddedHandle = UN2CTagManager::Get().OnBlueprintTagAdded.AddRaw(this, &SN2CGraphOverlay::OnTagAdded);
	OnTagRemovedHandle = UN2CTagManager::Get().OnBlueprintTagRemoved.AddRaw(this, &SN2CGraphOverlay::OnTagRemoved);
	OnTagsReloadedHandle = UN2CTagManager::Get().OnTagsReloaded.AddRaw(this, &SN2CGraphOverlay::RefreshTagCount);
	FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: Tag event subscriptions done"), EN2CLogSeverity::Warning);

	// Subscribe to global translation state changes
	OnTranslationStateChangedHandle = FN2CEditorIntegration::Get().OnTranslationStateChanged.AddRaw(
		this, &SN2CGraphOverlay::OnGlobalTranslationStateChanged);

	// Initialize local state from global state
	bIsTranslating = FN2CEditorIntegration::Get().IsAnyTranslationInProgress();

	ChildSlot
	[
		SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Overlay)
		.OverlayAlpha(0.85f)
		.Padding(FMargin(FN2CSpacing::SM, FN2CSpacing::XS))
		[
			SNew(SHorizontalBox)

			// NodeToCode branding
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
			[
				SNew(SN2CIconButton)
				.IconBrush(N2CStyle::Get().GetBrush("NodeToCode.ToolbarButton"))
				.IconSize(FVector2D(FN2CSizing::IconLG, FN2CSizing::IconLG))
				.Text(LOCTEXT("N2CBranding", "N2C"))
				.Font(FN2CFonts::Small())
				.TextColor(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
				.ButtonVariant(EN2CButtonVariant::Simple)
				.ContentPadding(FMargin(FN2CSpacing::XXS))
				.ToolTipText(LOCTEXT("OpenWindowTooltip", "Open NodeToCode Window"))
				.OnClicked(this, &SN2CGraphOverlay::OnOpenWindowClicked)
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XXS, 0.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Thickness(FN2CSizing::SeparatorThickness)
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::BorderSubtle))
			]

			// Tag button with count badge
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XS, 0.0f, FN2CSpacing::XS, 0.0f)
			[
				SAssignNew(TagMenuAnchor, SMenuAnchor)
				.Placement(MenuPlacement_BelowAnchor)
				.OnGetMenuContent(this, &SN2CGraphOverlay::CreateTagPopoverContent)
				[
					SNew(SN2CIconButton)
					.IconBrush(FAppStyle::GetBrush("GraphEditor.Bookmark"))
					.IconColor(TAttribute<FSlateColor>::CreateSP(this, &SN2CGraphOverlay::GetTagButtonColor))
					.Text(TAttribute<FText>::CreateSP(this, &SN2CGraphOverlay::GetTagCountText))
					.Font(FN2CFonts::Small())
					.TextColor(TAttribute<FSlateColor>::CreateSP(this, &SN2CGraphOverlay::GetTagButtonColor))
					.ButtonVariant(EN2CButtonVariant::Simple)
					.ContentPadding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
					.ToolTipText(TAttribute<FText>::CreateSP(this, &SN2CGraphOverlay::GetTagButtonTooltip))
					.OnClicked(this, &SN2CGraphOverlay::OnTagButtonClicked)
				]
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XXS, 0.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Thickness(FN2CSizing::SeparatorThickness)
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::BorderSubtle))
			]

			// Copy JSON button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XS, 0.0f)
			[
				SNew(SN2CIconButton)
				.IconBrush(FAppStyle::GetBrush("Icons.Clipboard"))
				.ButtonVariant(EN2CButtonVariant::Simple)
				.ContentPadding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
				.ToolTipText(TAttribute<FText>::CreateSP(this, &SN2CGraphOverlay::GetCopyJsonTooltip))
				.OnClicked(this, &SN2CGraphOverlay::OnCopyJsonClicked)
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XXS, 0.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Thickness(FN2CSizing::SeparatorThickness)
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::BorderSubtle))
			]

			// Translate button (custom layout due to spinner)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XS, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&N2CStyle::GetSimpleButtonStyle())
				.ToolTipText(this, &SN2CGraphOverlay::GetTranslateTooltip)
				.OnClicked(this, &SN2CGraphOverlay::OnTranslateClicked)
				.IsEnabled_Lambda([this]() { return !bIsTranslating; })
				.ContentPadding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(FN2CSizing::IconMD)
						.HeightOverride(FN2CSizing::IconMD)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Convert"))
							.Visibility(this, &SN2CGraphOverlay::GetTranslateTextVisibility)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(TranslateSpinner, SThrobber)
						.Visibility(this, &SN2CGraphOverlay::GetSpinnerVisibility)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::XS, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TranslateButton", "Translate"))
						.Font(FN2CFonts::Small())
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
					]
				]
			]

			// Separator before context usage
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XS, 0.0f, FN2CSpacing::XXS, 0.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Thickness(FN2CSizing::SeparatorThickness)
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::BorderSubtle))
			]

			// Context usage circular progress with percentage overlay
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XS, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(32.0f)
				.HeightOverride(32.0f)
				.ToolTipText(this, &SN2CGraphOverlay::GetContextTooltipText)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SN2CCircularProgressBar)
						.Percent(this, &SN2CGraphOverlay::GetContextUsagePercent)
						.Radius(14.0f)
						.Thickness(3.0f)
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SN2CGraphOverlay::GetContextUsagePercentText)
						.Font(FN2CFonts::TinyBold())
						.Justification(ETextJustify::Center)
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
					]
				]
			]

			// Nesting depth indicator (only visible when nesting is enabled)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XXS, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([]() -> EVisibility
				{
					return FN2CTokenEstimationService::Get().IsNestedTranslationEnabled()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					SNew(SN2CPanel)
					.Variant(EN2CPanelVariant::Dark)
					.Padding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
					[
						SNew(SBox)
						.ToolTipText_Lambda([]() -> FText
						{
							int32 Depth = FN2CTokenEstimationService::Get().GetTranslationDepth();
							return FText::FromString(FString::Printf(
								TEXT("Nested Translation Depth: %d\n\nThis graph's token estimate includes\nreferenced user functions up to %d level(s) deep.\n\nThis can significantly increase costs.\nAdjust in Project Settings > Node to Code."),
								Depth, Depth
							));
						})
						[
							SNew(STextBlock)
							.Text_Lambda([]() -> FText
							{
								int32 Depth = FN2CTokenEstimationService::Get().GetTranslationDepth();
								return FText::FromString(FString::Printf(TEXT("N:%d"), Depth));
							})
							.Font(FN2CFonts::TinyBold())
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentOrange))
						]
					]
				]
			]
		]
	];

	// Subscribe to token estimation service events
	FN2CTokenEstimationService& TokenService = FN2CTokenEstimationService::Get();
	OnModelChangedHandle = TokenService.OnModelChanged.AddSP(this, &SN2CGraphOverlay::OnModelChanged);
	OnCacheInvalidatedHandle = TokenService.OnCacheInvalidated.AddSP(this, &SN2CGraphOverlay::OnCacheInvalidated);

	// Initial token estimate
	RefreshTokenEstimate();

	//FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: Widget hierarchy built successfully"), EN2CLogSeverity::Warning);
	//FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphOverlay::Construct: TagMenuAnchor valid=%d"), TagMenuAnchor.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);
	//FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphOverlay::Construct: TranslateSpinner valid=%d"), TranslateSpinner.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);
	//FN2CLogger::Get().Log(TEXT("SN2CGraphOverlay::Construct: EXIT"), EN2CLogSeverity::Warning);
}

SN2CGraphOverlay::~SN2CGraphOverlay()
{
	// Unsubscribe from tag manager events (it may already be shut down when the editor exits)
	if (UN2CTagManager* TagManager = UN2CTagManager::GetIfExists())
	{
		TagManager->OnBlueprintTagAdded.Remove(OnTagAddedHandle);
		TagManager->OnBlueprintTagRemoved.Remove(OnTagRemovedHandle);
		TagManager->OnTagsReloaded.Remove(OnTagsReloadedHandle);
	}
	// Unsubscribe from global translation state
	if (OnTranslationStateChangedHandle.IsValid())
	{
		FN2CEditorIntegration::Get().OnTranslationStateChanged.Remove(OnTranslationStateChangedHandle);
	}
	// Unsubscribe from token estimation service
	if (OnModelChangedHandle.IsValid())
	{
		FN2CTokenEstimationService::Get().OnModelChanged.Remove(OnModelChangedHandle);
	}
	if (OnCacheInvalidatedHandle.IsValid())
	{
		FN2CTokenEstimationService::Get().OnCacheInvalidated.Remove(OnCacheInvalidatedHandle);
	}
}

void SN2CGraphOverlay::RefreshTagCount()
{
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsForGraph(GraphGuid);
	CachedTagCount = Tags.Num();
}

void SN2CGraphOverlay::SetTranslating(bool bInProgress)
{
	bIsTranslating = bInProgress;
}

FReply SN2CGraphOverlay::OnCopyJsonClicked()
{
	// Ensure the editor integration has the correct active editor
	// Use our stored BlueprintEditor reference which is guaranteed to be the correct one
	if (BlueprintEditor.IsValid())
	{
		FN2CEditorIntegration::Get().StoreActiveBlueprintEditor(BlueprintEditor);
	}
	else
	{
		FN2CLogger::Get().Log(TEXT("Blueprint editor reference is invalid"),
			EN2CLogSeverity::Warning, TEXT("SN2CGraphOverlay"));
		return FReply::Handled();
	}

	FString ErrorMsg;
	FString JsonString = FN2CEditorIntegration::Get().GetFocusedBlueprintAsJson(true, ErrorMsg);

	if (!JsonString.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*JsonString);
		FN2CLogger::Get().Log(TEXT("Blueprint graph JSON copied to clipboard"),
			EN2CLogSeverity::Info, TEXT("SN2CGraphOverlay"));
	}
	else
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Failed to copy JSON: %s"), *ErrorMsg),
			EN2CLogSeverity::Warning, TEXT("SN2CGraphOverlay"));
	}

	return FReply::Handled();
}

FReply SN2CGraphOverlay::OnTranslateClicked()
{
	// Check global translation state
	if (FN2CEditorIntegration::Get().IsAnyTranslationInProgress())
	{
		FN2CLogger::Get().Log(TEXT("Translation already in progress globally"),
			EN2CLogSeverity::Warning, TEXT("SN2CGraphOverlay"));
		return FReply::Handled();
	}

	// Ensure the editor integration has the correct active editor
	if (BlueprintEditor.IsValid())
	{
		FN2CEditorIntegration::Get().StoreActiveBlueprintEditor(BlueprintEditor);
	}
	else
	{
		FN2CLogger::Get().Log(TEXT("Blueprint editor reference is invalid"),
			EN2CLogSeverity::Warning, TEXT("SN2CGraphOverlay"));
		return FReply::Handled();
	}

	// Request translation through the central system
	// This broadcasts to the main window which will show the progress modal
	FN2CEditorIntegration::Get().RequestOverlayTranslation(GraphGuid, GraphName, BlueprintPath);

	return FReply::Handled();
}

FReply SN2CGraphOverlay::OnTagButtonClicked()
{
	if (TagMenuAnchor.IsValid())
	{
		TagMenuAnchor->SetIsOpen(!TagMenuAnchor->IsOpen());
	}
	return FReply::Handled();
}

FReply SN2CGraphOverlay::OnOpenWindowClicked()
{
	// Open the main NodeToCode window using the global tab manager
	FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);

	FN2CLogger::Get().Log(TEXT("NodeToCode window opened from graph overlay"),
		EN2CLogSeverity::Info, TEXT("SN2CGraphOverlay"));

	return FReply::Handled();
}

TSharedRef<SWidget> SN2CGraphOverlay::CreateTagPopoverContent()
{
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsForGraph(GraphGuid);

	TSharedRef<SVerticalBox> TagList = SNew(SVerticalBox);

	// Header
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, FN2CSpacing::SM)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("TagsForGraph", "Tags for \"{0}\""), FText::FromString(GraphName)))
		.Font(FN2CFonts::RegularBold())
		.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
	];

	// Separator
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, 0.0f)
	[
		SNew(SSeparator)
		.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::BorderSubtle))
	];

	// Tag list
	if (Tags.Num() > 0)
	{
		TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);

		for (const FN2CTaggedBlueprintGraph& TagItem : Tags)
		{
			ScrollBox->AddSlot()
			.Padding(FN2CSpacing::MD, FN2CSpacing::XS)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\u2022"))) // Bullet character
					.Font(FN2CFonts::Small())
					.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(FN2CSpacing::XS, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("TagDisplay", "{0} ({1})"),
						FText::FromString(TagItem.Tag), FText::FromString(TagItem.Category)))
					.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&N2CStyle::GetSimpleButtonStyle())
					.ToolTipText(LOCTEXT("RemoveTagTooltip", "Remove this tag"))
					.OnClicked_Lambda([this, TagItem]()
					{
						OnRemoveTagRequested(TagItem);
						return FReply::Handled();
					})
					.ContentPadding(FMargin(FN2CSpacing::XXS))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.X"))
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentRed))
					]
				]
			];
		}

		TagList->AddSlot()
		.AutoHeight()
		.MaxHeight(150.0f)
		[
			ScrollBox
		];
	}
	else
	{
		TagList->AddSlot()
		.AutoHeight()
		.Padding(FN2CSpacing::MD)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoTags", "No tags applied"))
			.Font(FN2CFonts::Regular())
			.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
		];
	}

	// Separator
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, FN2CSpacing::XS)
	[
		SNew(SSeparator)
		.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::BorderSubtle))
	];

	// Add new tag section header
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, FN2CSpacing::XS, FN2CSpacing::MD, FN2CSpacing::XXS)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AddNewTagHeader", "Add New Tag"))
		.Font(FN2CFonts::Small())
		.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
	];

	// Get existing tags and categories for the dropdowns
	TArray<FString> ExistingTags = UN2CTagManager::Get().GetAllTagNames();
	TArray<FString> ExistingCategories = UN2CTagManager::Get().GetAllCategories();

	// Ensure "Default" is always an option for categories
	if (!ExistingCategories.Contains(TEXT("Default")))
	{
		ExistingCategories.Insert(TEXT("Default"), 0);
	}

	// Populate member arrays for combo box options (must persist beyond this function)
	TagOptions.Empty();
	for (const FString& TagName : ExistingTags)
	{
		TagOptions.Add(MakeShared<FString>(TagName));
	}

	CategoryOptions.Empty();
	for (const FString& Category : ExistingCategories)
	{
		CategoryOptions.Add(MakeShared<FString>(Category));
	}

	// Add tag input fields
	TSharedPtr<SEditableTextBox> TagInputBox;
	TSharedPtr<SEditableTextBox> CategoryInputBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> TagComboBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CategoryComboBox;

	// Tag name row with combo box and text input
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, FN2CSpacing::XXS, FN2CSpacing::MD, FN2CSpacing::XXS)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(60.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TagLabel", "Tag:"))
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Editable text box for tag
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(TagInputBox, SEditableTextBox)
				.Style(&N2CStyle::GetEditableTextBoxStyle())
				.HintText(LOCTEXT("TagNameHint", "Enter or select..."))
			]
			// Dropdown button for existing tags (only show if there are existing tags)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FN2CSpacing::XXS, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility(TagOptions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SAssignNew(TagComboBox, SComboBox<TSharedPtr<FString>>)
					.ComboBoxStyle(&N2CStyle::GetComboBoxStyle())
					.ItemStyle(&N2CStyle::GetComboRowStyle())
					.OptionsSource(&TagOptions)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
					{
						return SNew(STextBlock).Text(FText::FromString(*Item));
					})
					.OnSelectionChanged_Lambda([TagInputBox](TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
					{
						if (SelectedItem.IsValid() && TagInputBox.IsValid())
						{
							TagInputBox->SetText(FText::FromString(*SelectedItem));
						}
					})
					[
						SNew(STextBlock)
						.Text(FText::GetEmpty())
					]
				]
			]
		]
	];

	// Category row with combo box and text input
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, FN2CSpacing::XXS, FN2CSpacing::MD, FN2CSpacing::XS)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(60.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CategoryLabel", "Category:"))
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Editable text box for category
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(CategoryInputBox, SEditableTextBox)
				.Style(&N2CStyle::GetEditableTextBoxStyle())
				.HintText(LOCTEXT("CategoryHint", "Enter or select..."))
				.Text(FText::FromString(TEXT("Default")))
			]
			// Dropdown button for existing categories
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FN2CSpacing::XXS, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(CategoryComboBox, SComboBox<TSharedPtr<FString>>)
				.ComboBoxStyle(&N2CStyle::GetComboBoxStyle())
				.ItemStyle(&N2CStyle::GetComboRowStyle())
				.OptionsSource(&CategoryOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(*Item));
				})
				.OnSelectionChanged_Lambda([CategoryInputBox](TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
				{
					if (SelectedItem.IsValid() && CategoryInputBox.IsValid())
					{
						CategoryInputBox->SetText(FText::FromString(*SelectedItem));
					}
				})
				[
					SNew(STextBlock)
					.Text(FText::GetEmpty())
				]
			]
		]
	];

	// Add button
	TagList->AddSlot()
	.AutoHeight()
	.Padding(FN2CSpacing::MD, FN2CSpacing::XS, FN2CSpacing::MD, FN2CSpacing::MD)
	[
		SNew(SButton)
		.ButtonStyle(&N2CStyle::GetButtonStyle())
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([this, TagInputBox, CategoryInputBox]()
		{
			if (TagInputBox.IsValid() && CategoryInputBox.IsValid())
			{
				FString TagName = TagInputBox->GetText().ToString();
				FString CategoryName = CategoryInputBox->GetText().ToString();

				if (TagName.IsEmpty())
				{
					return FReply::Handled();
				}

				if (CategoryName.IsEmpty())
				{
					CategoryName = TEXT("Default");
				}

				OnAddTagRequested(TagName, CategoryName);
			}
			return FReply::Handled();
		})
		.ContentPadding(FMargin(FN2CSpacing::MD, FN2CSpacing::XS))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Plus"))
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentGreen))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::XS, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddTagButton", "Add Tag"))
			]
		]
	];

	return SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Light)
		.Padding(0.0f)
		[
			SNew(SBox)
			.MinDesiredWidth(280.0f)
			[
				TagList
			]
		];
}

void SN2CGraphOverlay::OnAddTagRequested(const FString& TagName, const FString& CategoryName)
{
	FN2CTaggedBlueprintGraph NewTag(
		TagName,
		CategoryName,
		TEXT(""), // Description
		GraphGuid,
		GraphName,
		FSoftObjectPath(BlueprintPath)
	);

	if (UN2CTagManager::Get().AddTag(NewTag))
	{
		RefreshTagCount();
		// Refresh the popover by toggling it
		if (TagMenuAnchor.IsValid() && TagMenuAnchor->IsOpen())
		{
			TagMenuAnchor->SetIsOpen(false);
			TagMenuAnchor->SetIsOpen(true);
		}
	}
}

void SN2CGraphOverlay::OnRemoveTagRequested(const FN2CTaggedBlueprintGraph& TagInfo)
{
	if (UN2CTagManager::Get().RemoveTag(TagInfo.GraphGuid, TagInfo.Tag, TagInfo.Category))
	{
		RefreshTagCount();
		// Refresh the popover by toggling it
		if (TagMenuAnchor.IsValid() && TagMenuAnchor->IsOpen())
		{
			TagMenuAnchor->SetIsOpen(false);
			TagMenuAnchor->SetIsOpen(true);
		}
	}
}

void SN2CGraphOverlay::OnTagAdded(const FN2CTaggedBlueprintGraph& TagInfo)
{
	if (TagInfo.GraphGuid == GraphGuid)
	{
		RefreshTagCount();
	}
}

void SN2CGraphOverlay::OnTagRemoved(const FGuid& RemovedGraphGuid, const FString& RemovedTag)
{
	if (RemovedGraphGuid == GraphGuid)
	{
		RefreshTagCount();
	}
}

void SN2CGraphOverlay::OnGlobalTranslationStateChanged(bool bInProgress)
{
	// Update local state from global state
	// This ensures all overlays are synchronized
	bIsTranslating = bInProgress;
}

EVisibility SN2CGraphOverlay::GetSpinnerVisibility() const
{
	return bIsTranslating ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SN2CGraphOverlay::GetTranslateTextVisibility() const
{
	return bIsTranslating ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SN2CGraphOverlay::GetTagCountText() const
{
	return FText::AsNumber(CachedTagCount);
}

FText SN2CGraphOverlay::GetCopyJsonTooltip() const
{
	return LOCTEXT("CopyJsonTooltip", "Copy Blueprint graph JSON to clipboard");
}

FText SN2CGraphOverlay::GetTranslateTooltip() const
{
	if (bIsTranslating)
	{
		return LOCTEXT("TranslatingTooltip", "Translation in progress...");
	}
	return LOCTEXT("TranslateTooltip", "Translate this graph using the configured LLM");
}

FText SN2CGraphOverlay::GetTagButtonTooltip() const
{
	if (CachedTagCount == 0)
	{
		return LOCTEXT("NoTagsTooltip", "No tags - click to add");
	}
	return FText::Format(LOCTEXT("TagCountTooltip", "{0} tag(s) - click to manage"), FText::AsNumber(CachedTagCount));
}

FSlateColor SN2CGraphOverlay::GetTagButtonColor() const
{
	if (CachedTagCount > 0)
	{
		return FSlateColor(N2C::UI::Linear(N2C::UI::Colors().AccentGold)); // Gold/amber for tagged
	}
	return FSlateColor(N2C::UI::Linear(N2C::UI::Colors().TextMuted)); // Gray for untagged
}

void SN2CGraphOverlay::RefreshTokenEstimate()
{
	FN2CTagInfo GraphInfo;
	GraphInfo.GraphGuid = GraphGuid.ToString();
	GraphInfo.BlueprintPath = BlueprintPath;
	GraphInfo.GraphName = GraphName;

	FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();

	// Use nesting-aware estimation if enabled
	CachedTokenCount = Service.GetTokenEstimateWithNesting(GraphInfo, CachedNestedGraphCount);

	int32 UsableContext = Service.GetUsableContextWindow();
	CachedContextUsagePercent = UsableContext > 0
		? FMath::Clamp(static_cast<float>(CachedTokenCount) / UsableContext, 0.0f, 1.0f)
		: 0.0f;

	CachedEstimatedCost = Service.CalculateCost(CachedTokenCount);
}

void SN2CGraphOverlay::OnModelChanged()
{
	RefreshTokenEstimate();
}

void SN2CGraphOverlay::OnCacheInvalidated()
{
	RefreshTokenEstimate();
}

float SN2CGraphOverlay::GetContextUsagePercent() const
{
	return CachedContextUsagePercent;
}

FText SN2CGraphOverlay::GetContextUsagePercentText() const
{
	int32 PercentInt = FMath::RoundToInt(CachedContextUsagePercent * 100.0f);
	return FText::FromString(FString::Printf(TEXT("%d%%"), PercentInt));
}

FText SN2CGraphOverlay::GetContextTooltipText() const
{
	FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();

	FString TooltipStr = FString::Printf(
		TEXT("Tokens: %s\nEstimated cost: %s"),
		*FormatNumber(CachedTokenCount),
		Service.IsLocalProvider() ? TEXT("Free (local)") : *FString::Printf(TEXT("$%.4f"), CachedEstimatedCost)
	);

	// Show nesting info if enabled
	if (Service.IsNestedTranslationEnabled())
	{
		int32 Depth = Service.GetTranslationDepth();
		TooltipStr += FString::Printf(
			TEXT("\n\nNested Translation: ON (depth %d)"),
			Depth
		);
		if (CachedNestedGraphCount > 0)
		{
			TooltipStr += FString::Printf(TEXT("\nIncludes %d nested graph(s)"), CachedNestedGraphCount);
		}
		TooltipStr += TEXT("\n\n⚠ Nested translation includes referenced\nfunctions, increasing token count and cost.");
	}

	// Add warning if > 45% usage
	if (CachedContextUsagePercent > 0.45f)
	{
		TooltipStr += TEXT("\n\n⚠ High context usage. Consider refactoring\nthis graph into smaller, reusable functions.");
	}

	return FText::FromString(TooltipStr);
}

FString SN2CGraphOverlay::FormatNumber(int64 Number)
{
	FString Result = FString::Printf(TEXT("%lld"), Number);
	int32 InsertPosition = Result.Len() - 3;

	while (InsertPosition > 0)
	{
		Result.InsertAt(InsertPosition, TEXT(","));
		InsertPosition -= 3;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
