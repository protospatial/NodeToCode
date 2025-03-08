// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CFunctionCallProcessor.h"

void FN2CFunctionCallProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node);
    if (!FuncNode)
    {
        return;
    }
    
    if (UFunction* Function = FuncNode->GetTargetFunction())
    {
        OutNodeDef.MemberParent = GetCleanClassName(Function->GetOwnerClass()->GetName());
        OutNodeDef.MemberName = GetCleanClassName(Function->GetName());
        OutNodeDef.bLatent = FuncNode->IsLatentFunction();
        
        // Log function details
        FString FunctionInfo = FString::Printf(TEXT("Function call: %s::%s, Latent: %s"),
            *OutNodeDef.MemberParent,
            *OutNodeDef.MemberName,
            OutNodeDef.bLatent ? TEXT("true") : TEXT("false"));
        FN2CLogger::Get().Log(FunctionInfo, EN2CLogSeverity::Debug);
    }
}
