// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CDelegateProcessor.h"
#include "Utils/N2CStableNames.h"

void FN2CDelegateProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle base multicast delegate nodes
    if (UK2Node_BaseMCDelegate* DelegateNode = Cast<UK2Node_BaseMCDelegate>(Node))
    {
        OutNodeDef.MemberName = DelegateNode->DelegateReference.GetMemberName().ToString();
        if (UClass* DelegateClass = DelegateNode->DelegateReference.GetMemberParentClass())
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(DelegateClass);
        }
        
        // Log delegate details
        N2C_LOG(Debug, TEXT("Delegate Node: %s, Member: %s, Parent: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        return;
    }
    
    // Handle create delegate nodes
    if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
    {
        OutNodeDef.MemberName = CreateDelegateNode->GetFunctionName().ToString();
        
        // Try to get the scope class first since it's more reliable
        if (UClass* ScopeClass = CreateDelegateNode->GetScopeClass())
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(ScopeClass);
        }
        // Fallback to delegate signature if scope class isn't available
        else if (UFunction* DelegateSignature = CreateDelegateNode->GetDelegateSignature())
        {
            if (UClass* OwnerClass = DelegateSignature->GetOwnerClass())
            {
                OutNodeDef.MemberParent = N2CStableNames::ClassName(OwnerClass);
            }
        }
        
        // Log create delegate details
        N2C_LOG(Debug, TEXT("Create Delegate: %s, Function: %s, Class: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        return;
    }
    
    // Handle call delegate nodes
    if (UK2Node_CallDelegate* CallDelegateNode = Cast<UK2Node_CallDelegate>(Node))
    {
        if (UFunction* Function = CallDelegateNode->GetDelegateSignature())
        {
            OutNodeDef.MemberName = Function->GetName();
            if (UClass* OwnerClass = Function->GetOwnerClass())
            {
                OutNodeDef.MemberParent = N2CStableNames::ClassName(OwnerClass);
            }
        }
        
        // Log call delegate details
        N2C_LOG(Debug, TEXT("Call Delegate: %s, Signature: %s, Class: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        return;
    }
}
