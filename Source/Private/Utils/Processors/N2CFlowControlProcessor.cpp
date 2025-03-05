// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CFlowControlProcessor.h"

void FN2CFlowControlProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle sequence nodes
    if (Node->IsA<UK2Node_ExecutionSequence>())
    {
        OutNodeDef.Name = TEXT("Sequence");
        
        // Log sequence details
        FString SequenceInfo = FString::Printf(TEXT("Sequence Node: %s"), *OutNodeDef.ID);
        FN2CLogger::Get().Log(SequenceInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle branch (if-then-else) nodes
    if (Node->IsA<UK2Node_IfThenElse>())
    {
        OutNodeDef.Name = TEXT("Branch");
        
        // Log branch details
        FString BranchInfo = FString::Printf(TEXT("Branch Node: %s"), *OutNodeDef.ID);
        FN2CLogger::Get().Log(BranchInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle select nodes
    if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
    {
        OutNodeDef.Name = TEXT("Select");
        
        // Log select details
        FString SelectInfo = FString::Printf(TEXT("Select Node: %s, Options: %d"), 
            *OutNodeDef.ID,
            SelectNode->GetMaxInputPinsNum());
        FN2CLogger::Get().Log(SelectInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle switch nodes
    if (UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node))
    {
        OutNodeDef.Name = TEXT("Switch");
        
        // Log switch details
        FString SwitchInfo = FString::Printf(TEXT("Switch Node: %s"), *OutNodeDef.ID);
        FN2CLogger::Get().Log(SwitchInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle multi-gate nodes
    if (UK2Node_MultiGate* MultiGateNode = Cast<UK2Node_MultiGate>(Node))
    {
        OutNodeDef.Name = TEXT("Multi Gate");
        
        // Log multi-gate details
        FString GateInfo = FString::Printf(TEXT("Multi Gate Node: %s, Outputs: %d"), 
            *OutNodeDef.ID,
            MultiGateNode->GetOutputPins().Num());
        FN2CLogger::Get().Log(GateInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle do-once-multi-input nodes
    if (Node->IsA<UK2Node_DoOnceMultiInput>())
    {
        OutNodeDef.Name = TEXT("Do Once Multi Input");
        
        // Log do-once details
        FString DoOnceInfo = FString::Printf(TEXT("Do Once Multi Input Node: %s"), *OutNodeDef.ID);
        FN2CLogger::Get().Log(DoOnceInfo, EN2CLogSeverity::Debug);
        return;
    }
}
