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
        FString DelegateInfo = FString::Printf(TEXT("Delegate Node: %s, Member: %s, Parent: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(DelegateInfo, EN2CLogSeverity::Debug);
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
        FString CreateDelegateInfo = FString::Printf(TEXT("Create Delegate: %s, Function: %s, Class: %s"),
            *OutNodeDef.Title,
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
            OutNodeDef.MemberName = Function->GetName();
            if (UClass* OwnerClass = Function->GetOwnerClass())
            {
                OutNodeDef.MemberParent = N2CStableNames::ClassName(OwnerClass);
            }
        }
        
        // Log call delegate details
        FString CallDelegateInfo = FString::Printf(TEXT("Call Delegate: %s, Signature: %s, Class: %s"),
            *OutNodeDef.Title,
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(CallDelegateInfo, EN2CLogSeverity::Debug);
        return;
    }
}
