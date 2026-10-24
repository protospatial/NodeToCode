// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "N2CTagManagerWidget.generated.h"

/**
 * UMG widget wrapper for the N2C Tag Manager
 * Provides a complete UI for managing tagged Blueprint graphs
 *
 * Kept only so Editor Utility Widgets that embed it keep loading; unsupported and planned for removal.
 * NodeToCode 2.0 no longer ships the EUW UI (Content/UI) that used it.
 */
UCLASS(Meta=(DisplayName="Tag Manager"))
class NODETOCODE_API UN2CTagManagerWidget : public UWidget
{
	GENERATED_BODY()

public:
	UN2CTagManagerWidget();

	// ==================== Blueprint-Callable Functions ====================

	/** Refresh all data from the tag manager */
	UFUNCTION(BlueprintCallable, Category = "Tag Manager")
	void RefreshData();

	/** Programmatically select a specific tag */
	UFUNCTION(BlueprintCallable, Category = "Tag Manager|Selection")
	void SelectTag(const FString& Tag, const FString& Category);

	/** Programmatically select a specific category */
	UFUNCTION(BlueprintCallable, Category = "Tag Manager|Selection")
	void SelectCategory(const FString& Category);

	/** Get the currently selected graphs */
	UFUNCTION(BlueprintCallable, Category = "Tag Manager|Selection")
	TArray<FN2CTagInfo> GetSelectedGraphs() const;

	/** Get the count of selected graphs */
	UFUNCTION(BlueprintPure, Category = "Tag Manager|Selection")
	int32 GetSelectedCount() const;

	/** Get the currently selected tag (empty if category selected) */
	UFUNCTION(BlueprintPure, Category = "Tag Manager|Selection")
	FString GetSelectedTag() const;

	/** Get the currently selected category */
	UFUNCTION(BlueprintPure, Category = "Tag Manager|Selection")
	FString GetSelectedCategory() const;

	/** Check if a category is currently selected (vs a tag) */
	UFUNCTION(BlueprintPure, Category = "Tag Manager|Selection")
	bool IsSelectedCategory() const;

	/** Set the search filter text */
	UFUNCTION(BlueprintCallable, Category = "Tag Manager|Filtering")
	void SetSearchFilter(const FString& SearchText);

	/** Get whether minify JSON is enabled */
	UFUNCTION(BlueprintPure, Category = "Tag Manager|Export")
	bool IsMinifyJsonEnabled() const;

	/** Get the current output path */
	UFUNCTION(BlueprintPure, Category = "Tag Manager|Export")
	FString GetOutputPath() const;

	/** Get the graph that was double-clicked */
	UFUNCTION(BlueprintPure, Category = "Tag Manager")
	FN2CTagInfo GetDoubleClickedGraph() const;

	/** Get the graph for which translate was requested */
	UFUNCTION(BlueprintPure, Category = "Tag Manager")
	FN2CTagInfo GetTranslateRequestedGraph() const;

	/** Get the graph for which JSON export was requested */
	UFUNCTION(BlueprintPure, Category = "Tag Manager")
	FN2CTagInfo GetJsonExportRequestedGraph() const;

	/** Get the graph for which view translation was requested */
	UFUNCTION(BlueprintPure, Category = "Tag Manager")
	FN2CTagInfo GetViewTranslationRequestedGraph() const;

	// ==================== Blueprint-Assignable Events ====================

	/** Fired when a tag is selected in the tree */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnTagSelectedEvent OnTagSelected;

	/** Fired when a category is selected in the tree */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnCategorySelectedEvent OnCategorySelected;

	/** Fired when a graph row is double-clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnGraphDoubleClickedEvent OnGraphDoubleClicked;

	/** Fired when selection changes in the graphs list */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnSelectionChangedEvent OnSelectionChanged;

	/** Fired when the Batch Translate button is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnBatchTranslateRequestedEvent OnBatchTranslateRequested;

	/** Fired when the Export JSON button is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnExportJsonRequestedEvent OnExportJsonRequested;

	/** Fired when the Remove Selected button is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnRemoveSelectedRequestedEvent OnRemoveSelectedRequested;

	/** Fired when a single graph's Translate button is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnSingleTranslateRequestedEvent OnSingleTranslateRequested;

	/** Fired when a single graph's JSON export button is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnSingleJsonExportRequestedEvent OnSingleJsonExportRequested;

	/** Fired when a single graph's View Translation button is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Tag Manager|Events")
	FOnViewTranslationRequestedEvent OnViewTranslationRequested;

	// ==================== Widget Properties ====================

	/** Whether to show the search bar */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Manager|Appearance")
	bool bShowSearchBar;

	/** Whether to show the action bar */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Manager|Appearance")
	bool bShowActionBar;

	/** Default state of the minify JSON checkbox */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag Manager|Defaults")
	bool bMinifyJsonByDefault;

	// ==================== UWidget Interface ====================

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;

private:
	/** The Slate widget */
	TSharedPtr<class SN2CTagManager> TagManagerWidget;

	/** Handle tag selection from Slate widget */
	void HandleTagSelected();

	/** Handle category selection from Slate widget */
	void HandleCategorySelected();

	/** Handle graph double-click from Slate widget */
	void HandleGraphDoubleClicked();

	/** Handle selection change from Slate widget */
	void HandleSelectionChanged();

	/** Handle batch translate request from Slate widget */
	void HandleBatchTranslateRequested();

	/** Handle export JSON request from Slate widget */
	void HandleExportJsonRequested();

	/** Handle remove selected request from Slate widget */
	void HandleRemoveSelectedRequested();

	/** Handle single graph translate request from Slate widget */
	void HandleSingleTranslateRequested();

	/** Handle single graph JSON export request from Slate widget */
	void HandleSingleJsonExportRequested();

	/** Handle view translation request from Slate widget */
	void HandleViewTranslationRequested();
};
