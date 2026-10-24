// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/N2CMainWindowWidget.h"
#include "Core/Widgets/SN2CMainWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "N2CMainWindowWidget"

UN2CMainWindowWidget::UN2CMainWindowWidget()
	: bShowSearchBar(true)
	, bShowActionBar(true)
{
}

TSharedRef<SWidget> UN2CMainWindowWidget::RebuildWidget()
{
	// SN2CMainWindow subscribes to the batch orchestrator and editor integration delegates in Construct.
	// In the UMG designer that would hook a preview widget into live editor state, so show a placeholder.
	if (IsDesignTime())
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DesignTimePlaceholder", "NodeToCode Main Window (not shown in the designer)"))
			];
	}

	MainWindowWidget = SNew(SN2CMainWindow)
		.ShowSearchBar(bShowSearchBar)
		.ShowActionBar(bShowActionBar)
		.OnBatchComplete(FOnN2CBatchOperationComplete::CreateUObject(this, &UN2CMainWindowWidget::HandleBatchComplete))
		.OnTranslationComplete(FOnN2CTranslationComplete::CreateUObject(this, &UN2CMainWindowWidget::HandleTranslationComplete));

	return MainWindowWidget.ToSharedRef();
}

void UN2CMainWindowWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// Note: Most properties are set at construction time through Slate arguments
	// For runtime property changes, we would need to recreate the widget
}

void UN2CMainWindowWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MainWindowWidget.Reset();
}

void UN2CMainWindowWidget::RefreshData()
{
	if (MainWindowWidget.IsValid())
	{
		MainWindowWidget->RefreshData();
	}
}

TArray<FN2CTagInfo> UN2CMainWindowWidget::GetSelectedGraphs() const
{
	if (MainWindowWidget.IsValid())
	{
		return MainWindowWidget->GetSelectedGraphs();
	}
	return TArray<FN2CTagInfo>();
}

int32 UN2CMainWindowWidget::GetSelectedCount() const
{
	if (MainWindowWidget.IsValid())
	{
		return MainWindowWidget->GetSelectedCount();
	}
	return 0;
}

void UN2CMainWindowWidget::ShowTranslationViewer(const FN2CTagInfo& GraphInfo)
{
	if (MainWindowWidget.IsValid())
	{
		MainWindowWidget->ShowTranslationViewer(GraphInfo);
	}
}

void UN2CMainWindowWidget::ShowTranslationViewerWithData(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent)
{
	if (MainWindowWidget.IsValid())
	{
		MainWindowWidget->ShowTranslationViewer(Translation, GraphName, JsonContent);
	}
}

void UN2CMainWindowWidget::HideTranslationViewer()
{
	if (MainWindowWidget.IsValid())
	{
		MainWindowWidget->HideTranslationViewer();
	}
}

bool UN2CMainWindowWidget::IsTranslationViewerVisible() const
{
	if (MainWindowWidget.IsValid())
	{
		return MainWindowWidget->IsTranslationViewerVisible();
	}
	return false;
}

void UN2CMainWindowWidget::ShowBatchProgress()
{
	if (MainWindowWidget.IsValid())
	{
		MainWindowWidget->ShowBatchProgress();
	}
}

void UN2CMainWindowWidget::HideBatchProgress()
{
	if (MainWindowWidget.IsValid())
	{
		MainWindowWidget->HideBatchProgress();
	}
}

bool UN2CMainWindowWidget::IsBatchProgressVisible() const
{
	if (MainWindowWidget.IsValid())
	{
		return MainWindowWidget->IsBatchProgressVisible();
	}
	return false;
}

void UN2CMainWindowWidget::HandleBatchComplete(const FN2CBatchTranslationResult& Result)
{
	OnBatchComplete.Broadcast(Result);
}

void UN2CMainWindowWidget::HandleTranslationComplete(const FN2CTranslationResponse& Response, bool bSuccess)
{
	OnTranslationComplete.Broadcast(Response, bSuccess);
}

#undef LOCTEXT_NAMESPACE
