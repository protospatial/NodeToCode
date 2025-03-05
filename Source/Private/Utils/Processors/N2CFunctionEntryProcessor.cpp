// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CFunctionEntryProcessor.h"

void FN2CFunctionEntryProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle function entry nodes
    if (UK2Node_FunctionEntry* FuncEntryNode = Cast<UK2Node_FunctionEntry>(Node))
    {
        OutNodeDef.MemberName = FuncEntryNode->CustomGeneratedFunctionName.ToString();
        if (UBlueprint* BP = FuncEntryNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
        
        // Log function entry details
        FString EntryInfo = FString::Printf(TEXT("Function Entry: %s, Function: %s, Class: %s"),
            *OutNodeDef.Name,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(EntryInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle function result nodes
    if (UK2Node_FunctionResult* FuncResultNode = Cast<UK2Node_FunctionResult>(Node))
    {
        if (UBlueprint* BP = FuncResultNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
        
        // Log function result details
        FString ResultInfo = FString::Printf(TEXT("Function Result: %s, Class: %s"),
            *OutNodeDef.Name,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(ResultInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle macro instance nodes
    if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
        {
            OutNodeDef.MemberName = GetCleanClassName(MacroGraph->GetName());
            if (UBlueprint* BP = Cast<UBlueprint>(MacroGraph->GetOuter()))
            {
                OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
            }
        }
        
        // Log macro instance details
        FString MacroInfo = FString::Printf(TEXT("Macro Instance: %s, Macro: %s, Blueprint: %s"),
            *OutNodeDef.Name,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(MacroInfo, EN2CLogSeverity::Debug);
        return;
    }
}
