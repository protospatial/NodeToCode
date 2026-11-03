// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CTagManager.h"
#include "Core/N2CUserSettings.h"
#include "Core/Widgets/SN2CTagCategoryTree.h"
#include "Utils/N2CPathUtils.h"
#include "Core/Widgets/SN2CTaggedGraphsList.h"
#include "Core/Widgets/SN2CContextWindowVisualizer.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CDependencyActions.h"
#include "Utils/N2CLogger.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/MessageDialog.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Style/N2CStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "DesktopPlatformModule.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SN2CTagManager"

void SN2CTagManager::Construct(const FArguments& InArgs)
{
	OnTagSelectedDelegate = InArgs._OnTagSelected;
	OnCategorySelectedDelegate = InArgs._OnCategorySelected;
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	OnGraphDoubleClickedDelegate = InArgs._OnGraphDoubleClicked;
	OnBatchTranslateRequestedDelegate = InArgs._OnBatchTranslateRequested;
	OnExportJsonRequestedDelegate = InArgs._OnExportJsonRequested;
	OnRemoveSelectedRequestedDelegate = InArgs._OnRemoveSelectedRequested;
	OnSingleTranslateRequestedDelegate = InArgs._OnSingleTranslateRequested;
	OnSingleJsonExportRequestedDelegate = InArgs._OnSingleJsonExportRequested;
	OnViewTranslationRequestedDelegate = InArgs._OnViewTranslationRequested;

	bMinifyJson = InArgs._MinifyJsonByDefault;
	TagSource = InArgs._TagSource;

	// The translation output directory, as the translation archive resolves it
	OutputPath = FN2CPathUtils::GetTranslationsBasePath();

	ChildSlot
	[
		SNew(SVerticalBox)
		// Context Window Visualizer (shows provider, model, token usage estimate)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FN2CSpacing::XS, FN2CSpacing::XS, FN2CSpacing::XS, FN2CSpacing::XXS)
		[
			SNew(SBox)
			.Visibility(InArgs._ShowActionBar ? EVisibility::Visible : EVisibility::Collapsed)
			[
				SAssignNew(ContextVisualizer, SN2CContextWindowVisualizer)
			]
		]
		// Search bar (optional)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FN2CSpacing::XS)
		[
			SNew(SBox)
			.Visibility(InArgs._ShowSearchBar ? EVisibility::Visible : EVisibility::Collapsed)
			[
				SNew(SHorizontalBox)
				// Search input
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(SearchBox, SEditableTextBox)
					.Style(&N2CStyle::GetEditableTextBoxStyle())
					.HintText(LOCTEXT("SearchHint", "Search tags or graphs..."))
					.OnTextChanged(this, &SN2CTagManager::HandleSearchTextChanged)
				]
			]
		]
		// Main split panel
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(FN2CSpacing::XS, 0.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			// Left panel - Categories tree
			+ SSplitter::Slot()
			.Value(0.3f)
			[
				SAssignNew(CategoryTree, SN2CTagCategoryTree)
				.TagSource(TagSource)
				.OnSelectionChanged(FSimpleDelegate::CreateSP(this, &SN2CTagManager::HandleTreeSelectionChanged))
			]
			// Right panel - Graphs list
			+ SSplitter::Slot()
			.Value(0.7f)
			[
				SAssignNew(GraphsList, SN2CTaggedGraphsList)
				.OnSelectionChanged(FSimpleDelegate::CreateSP(this, &SN2CTagManager::HandleListSelectionChanged))
				.OnGraphDoubleClicked(FSimpleDelegate::CreateSP(this, &SN2CTagManager::HandleGraphDoubleClicked))
				.OnRemoveRequested(FSimpleDelegate::CreateLambda([this]() { HandleRemoveSelectedClicked(); }))
				.OnSingleTranslateRequested(FSimpleDelegate::CreateSP(this, &SN2CTagManager::HandleSingleTranslateRequested))
				.OnSingleJsonExportRequested(FSimpleDelegate::CreateSP(this, &SN2CTagManager::HandleSingleJsonExportRequested))
				.OnViewTranslationRequested(FSimpleDelegate::CreateSP(this, &SN2CTagManager::HandleViewTranslationRequested))
			]
		]
		// Actions bar (optional)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FN2CSpacing::XS)
		[
			SNew(SBox)
			.Visibility(InArgs._ShowActionBar ? EVisibility::Visible : EVisibility::Collapsed)
			[
				SNew(SN2CPanel)
					.Variant(EN2CPanelVariant::Light)
					.Padding(FN2CSpacing::MD)
				[
					SNew(SVerticalBox)
					// Actions header + selection info + buttons in one row
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::MD)
					[
						SNew(SHorizontalBox)
						// Left: header + selection info
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ActionsHeader", "ACTIONS"))
								.Font(FN2CFonts::SmallBold())
								.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, FN2CSpacing::XXS, 0.0f, 0.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("SelectionLabel", "Selection: "))
									.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SAssignNew(SelectionCountText, STextBlock)
									.Text(LOCTEXT("NoSelection", "0 graphs selected"))
									.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentGold))
								]
							]
						]
						// Right: action buttons
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(SHorizontalBox)
							// Batch Translate button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
							[
								SAssignNew(BatchTranslateButton, SN2CIconButton)
								.IsEnabled(this, &SN2CTagManager::AreSelectionActionsEnabled)
								.ToolTipText(this, &SN2CTagManager::GetBatchTranslateTooltip)
								.IconBrush(FAppStyle::GetBrush("Icons.Convert"))
								.Text(LOCTEXT("BatchTranslateButton", "Batch Translate"))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.OnClicked(this, &SN2CTagManager::HandleBatchTranslateClicked)
							]
							// Export JSON button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
							[
								SAssignNew(ExportJsonButton, SN2CIconButton)
								.IsEnabled(this, &SN2CTagManager::AreSelectionActionsEnabled)
								.ToolTipText(this, &SN2CTagManager::GetExportJsonTooltip)
								.IconBrush(FAppStyle::GetBrush("Icons.Save"))
								.Text(LOCTEXT("ExportJsonButton", "Export JSON"))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.OnClicked(this, &SN2CTagManager::HandleExportJsonClicked)
							]
							// Remove Selected button
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SAssignNew(RemoveSelectedButton, SN2CIconButton)
								.IsEnabled(this, &SN2CTagManager::AreSelectionActionsEnabled)
								.ToolTipText(this, &SN2CTagManager::GetRemoveSelectedTooltip)
								.IconBrush(FAppStyle::GetBrush("GenericCommands.Delete"))
								.Text(LOCTEXT("RemoveSelectedButton", "Remove Selected"))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.OnClicked(this, &SN2CTagManager::HandleRemoveSelectedClicked)
							]
							// Tag Dependencies: the selected tag on everything the checked Blueprints depend on (card 0319)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
							[
								SNew(SN2CIconButton)
								.IsEnabled(this, &SN2CTagManager::IsTagDependenciesEnabled)
								.ToolTipText(this, &SN2CTagManager::GetTagDependenciesTooltip)
								.IconBrush(FAppStyle::GetBrush("Icons.Link"))
								.Text(LOCTEXT("TagDependenciesButton", "Tag Dependencies"))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.OnClicked(this, &SN2CTagManager::HandleTagDependenciesClicked)
							]
							// Reference Viewer: UE's Reference Viewer on the checked Blueprints (card 0319)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
							[
								SNew(SN2CIconButton)
								.IsEnabled(this, &SN2CTagManager::AreSelectionActionsEnabled)
								.ToolTipText(this, &SN2CTagManager::GetReferenceViewerTooltip)
								.IconBrush(FAppStyle::GetBrush("ContentBrowser.ReferenceViewer"))
								.Text(LOCTEXT("ReferenceViewerButton", "Reference Viewer"))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.OnClicked(this, &SN2CTagManager::HandleReferenceViewerClicked)
							]
							// Prune Missing button: tags and translation records of deleted Blueprints
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
							[
								SNew(SN2CIconButton)
								.IconBrush(FAppStyle::GetBrush("Icons.Warning"))
								.Text(LOCTEXT("PruneMissingButton", "Prune Missing"))
								.ToolTipText(LOCTEXT("PruneMissingTooltip", "Remove the tags and translation records of graphs whose Blueprint was deleted"))
								.ButtonVariant(EN2CButtonVariant::Standard)
								.OnClicked(this, &SN2CTagManager::HandlePruneMissingClicked)
							]
						]
					]

					// Export options row
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						// Minify checkbox
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, FN2CSpacing::XL, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(MinifyCheckbox, SCheckBox)
								.Style(&N2CStyle::GetCheckBoxStyle())
								.IsChecked(bMinifyJson ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
								.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
								{
									bMinifyJson = (NewState == ECheckBoxState::Checked);
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FN2CSpacing::XS, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("MinifyJsonLabel", "Minify JSON"))
								.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
							]
						]
						// Output path
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("OutputLabel", "Output:"))
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
						[
							SAssignNew(OutputPathBox, SEditableTextBox)
							.Style(&N2CStyle::GetEditableTextBoxStyle())
							.Text(FText::FromString(OutputPath))
							.IsReadOnly(true)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle(&N2CStyle::GetButtonStyle())
							.Text(LOCTEXT("BrowseButton", "Browse..."))
							.OnClicked(this, &SN2CTagManager::HandleBrowseClicked)
						]
					]
				]
			]
		]
	];

	// Subscribe to tag manager events for live updates
	OnTagAddedHandle = UN2CGraphStateManager::Get().OnGraphTagAdded.AddRaw(this, &SN2CTagManager::HandleTagAdded);
	OnTagRemovedHandle = UN2CGraphStateManager::Get().OnGraphTagRemoved.AddRaw(this, &SN2CTagManager::HandleTagRemoved);
	OnTagsReloadedHandle = UN2CGraphStateManager::Get().OnGraphStatesReloaded.AddRaw(this, &SN2CTagManager::RequestRefresh);

	// Initial data load
	RefreshData();
}

SN2CTagManager::~SN2CTagManager()
{
	// Unsubscribe from the tag store's events (it may already be shut down when the editor exits)
	if (UN2CGraphStateManager* TagStore = UN2CGraphStateManager::GetIfExists())
	{
		TagStore->OnGraphTagAdded.Remove(OnTagAddedHandle);
		TagStore->OnGraphTagRemoved.Remove(OnTagRemovedHandle);
		TagStore->OnGraphStatesReloaded.Remove(OnTagsReloadedHandle);
	}
}

void SN2CTagManager::HandleTagAdded(const FN2CTagEntry& AddedTag, const FN2CGraphRef& Graph)
{
	RequestRefresh();
}

void SN2CTagManager::HandleTagRemoved(const FN2CGraphKey& Graph, const FString& RemovedTag)
{
	RequestRefresh();
}

void SN2CTagManager::RequestRefresh()
{
	if (bRefreshPending)
	{
		return;
	}
	bRefreshPending = true;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SN2CTagManager::HandleDeferredRefresh));
}

EActiveTimerReturnType SN2CTagManager::HandleDeferredRefresh(double InCurrentTime, float InDeltaTime)
{
	if (bRefreshPending)
	{
		RefreshData();
	}
	return EActiveTimerReturnType::Stop;
}

TArray<FN2CTaggedBlueprintGraph> SN2CTagManager::GetTags() const
{
	return TagSource ? TagSource() : UN2CGraphStateManager::Get().GetAllTags();
}

void SN2CTagManager::RefreshData()
{
	bRefreshPending = false;
	if (CategoryTree.IsValid())
	{
		CategoryTree->RefreshData();
	}
	UpdateGraphsList();
}

FString SN2CTagManager::GetSelectedTag() const
{
	if (CategoryTree.IsValid())
	{
		return CategoryTree->GetSelectedTag();
	}
	return FString();
}

FString SN2CTagManager::GetSelectedCategory() const
{
	if (CategoryTree.IsValid())
	{
		return CategoryTree->GetSelectedCategory();
	}
	return FString();
}

bool SN2CTagManager::IsSelectedCategory() const
{
	if (CategoryTree.IsValid())
	{
		return CategoryTree->IsSelectedCategory();
	}
	return false;
}

TArray<FN2CTagInfo> SN2CTagManager::GetSelectedGraphs() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetSelectedGraphs();
	}
	return TArray<FN2CTagInfo>();
}

TArray<FN2CTagInfo> SN2CTagManager::GetSelectedTagEntries() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetSelectedTagEntries();
	}
	return TArray<FN2CTagInfo>();
}

FText SN2CTagManager::GetBatchTranslateTooltip() const
{
	return AreSelectionActionsEnabled()
		? LOCTEXT("BatchTranslateTooltip", "Translate the checked graphs, one request each")
		: LOCTEXT("BatchTranslateNoneTooltip", "Check one or more graphs in the list to translate them");
}

FText SN2CTagManager::GetExportJsonTooltip() const
{
	return AreSelectionActionsEnabled()
		? LOCTEXT("ExportJsonTooltip", "Save the N2C JSON of the checked graphs to the output folder")
		: LOCTEXT("ExportJsonNoneTooltip", "Check one or more graphs in the list to export their N2C JSON");
}

FText SN2CTagManager::GetRemoveSelectedTooltip() const
{
	return AreSelectionActionsEnabled()
		? LOCTEXT("RemoveSelectedTooltip", "Remove the tags of the checked graphs (their translations are kept)")
		: LOCTEXT("RemoveSelectedNoneTooltip", "Check one or more graphs in the list to remove their tags");
}

int32 SN2CTagManager::GetSelectedCount() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetSelectedCount();
	}
	return 0;
}

bool SN2CTagManager::IsMinifyJsonEnabled() const
{
	return bMinifyJson;
}

FString SN2CTagManager::GetOutputPath() const
{
	return OutputPath;
}

FN2CTagInfo SN2CTagManager::GetDoubleClickedGraph() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetDoubleClickedGraph();
	}
	return FN2CTagInfo();
}

FN2CTagInfo SN2CTagManager::GetTranslateRequestedGraph() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetTranslateRequestedGraph();
	}
	return FN2CTagInfo();
}

FN2CTagInfo SN2CTagManager::GetJsonExportRequestedGraph() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetJsonExportRequestedGraph();
	}
	return FN2CTagInfo();
}

FN2CTagInfo SN2CTagManager::GetViewTranslationRequestedGraph() const
{
	if (GraphsList.IsValid())
	{
		return GraphsList->GetViewTranslationRequestedGraph();
	}
	return FN2CTagInfo();
}

void SN2CTagManager::SelectTag(const FString& InTag, const FString& InCategory)
{
	if (CategoryTree.IsValid())
	{
		CategoryTree->SelectTag(InTag, InCategory);
		bSearchScopedToTree = !CurrentSearchFilter.IsEmpty() && CategoryTree->GetSelectedItem().IsValid();
		UpdateGraphsList();
	}
}

void SN2CTagManager::SelectCategory(const FString& Category)
{
	if (CategoryTree.IsValid())
	{
		CategoryTree->SelectCategory(Category);
		bSearchScopedToTree = !CurrentSearchFilter.IsEmpty() && CategoryTree->GetSelectedItem().IsValid();
		UpdateGraphsList();
	}
}

void SN2CTagManager::SetSearchFilter(const FString& SearchText)
{
	if (SearchBox.IsValid())
	{
		SearchBox->SetText(FText::FromString(SearchText));
	}
	HandleSearchTextChanged(FText::FromString(SearchText));
}

void SN2CTagManager::HandleTreeSelectionChanged()
{
	// A category or tag picked during a search narrows the search to it
	bSearchScopedToTree = !CurrentSearchFilter.IsEmpty() && CategoryTree.IsValid() && CategoryTree->GetSelectedItem().IsValid();
	UpdateGraphsList();

	if (CategoryTree.IsValid())
	{
		if (CategoryTree->IsSelectedTag())
		{
			OnTagSelectedDelegate.ExecuteIfBound();
		}
		else if (CategoryTree->IsSelectedCategory())
		{
			OnCategorySelectedDelegate.ExecuteIfBound();
		}
	}
}

void SN2CTagManager::HandleListSelectionChanged()
{
	UpdateSelectionDisplay();

	// Update context visualizer with new selection
	if (ContextVisualizer.IsValid())
	{
		ContextVisualizer->UpdateForSelection(GetSelectedGraphs());
	}

	OnSelectionChangedDelegate.ExecuteIfBound();
}

void SN2CTagManager::HandleGraphDoubleClicked()
{
	OnGraphDoubleClickedDelegate.ExecuteIfBound();
}

void SN2CTagManager::HandleSearchTextChanged(const FText& NewText)
{
	const FString SearchText = NewText.ToString();
	if (SearchText.IsEmpty() != CurrentSearchFilter.IsEmpty())
	{
		// A search starts across all tags, and ends back on the tree's selection
		bSearchScopedToTree = false;
	}
	CurrentSearchFilter = SearchText;

	// The tree keeps its items (and so its selection) while it filters: the first keystroke no longer clears the list
	if (CategoryTree.IsValid())
	{
		CategoryTree->SetSearchFilter(SearchText);
	}

	UpdateGraphsList();
}

FReply SN2CTagManager::HandleBatchTranslateClicked()
{
	OnBatchTranslateRequestedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply SN2CTagManager::HandleExportJsonClicked()
{
	OnExportJsonRequestedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply SN2CTagManager::HandleRemoveSelectedClicked()
{
	OnRemoveSelectedRequestedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply SN2CTagManager::HandlePruneMissingClicked()
{
	UN2CGraphStateManager& StateManager = UN2CGraphStateManager::Get();
	const int32 Missing = StateManager.RefreshMissing();
	if (Missing == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("PruneMissingNone", "Every tagged or translated graph's Blueprint still exists: there is nothing to prune."));
		return FReply::Handled();
	}

	const FText Question = FText::Format(
		LOCTEXT("PruneMissingConfirm", "{0} {0}|plural(one=graph belongs,other=graphs belong) to Blueprints that no longer exist. Remove {0}|plural(one=its,other=their) tags and translation records? The translation files on disk are kept."),
		FText::AsNumber(Missing));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Question) == EAppReturnType::Yes)
	{
		StateManager.PruneMissing();
		RefreshData();
	}
	return FReply::Handled();
}

TArray<FString> SN2CTagManager::GetSelectedBlueprintPaths() const
{
	TArray<FString> Paths;
	for (const FN2CTagInfo& Graph : GetSelectedGraphs())
	{
		if (!Graph.BlueprintPath.IsEmpty())
		{
			Paths.AddUnique(Graph.BlueprintPath);
		}
	}
	return Paths;
}

bool SN2CTagManager::IsTagDependenciesEnabled() const
{
	return AreSelectionActionsEnabled() && !GetSelectedTag().IsEmpty();
}

FText SN2CTagManager::GetTagDependenciesTooltip() const
{
	if (!AreSelectionActionsEnabled())
	{
		return LOCTEXT("TagDependenciesNoneTooltip", "Check one or more graphs, then select the tag to apply in the tree");
	}
	if (GetSelectedTag().IsEmpty())
	{
		return LOCTEXT("TagDependenciesNoTagTooltip", "Select a tag (not only a category) in the tree: it is the tag this applies");
	}
	return FText::Format(
		LOCTEXT("TagDependenciesTooltip", "Tag every graph of every Blueprint, interface and macro library the checked graphs' Blueprints depend on with '{0}' ({1}), so they show in this list for translation"),
		FText::FromString(GetSelectedTag()), FText::FromString(GetSelectedCategory()));
}

FText SN2CTagManager::GetReferenceViewerTooltip() const
{
	return AreSelectionActionsEnabled()
		? LOCTEXT("ReferenceViewerTooltip", "Open Unreal's Reference Viewer on the checked graphs' Blueprints")
		: LOCTEXT("ReferenceViewerNoneTooltip", "Check one or more graphs to see their Blueprints in Unreal's Reference Viewer");
}

FReply SN2CTagManager::HandleTagDependenciesClicked()
{
	const TArray<FString> Roots = GetSelectedBlueprintPaths();
	const FString TagName = GetSelectedTag();
	const FString Category = GetSelectedCategory();
	if (Roots.Num() == 0 || TagName.IsEmpty())
	{
		return FReply::Handled();
	}

	const FN2CDependencyTagResult Result = FN2CDependencyActions::TagDependencies(Roots, TagName, Category, UN2CGraphStateManager::Get());

	FText Message;
	if (Result.Dependencies == 0)
	{
		Message = FText::Format(LOCTEXT("TagDependenciesNone", "The checked {0}|plural(one=Blueprint depends,other=Blueprints depend) on no other project asset: nothing to tag."),
			FText::AsNumber(Roots.Num()));
	}
	else
	{
		Message = FText::Format(LOCTEXT("TagDependenciesDone", "Tagged {0} {0}|plural(one=graph,other=graphs) in {1} {1}|plural(one=Blueprint,other=Blueprints) with '{2}' ({3} {3}|plural(one=dependency,other=dependencies) found)."),
			FText::AsNumber(Result.TaggedGraphs), FText::AsNumber(Result.TaggedBlueprints), FText::FromString(TagName), FText::AsNumber(Result.Dependencies));
		if (Result.Untagged.Num() > 0)
		{
			Message = FText::Format(LOCTEXT("TagDependenciesUntagged", "{0}\nNo graph to tag: {1}"), Message,
				FText::FromString(FString::Join(Result.Untagged, TEXT(", "))));
		}
	}
	for (const FString& Warning : Result.Warnings)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tag Dependencies: %s"), *Warning), TEXT("TagManager"));
	}

	FNotificationInfo Info(Message);
	Info.ExpireDuration = 8.0f;
	Info.bUseLargeFont = false;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(Result.TaggedGraphs > 0 || Result.Dependencies == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
	RefreshData();
	return FReply::Handled();
}

FReply SN2CTagManager::HandleReferenceViewerClicked()
{
	if (!FN2CDependencyActions::OpenReferenceViewer(GetSelectedBlueprintPaths()))
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("ReferenceViewerUnavailable", "The Reference Viewer could not be opened: the Asset Manager Editor plugin, which provides it, is not loaded."));
	}
	return FReply::Handled();
}

void SN2CTagManager::HandleSingleTranslateRequested()
{
	OnSingleTranslateRequestedDelegate.ExecuteIfBound();
}

void SN2CTagManager::HandleSingleJsonExportRequested()
{
	OnSingleJsonExportRequestedDelegate.ExecuteIfBound();
}

void SN2CTagManager::HandleViewTranslationRequested()
{
	OnViewTranslationRequestedDelegate.ExecuteIfBound();
}

FReply SN2CTagManager::HandleBrowseClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		FString SelectedPath;
		const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("SelectOutputFolder", "Select Output Folder").ToString(),
			OutputPath,
			SelectedPath
		);

		if (bOpened && !SelectedPath.IsEmpty())
		{
			OutputPath = SelectedPath;
			if (OutputPathBox.IsValid())
			{
				OutputPathBox->SetText(FText::FromString(OutputPath));
			}
			// The folder is the user's Custom Translation Output Directory: translations and Export JSON both go there
			// (card 0258), and the choice is kept in their own settings, not the project's
			UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
			UserSettings->CustomTranslationOutputDirectory.Path = SelectedPath;
			UserSettings->SaveToUserConfig({ GET_MEMBER_NAME_CHECKED(UN2CUserSettings, CustomTranslationOutputDirectory) });
		}
	}

	return FReply::Handled();
}

void SN2CTagManager::UpdateGraphsList()
{
	if (!CategoryTree.IsValid() || !GraphsList.IsValid())
	{
		return;
	}

	// Which view: a search across all tags, the tree's category or tag (narrowed by a search, if any), or a prompt
	const TSharedPtr<FN2CTreeItem> SelectedItem = CategoryTree->GetSelectedItem();
	EN2CGraphsListView View = EN2CGraphsListView::Prompt;
	FString Category;
	FString TagName;
	if (!CurrentSearchFilter.IsEmpty() && (!bSearchScopedToTree || !SelectedItem.IsValid()))
	{
		View = EN2CGraphsListView::Search;
	}
	else if (SelectedItem.IsValid() && SelectedItem->IsCategory())
	{
		View = EN2CGraphsListView::Category;
		Category = SelectedItem->Name;
	}
	else if (SelectedItem.IsValid())
	{
		View = EN2CGraphsListView::Tag;
		Category = SelectedItem->Category;
		TagName = SelectedItem->Name;
	}

	// One entry per (graph, tag) in the view; the list shows each graph once (FString == ignores case)
	TArray<FN2CTagInfo> TagInfos;
	if (View != EN2CGraphsListView::Prompt)
	{
		for (const FN2CTaggedBlueprintGraph& Graph : GetTags())
		{
			const FString GraphCategory = FN2CTagEntry::NormalizeCategory(Graph.Category);
			const bool bInView = View == EN2CGraphsListView::Search
				|| (View == EN2CGraphsListView::Category && GraphCategory == Category)
				|| (View == EN2CGraphsListView::Tag && GraphCategory == Category && Graph.Tag == TagName);
			if (bInView)
			{
				TagInfos.Add(FN2CTagInfo::FromTaggedGraph(Graph));
			}
		}
	}

	// A refresh of the view already shown (after a batch, a tag change, a search keystroke) keeps the checked graphs
	const FString ListView = FString::Printf(TEXT("%d|%s|%s"), static_cast<int32>(View), *Category, *TagName);
	const bool bSameView = ListView.Equals(LastListView, ESearchCase::CaseSensitive);
	LastListView = ListView;

	GraphsList->SetView(View, Category, TagName);
	GraphsList->SetSearchFilter(CurrentSearchFilter);
	GraphsList->SetGraphs(TagInfos, bSameView);
	UpdateSelectionDisplay();

	if (ContextVisualizer.IsValid())
	{
		ContextVisualizer->UpdateForSelection(GetSelectedGraphs());
	}
}

void SN2CTagManager::UpdateSelectionDisplay()
{
	if (SelectionCountText.IsValid())
	{
		int32 Count = GetSelectedCount();
		FText DisplayText = FText::Format(
			LOCTEXT("SelectionCountFormat", "{0} {0}|plural(one=graph,other=graphs) selected"),
			FText::AsNumber(Count)
		);
		SelectionCountText->SetText(DisplayText);
	}
}

#undef LOCTEXT_NAMESPACE
