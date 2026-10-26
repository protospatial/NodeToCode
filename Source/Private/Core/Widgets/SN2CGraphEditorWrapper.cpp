// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CGraphEditorWrapper.h"
#include "Core/Widgets/SN2CGraphOverlay.h"
#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Utils/N2CLogger.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

void SN2CGraphEditorWrapper::Construct(const FArguments& InArgs)
{
	FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: ENTER"), EN2CLogSeverity::Warning);

	FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Storing arguments"), EN2CLogSeverity::Warning);
	OriginalContent = InArgs._GraphEditorContent;
	BlueprintEditor = InArgs._BlueprintEditor;
	Graph = InArgs._Graph;

	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: OriginalContent valid=%d"), OriginalContent.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);
	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: BlueprintEditor valid=%d"), BlueprintEditor.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);
	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: Graph valid=%d"), Graph.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);

	// Get graph info for the overlay
	FGuid GraphGuid;
	FString GraphName;
	FString BlueprintPath;

	if (Graph.IsValid())
	{
		FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Getting graph info"), EN2CLogSeverity::Warning);
		GraphGuid = Graph->GraphGuid;
		GraphName = Graph->GetName();
		FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: GraphName=%s"), *GraphName), EN2CLogSeverity::Warning);

		// Get the Blueprint path from the graph's outer
		FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Getting Outer"), EN2CLogSeverity::Warning);
		if (UObject* Outer = Graph->GetOuter())
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: Outer=%s"), *Outer->GetName()), EN2CLogSeverity::Warning);
			if (UBlueprint* OwningBlueprint = Cast<UBlueprint>(Outer))
			{
				BlueprintPath = OwningBlueprint->GetPathName();
				FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: BlueprintPath=%s"), *BlueprintPath), EN2CLogSeverity::Warning);
			}
			else
			{
				FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Outer is not Blueprint, searching up hierarchy"), EN2CLogSeverity::Warning);
				// The graph might be nested under another object in the Blueprint
				UObject* OuterOuter = Outer->GetOuter();
				while (OuterOuter)
				{
					FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: Checking OuterOuter=%s"), *OuterOuter->GetName()), EN2CLogSeverity::Warning);
					if (UBlueprint* NestedBlueprint = Cast<UBlueprint>(OuterOuter))
					{
						BlueprintPath = NestedBlueprint->GetPathName();
						FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: Found BlueprintPath=%s"), *BlueprintPath), EN2CLogSeverity::Warning);
						break;
					}
					OuterOuter = OuterOuter->GetOuter();
				}
			}
		}
		else
		{
			FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Graph has no Outer!"), EN2CLogSeverity::Warning);
		}
	}
	else
	{
		FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Graph is INVALID"), EN2CLogSeverity::Warning);
	}

	FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: About to build widget hierarchy"), EN2CLogSeverity::Warning);

	ChildSlot
	[
		SNew(SOverlay)

		// Original graph editor content (fills the entire area)
		+ SOverlay::Slot()
		[
			OriginalContent.IsValid() ? OriginalContent.ToSharedRef() : SNullWidget::NullWidget
		]

		// Our overlay in the bottom-right corner
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(10.0f, 0.0f, 0.0f, 10.0f)) // Right and bottom padding
		[
			SAssignNew(OverlayWidget, SN2CGraphOverlay)
			.GraphGuid(GraphGuid)
			.BlueprintPath(BlueprintPath)
			.GraphName(GraphName)
			.BlueprintEditor(BlueprintEditor)
			.Graph(Graph.Get())
		]
	];

	FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: Widget hierarchy built successfully"), EN2CLogSeverity::Warning);
	FN2CLogger::Get().Log(FString::Printf(TEXT("SN2CGraphEditorWrapper::Construct: OverlayWidget valid=%d"), OverlayWidget.IsValid() ? 1 : 0), EN2CLogSeverity::Warning);
	FN2CLogger::Get().Log(TEXT("SN2CGraphEditorWrapper::Construct: EXIT"), EN2CLogSeverity::Warning);
}

void SN2CGraphEditorWrapper::UpdateOverlay()
{
	if (OverlayWidget.IsValid())
	{
		OverlayWidget->RefreshTagCount();
	}
}

bool SN2CGraphEditorWrapper::IsForGraph(const UEdGraph* InGraph) const
{
	return Graph.IsValid() && Graph.Get() == InGraph;
}
