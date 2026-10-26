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
            OutNodeDef.MemberParent = BP->GetName();
        }
        
        // Log function entry details
        N2C_LOG(Debug, TEXT("Function Entry: %s, Function: %s, Class: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        return;
    }
    
    // Handle function result nodes
    if (UK2Node_FunctionResult* FuncResultNode = Cast<UK2Node_FunctionResult>(Node))
    {
        if (UBlueprint* BP = FuncResultNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = BP->GetName();
        }
        
        // Log function result details
        N2C_LOG(Debug, TEXT("Function Result: %s, Class: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberParent);
        return;
    }
    
    // Handle macro instance nodes
    if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
        {
            OutNodeDef.MemberName = MacroGraph->GetName();
            if (UBlueprint* BP = Cast<UBlueprint>(MacroGraph->GetOuter()))
            {
                OutNodeDef.MemberParent = BP->GetName();
            }
        }
        
        // Log macro instance details
        N2C_LOG(Debug, TEXT("Macro Instance: %s, Macro: %s, Blueprint: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        return;
    }
}
