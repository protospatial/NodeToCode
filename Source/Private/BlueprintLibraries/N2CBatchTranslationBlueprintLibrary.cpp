// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "BlueprintLibraries/N2CBatchTranslationBlueprintLibrary.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"

void UN2CBatchTranslationBlueprintLibrary::StartBatchTranslation(
	const TArray<FN2CTagInfo>& TaggedGraphs,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;
	ErrorMessage.Empty();

	if (TaggedGraphs.IsEmpty())
	{
		ErrorMessage = TEXT("Cannot start batch translation with empty array");
		return;
	}

	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();

	if (Orchestrator.IsBatchInProgress())
	{
		ErrorMessage = TEXT("A batch translation is already in progress");
		return;
	}

	bSuccess = Orchestrator.StartBatchTranslation(TaggedGraphs);

	if (!bSuccess)
	{
		ErrorMessage = TEXT("Failed to start batch translation. Check the log for details.");
	}
}

void UN2CBatchTranslationBlueprintLibrary::TranslateGraphsWithTag(
	const FString& Tag,
	const FString& OptionalCategory,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;
	ErrorMessage.Empty();

	if (Tag.IsEmpty())
	{
		ErrorMessage = TEXT("Tag name cannot be empty");
		return;
	}

	// Get all graphs with the specified tag from the tag manager
	UN2CGraphStateManager& TagManager = UN2CGraphStateManager::Get();
	TArray<FN2CTaggedBlueprintGraph> TaggedGraphs = TagManager.FindTaggedGraphs(Tag, OptionalCategory);

	if (TaggedGraphs.IsEmpty())
	{
		ErrorMessage = FString::Printf(TEXT("No graphs found with tag '%s'%s"),
			*Tag,
			OptionalCategory.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in category '%s'"), *OptionalCategory));
		return;
	}

	// Convert to FN2CTagInfo array
	TArray<FN2CTagInfo> TagInfos;
	for (const FN2CTaggedBlueprintGraph& TaggedGraph : TaggedGraphs)
	{
		TagInfos.Add(FN2CTagInfo::FromTaggedGraph(TaggedGraph));
	}

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Found %d graphs with tag '%s' to translate"), TagInfos.Num(), *Tag),
		EN2CLogSeverity::Info, TEXT("BatchTranslationLib"));

	// Start the batch translation
	StartBatchTranslation(TagInfos, bSuccess, ErrorMessage);
}

void UN2CBatchTranslationBlueprintLibrary::TranslateGraphsInCategory(
	const FString& Category,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;
	ErrorMessage.Empty();

	if (Category.IsEmpty())
	{
		ErrorMessage = TEXT("Category name cannot be empty");
		return;
	}

	// Get all tags in the specified category from the tag manager
	UN2CGraphStateManager& TagManager = UN2CGraphStateManager::Get();
	TArray<FN2CTaggedBlueprintGraph> TaggedGraphs = TagManager.FindTaggedGraphsInCategory(Category);

	if (TaggedGraphs.IsEmpty())
	{
		ErrorMessage = FString::Printf(TEXT("No graphs found in category '%s'"), *Category);
		return;
	}

	// Convert to FN2CTagInfo array
	TArray<FN2CTagInfo> TagInfos;
	for (const FN2CTaggedBlueprintGraph& TaggedGraph : TaggedGraphs)
	{
		TagInfos.Add(FN2CTagInfo::FromTaggedGraph(TaggedGraph));
	}

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Found %d graphs in category '%s' to translate"), TagInfos.Num(), *Category),
		EN2CLogSeverity::Info, TEXT("BatchTranslationLib"));

	// Start the batch translation
	StartBatchTranslation(TagInfos, bSuccess, ErrorMessage);
}

void UN2CBatchTranslationBlueprintLibrary::CancelBatchTranslation()
{
	UN2CBatchTranslationOrchestrator::Get().CancelBatch();
}

bool UN2CBatchTranslationBlueprintLibrary::IsBatchTranslationInProgress()
{
	return UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress();
}

float UN2CBatchTranslationBlueprintLibrary::GetBatchProgress()
{
	return UN2CBatchTranslationOrchestrator::Get().GetBatchProgress();
}

UN2CBatchTranslationOrchestrator* UN2CBatchTranslationBlueprintLibrary::GetBatchOrchestrator()
{
	return &UN2CBatchTranslationOrchestrator::Get();
}

// ==================== Batch JSON Export (No LLM) ====================

void UN2CBatchTranslationBlueprintLibrary::BatchExportJson(
	const TArray<FN2CTagInfo>& TaggedGraphs,
	bool bMinifyJson,
	bool& bSuccess,
	FN2CBatchJsonExportResult& Result,
	FString& ErrorMessage)
{
	bSuccess = false;
	ErrorMessage.Empty();
	Result = FN2CBatchJsonExportResult();

	if (TaggedGraphs.IsEmpty())
	{
		ErrorMessage = TEXT("Cannot export with empty array");
		return;
	}

	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();

	if (Orchestrator.IsBatchInProgress())
	{
		ErrorMessage = TEXT("A batch translation is already in progress");
		return;
	}

	bSuccess = Orchestrator.BatchExportJson(TaggedGraphs, Result, bMinifyJson);

	if (!bSuccess)
	{
		ErrorMessage = TEXT("Failed to complete batch JSON export. Check the log for details.");
	}
}

void UN2CBatchTranslationBlueprintLibrary::ExportGraphsWithTagToJson(
	const FString& Tag,
	const FString& OptionalCategory,
	bool bMinifyJson,
	bool& bSuccess,
	FN2CBatchJsonExportResult& Result,
	FString& ErrorMessage)
{
	bSuccess = false;
	ErrorMessage.Empty();
	Result = FN2CBatchJsonExportResult();

	if (Tag.IsEmpty())
	{
		ErrorMessage = TEXT("Tag name cannot be empty");
		return;
	}

	// Get all graphs with the specified tag from the tag manager
	UN2CGraphStateManager& TagManager = UN2CGraphStateManager::Get();
	TArray<FN2CTaggedBlueprintGraph> TaggedGraphs = TagManager.FindTaggedGraphs(Tag, OptionalCategory);

	if (TaggedGraphs.IsEmpty())
	{
		ErrorMessage = FString::Printf(TEXT("No graphs found with tag '%s'%s"),
			*Tag,
			OptionalCategory.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in category '%s'"), *OptionalCategory));
		return;
	}

	// Convert to FN2CTagInfo array
	TArray<FN2CTagInfo> TagInfos;
	for (const FN2CTaggedBlueprintGraph& TaggedGraph : TaggedGraphs)
	{
		TagInfos.Add(FN2CTagInfo::FromTaggedGraph(TaggedGraph));
	}

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Found %d graphs with tag '%s' to export"), TagInfos.Num(), *Tag),
		EN2CLogSeverity::Info, TEXT("BatchTranslationLib"));

	// Export the graphs
	BatchExportJson(TagInfos, bMinifyJson, bSuccess, Result, ErrorMessage);
}

void UN2CBatchTranslationBlueprintLibrary::ExportGraphsInCategoryToJson(
	const FString& Category,
	bool bMinifyJson,
	bool& bSuccess,
	FN2CBatchJsonExportResult& Result,
	FString& ErrorMessage)
{
	bSuccess = false;
	ErrorMessage.Empty();
	Result = FN2CBatchJsonExportResult();

	if (Category.IsEmpty())
	{
		ErrorMessage = TEXT("Category name cannot be empty");
		return;
	}

	// Get all tags in the specified category from the tag manager
	UN2CGraphStateManager& TagManager = UN2CGraphStateManager::Get();
	TArray<FN2CTaggedBlueprintGraph> TaggedGraphs = TagManager.FindTaggedGraphsInCategory(Category);

	if (TaggedGraphs.IsEmpty())
	{
		ErrorMessage = FString::Printf(TEXT("No graphs found in category '%s'"), *Category);
		return;
	}

	// Convert to FN2CTagInfo array
	TArray<FN2CTagInfo> TagInfos;
	for (const FN2CTaggedBlueprintGraph& TaggedGraph : TaggedGraphs)
	{
		TagInfos.Add(FN2CTagInfo::FromTaggedGraph(TaggedGraph));
	}

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Found %d graphs in category '%s' to export"), TagInfos.Num(), *Category),
		EN2CLogSeverity::Info, TEXT("BatchTranslationLib"));

	// Export the graphs
	BatchExportJson(TagInfos, bMinifyJson, bSuccess, Result, ErrorMessage);
}
