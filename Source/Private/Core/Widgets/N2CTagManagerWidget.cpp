// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/N2CTagManagerWidget.h"
#include "Core/Widgets/SN2CTagManager.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "N2CTagManagerWidget"

UN2CTagManagerWidget::UN2CTagManagerWidget()
	: bShowSearchBar(true)
	, bShowActionBar(true)
	, bMinifyJsonByDefault(true)
{
}

TSharedRef<SWidget> UN2CTagManagerWidget::RebuildWidget()
{
	// SN2CTagManager subscribes to UN2CTagManager's tag delegates in Construct. In the UMG designer that
	// would hook a preview widget into live editor state, so show a placeholder.
	if (IsDesignTime())
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DesignTimePlaceholder", "NodeToCode Tag Manager (not shown in the designer)"))
			];
	}

	TagManagerWidget = SNew(SN2CTagManager)
		.ShowSearchBar(bShowSearchBar)
		.ShowActionBar(bShowActionBar)
		.MinifyJsonByDefault(bMinifyJsonByDefault)
		.OnTagSelected(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleTagSelected))
		.OnCategorySelected(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleCategorySelected))
		.OnGraphDoubleClicked(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleGraphDoubleClicked))
		.OnSelectionChanged(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleSelectionChanged))
		.OnBatchTranslateRequested(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleBatchTranslateRequested))
		.OnExportJsonRequested(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleExportJsonRequested))
		.OnRemoveSelectedRequested(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleRemoveSelectedRequested))
		.OnSingleTranslateRequested(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleSingleTranslateRequested))
		.OnSingleJsonExportRequested(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleSingleJsonExportRequested))
		.OnViewTranslationRequested(FSimpleDelegate::CreateUObject(this, &UN2CTagManagerWidget::HandleViewTranslationRequested));

	return TagManagerWidget.ToSharedRef();
}

void UN2CTagManagerWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// Note: Most properties are set at construction time through Slate arguments
	// For runtime property changes, we would need to recreate the widget
}

void UN2CTagManagerWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TagManagerWidget.Reset();
}

void UN2CTagManagerWidget::RefreshData()
{
	if (TagManagerWidget.IsValid())
	{
		TagManagerWidget->RefreshData();
	}
}

void UN2CTagManagerWidget::SelectTag(const FString& Tag, const FString& Category)
{
	if (TagManagerWidget.IsValid())
	{
		TagManagerWidget->SelectTag(Tag, Category);
	}
}

void UN2CTagManagerWidget::SelectCategory(const FString& Category)
{
	if (TagManagerWidget.IsValid())
	{
		TagManagerWidget->SelectCategory(Category);
	}
}

TArray<FN2CTagInfo> UN2CTagManagerWidget::GetSelectedGraphs() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetSelectedGraphs();
	}
	return TArray<FN2CTagInfo>();
}

int32 UN2CTagManagerWidget::GetSelectedCount() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetSelectedCount();
	}
	return 0;
}

FString UN2CTagManagerWidget::GetSelectedTag() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetSelectedTag();
	}
	return FString();
}

FString UN2CTagManagerWidget::GetSelectedCategory() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetSelectedCategory();
	}
	return FString();
}

bool UN2CTagManagerWidget::IsSelectedCategory() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->IsSelectedCategory();
	}
	return false;
}

void UN2CTagManagerWidget::SetSearchFilter(const FString& SearchText)
{
	if (TagManagerWidget.IsValid())
	{
		TagManagerWidget->SetSearchFilter(SearchText);
	}
}

bool UN2CTagManagerWidget::IsMinifyJsonEnabled() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->IsMinifyJsonEnabled();
	}
	return bMinifyJsonByDefault;
}

FString UN2CTagManagerWidget::GetOutputPath() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetOutputPath();
	}
	return FString();
}

FN2CTagInfo UN2CTagManagerWidget::GetDoubleClickedGraph() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetDoubleClickedGraph();
	}
	return FN2CTagInfo();
}

FN2CTagInfo UN2CTagManagerWidget::GetTranslateRequestedGraph() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetTranslateRequestedGraph();
	}
	return FN2CTagInfo();
}

FN2CTagInfo UN2CTagManagerWidget::GetJsonExportRequestedGraph() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetJsonExportRequestedGraph();
	}
	return FN2CTagInfo();
}

FN2CTagInfo UN2CTagManagerWidget::GetViewTranslationRequestedGraph() const
{
	if (TagManagerWidget.IsValid())
	{
		return TagManagerWidget->GetViewTranslationRequestedGraph();
	}
	return FN2CTagInfo();
}

void UN2CTagManagerWidget::HandleTagSelected()
{
	if (TagManagerWidget.IsValid())
	{
		FString Tag = TagManagerWidget->GetSelectedTag();
		FString Category = TagManagerWidget->GetSelectedCategory();
		OnTagSelected.Broadcast(Tag, Category);
	}
}

void UN2CTagManagerWidget::HandleCategorySelected()
{
	if (TagManagerWidget.IsValid())
	{
		FString Category = TagManagerWidget->GetSelectedCategory();
		OnCategorySelected.Broadcast(Category);
	}
}

void UN2CTagManagerWidget::HandleGraphDoubleClicked()
{
	if (TagManagerWidget.IsValid())
	{
		FN2CTagInfo TagInfo = TagManagerWidget->GetDoubleClickedGraph();
		OnGraphDoubleClicked.Broadcast(TagInfo);
	}
}

void UN2CTagManagerWidget::HandleSelectionChanged()
{
	if (TagManagerWidget.IsValid())
	{
		int32 Count = TagManagerWidget->GetSelectedCount();
		OnSelectionChanged.Broadcast(Count);
	}
}

void UN2CTagManagerWidget::HandleBatchTranslateRequested()
{
	if (TagManagerWidget.IsValid())
	{
		TArray<FN2CTagInfo> SelectedGraphs = TagManagerWidget->GetSelectedGraphs();
		OnBatchTranslateRequested.Broadcast(SelectedGraphs);
	}
}

void UN2CTagManagerWidget::HandleExportJsonRequested()
{
	if (TagManagerWidget.IsValid())
	{
		TArray<FN2CTagInfo> SelectedGraphs = TagManagerWidget->GetSelectedGraphs();
		bool bMinify = TagManagerWidget->IsMinifyJsonEnabled();
		OnExportJsonRequested.Broadcast(SelectedGraphs, bMinify);
	}
}

void UN2CTagManagerWidget::HandleRemoveSelectedRequested()
{
	if (TagManagerWidget.IsValid())
	{
		TArray<FN2CTagInfo> SelectedGraphs = TagManagerWidget->GetSelectedGraphs();
		OnRemoveSelectedRequested.Broadcast(SelectedGraphs);
	}
}

void UN2CTagManagerWidget::HandleSingleTranslateRequested()
{
	if (TagManagerWidget.IsValid())
	{
		FN2CTagInfo TagInfo = TagManagerWidget->GetTranslateRequestedGraph();
		OnSingleTranslateRequested.Broadcast(TagInfo);
	}
}

void UN2CTagManagerWidget::HandleSingleJsonExportRequested()
{
	if (TagManagerWidget.IsValid())
	{
		FN2CTagInfo TagInfo = TagManagerWidget->GetJsonExportRequestedGraph();
		bool bMinify = TagManagerWidget->IsMinifyJsonEnabled();
		OnSingleJsonExportRequested.Broadcast(TagInfo, bMinify);
	}
}

void UN2CTagManagerWidget::HandleViewTranslationRequested()
{
	if (TagManagerWidget.IsValid())
	{
		FN2CTagInfo TagInfo = TagManagerWidget->GetViewTranslationRequestedGraph();
		OnViewTranslationRequested.Broadcast(TagInfo);
	}
}

#undef LOCTEXT_NAMESPACE
