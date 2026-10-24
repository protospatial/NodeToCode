// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "Models/N2CTranslation.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "N2CMainWindowWidget.generated.h"

/**
 * Delegate for when a batch translation operation completes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMainWindowBatchComplete, const FN2CBatchTranslationResult&, Result);

/**
 * Delegate for when a single translation completes
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMainWindowTranslationComplete, const FN2CTranslationResponse&, Response, bool, bSuccess);

/**
 * UMG widget wrapper for the NodeToCode Main Window
 * Provides the complete NodeToCode UI with all functionality integrated in C++
 * This widget can be embedded in Editor Utility Widgets or other UMG contexts
 *
 * Kept only so Editor Utility Widgets that embed it keep loading; unsupported and planned for removal.
 * NodeToCode 2.0 no longer ships the EUW UI (Content/UI) that used it.
 */
UCLASS(Meta=(DisplayName="NodeToCode Main Window"))
class NODETOCODE_API UN2CMainWindowWidget : public UWidget
{
	GENERATED_BODY()

public:
	UN2CMainWindowWidget();

	// ==================== Blueprint-Callable Functions ====================

	/** Refresh all data in the tag manager */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window")
	void RefreshData();

	/** Get the currently selected graphs from the tag manager */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window|Selection")
	TArray<FN2CTagInfo> GetSelectedGraphs() const;

	/** Get the count of selected graphs */
	UFUNCTION(BlueprintPure, Category = "NodeToCode|Main Window|Selection")
	int32 GetSelectedCount() const;

	/** Show the translation viewer for a specific graph (loads from state manager) */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window|Overlays")
	void ShowTranslationViewer(const FN2CTagInfo& GraphInfo);

	/** Show the translation viewer with specific translation data */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window|Overlays")
	void ShowTranslationViewerWithData(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent);

	/** Hide the translation viewer overlay */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window|Overlays")
	void HideTranslationViewer();

	/** Check if the translation viewer is currently visible */
	UFUNCTION(BlueprintPure, Category = "NodeToCode|Main Window|Overlays")
	bool IsTranslationViewerVisible() const;

	/** Show the batch progress modal */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window|Overlays")
	void ShowBatchProgress();

	/** Hide the batch progress modal */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Main Window|Overlays")
	void HideBatchProgress();

	/** Check if the batch progress modal is currently visible */
	UFUNCTION(BlueprintPure, Category = "NodeToCode|Main Window|Overlays")
	bool IsBatchProgressVisible() const;

	// ==================== Blueprint-Assignable Events ====================

	/** Fired when a batch translation operation completes */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Main Window|Events")
	FOnMainWindowBatchComplete OnBatchComplete;

	/** Fired when a single translation completes */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Main Window|Events")
	FOnMainWindowTranslationComplete OnTranslationComplete;

	// ==================== Widget Properties ====================

	/** Whether to show the search bar in the tag manager */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NodeToCode|Main Window|Appearance")
	bool bShowSearchBar;

	/** Whether to show the action bar in the tag manager */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NodeToCode|Main Window|Appearance")
	bool bShowActionBar;

	// ==================== UWidget Interface ====================

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;

private:
	/** The Slate main window widget */
	TSharedPtr<class SN2CMainWindow> MainWindowWidget;

	/** Handle batch operation completion from Slate widget */
	void HandleBatchComplete(const FN2CBatchTranslationResult& Result);

	/** Handle single translation completion from Slate widget */
	void HandleTranslationComplete(const FN2CTranslationResponse& Response, bool bSuccess);
};
