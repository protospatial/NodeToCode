// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CDelegateProcessor.h"

void FN2CDelegateProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle base multicast delegate nodes
    if (UK2Node_BaseMCDelegate* DelegateNode = Cast<UK2Node_BaseMCDelegate>(Node))
    {
        OutNodeDef.MemberName = DelegateNode->DelegateReference.GetMemberName().ToString();
        if (UClass* DelegateClass = DelegateNode->DelegateReference.GetMemberParentClass())
        {
            OutNodeDef.MemberParent = GetCleanClassName(DelegateClass->GetName());
        }
        
        // Log delegate details
        FString DelegateInfo = FString::Printf(TEXT("Delegate Node: %s, Member: %s, Parent: %s"),
            *OutNodeDef.Name,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(DelegateInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle create delegate nodes
    if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
    {
        OutNodeDef.MemberName = GetCleanClassName(CreateDelegateNode->GetFunctionName().ToString());
        
        // Try to get the scope class first since it's more reliable
        if (UClass* ScopeClass = CreateDelegateNode->GetScopeClass())
        {
            OutNodeDef.MemberParent = GetCleanClassName(ScopeClass->GetName());
        }
        // Fallback to delegate signature if scope class isn't available
        else if (UFunction* DelegateSignature = CreateDelegateNode->GetDelegateSignature())
        {
            if (UClass* OwnerClass = DelegateSignature->GetOwnerClass())
            {
                OutNodeDef.MemberParent = GetCleanClassName(OwnerClass->GetName());
            }
        }
        
        // Log create delegate details
        FString CreateDelegateInfo = FString::Printf(TEXT("Create Delegate: %s, Function: %s, Class: %s"),
            *OutNodeDef.Name,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(CreateDelegateInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle call delegate nodes
    if (UK2Node_CallDelegate* CallDelegateNode = Cast<UK2Node_CallDelegate>(Node))
    {
        if (UFunction* Function = CallDelegateNode->GetDelegateSignature())
        {
            OutNodeDef.MemberName = GetCleanClassName(Function->GetName());
            if (UClass* OwnerClass = Function->GetOwnerClass())
            {
                OutNodeDef.MemberParent = GetCleanClassName(OwnerClass->GetName());
            }
        }
        
        // Log call delegate details
        FString CallDelegateInfo = FString::Printf(TEXT("Call Delegate: %s, Signature: %s, Class: %s"),
            *OutNodeDef.Name,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(CallDelegateInfo, EN2CLogSeverity::Debug);
        return;
    }
}
