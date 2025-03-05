// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CVariableProcessor.h"

void FN2CVariableProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
    if (!VarNode)
    {
        return;
    }
    
    // Get variable reference info
    const FMemberReference& VarRef = VarNode->VariableReference;

    OutNodeDef.Name = VarNode->GetVarNameString();
    
    // Get member name safely
    FName MemberName = VarRef.GetMemberName();
    if (!MemberName.IsNone())
    {
        OutNodeDef.MemberName = GetCleanClassName(MemberName.ToString());
    }
    else if (UEdGraph* Graph = Node->GetGraph())
    {
        // Fallback to a generated name using the graph
        OutNodeDef.MemberName = FString::Printf(TEXT("Var_%s_%d"), 
            *Graph->GetName(),
            Node->GetUniqueID());
    }
    else
    {
        OutNodeDef.MemberName = FString::Printf(TEXT("UnknownVariable"));
    }
    
    // For variable nodes, we want the pin type info as the MemberParent
    // since it represents the variable's type
    for (UEdGraphPin* Pin : VarNode->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != FName("exec"))
        {
            // Start with required PinCategory
            TArray<FString> TypeParts;
            TypeParts.Add(GetCleanClassName(Pin->PinType.PinCategory.ToString()));
            
            // Add optional parts if they exist and aren't "None"
            if (!Pin->PinType.PinSubCategory.IsNone() && !Pin->PinType.PinSubCategory.ToString().IsEmpty())
            {
                TypeParts.Add(GetCleanClassName(Pin->PinType.PinSubCategory.ToString()));
            }
            
            if (Pin->PinType.PinSubCategoryObject.IsValid())
            {
                TypeParts.Add(GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName()));
            }
            
            if (!Pin->PinType.PinSubCategoryMemberReference.MemberName.IsNone())
            {
                TypeParts.Add(GetCleanClassName(Pin->PinType.PinSubCategoryMemberReference.MemberName.ToString()));
            }
            
            // Join with forward slashes
            OutNodeDef.MemberParent = FString::Join(TypeParts, TEXT("/"));
            break;
        }
    }
    
    // Log variable details
    FString VarInfo = FString::Printf(TEXT("Variable: %s, Type: %s"),
        *OutNodeDef.MemberName,
        *OutNodeDef.MemberParent);
    FN2CLogger::Get().Log(VarInfo, EN2CLogSeverity::Debug);
}
