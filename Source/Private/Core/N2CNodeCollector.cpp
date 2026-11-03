// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CNodeCollector.h"
#include "Utils/N2CLogger.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FN2CNodeCollector& FN2CNodeCollector::Get()
{
    static FN2CNodeCollector Instance;
    return Instance;
}

bool FN2CNodeCollector::CollectNodesFromGraph(UEdGraph* Graph, TArray<UK2Node*>& OutNodes)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(N2C_Capture_CollectNodes);
    if (!Graph)
    {
        FN2CLogger::Get().LogWarning(TEXT("Invalid graph provided to CollectNodesFromGraph"));
        return false;
    }

    // Get Blueprint name for context
    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Graph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }

    // Log collection start
    FString Context = FString::Printf(TEXT("Blueprint: %s, Graph: %s"),
        *BlueprintName,
        *Graph->GetName());
    FN2CLogger::Get().Log(TEXT("Starting node collection"), EN2CLogSeverity::Info, Context);

    // Collect K2 nodes from the graph
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node* K2Node = Cast<UK2Node>(Node))
        {
            OutNodes.Add(K2Node);
        }
    }

    // Log collection results
    FString ResultContext = FString::Printf(TEXT("%d nodes collected from %s"), 
        OutNodes.Num(), 
        *Context);
    FN2CLogger::Get().Log(TEXT("Node collection complete"), EN2CLogSeverity::Info, ResultContext);

    return true;
}
