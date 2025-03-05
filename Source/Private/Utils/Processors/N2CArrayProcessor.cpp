// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CArrayProcessor.h"

void FN2CArrayProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle make array nodes
    if (UK2Node_MakeArray* MakeArrayNode = Cast<UK2Node_MakeArray>(Node))
    {
        // Get array element type from output pin
        for (UEdGraphPin* Pin : MakeArrayNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                if (Pin->PinType.PinSubCategory != NAME_None)
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategory.ToString());
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
                }
                
                // Log array element type
                FString ArrayInfo = FString::Printf(TEXT("Make Array: %s, Element Type: %s"),
                    *OutNodeDef.Name,
                    *OutNodeDef.MemberName);
                FN2CLogger::Get().Log(ArrayInfo, EN2CLogSeverity::Debug);
                break;
            }
        }
        return;
    }
    
    // Handle make map nodes
    if (UK2Node_MakeMap* MakeMapNode = Cast<UK2Node_MakeMap>(Node))
    {
        // Get key and value types from output pin
        for (UEdGraphPin* Pin : MakeMapNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                FString KeyType, ValueType;
                
                // Try to get key and value types
                if (Pin->PinType.PinValueType.TerminalCategory != NAME_None)
                {
                    KeyType = GetCleanClassName(Pin->PinType.PinValueType.TerminalCategory.ToString());
                }
                
                if (Pin->PinType.PinSubCategory != NAME_None)
                {
                    ValueType = GetCleanClassName(Pin->PinType.PinSubCategory.ToString());
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    ValueType = GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
                }
                
                OutNodeDef.MemberName = FString::Printf(TEXT("%s to %s"), *KeyType, *ValueType);
                
                // Log map key/value types
                FString MapInfo = FString::Printf(TEXT("Make Map: %s, Key Type: %s, Value Type: %s"),
                    *OutNodeDef.Name,
                    *KeyType,
                    *ValueType);
                FN2CLogger::Get().Log(MapInfo, EN2CLogSeverity::Debug);
                break;
            }
        }
        return;
    }
    
    // Handle make set nodes
    if (UK2Node_MakeSet* MakeSetNode = Cast<UK2Node_MakeSet>(Node))
    {
        // Get set element type from output pin
        for (UEdGraphPin* Pin : MakeSetNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                if (Pin->PinType.PinSubCategory != NAME_None)
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategory.ToString());
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
                }
                
                // Log set element type
                FString SetInfo = FString::Printf(TEXT("Make Set: %s, Element Type: %s"),
                    *OutNodeDef.Name,
                    *OutNodeDef.MemberName);
                FN2CLogger::Get().Log(SetInfo, EN2CLogSeverity::Debug);
                break;
            }
        }
        return;
    }
    
    // Handle get array item nodes
    if (UK2Node_GetArrayItem* GetArrayItemNode = Cast<UK2Node_GetArrayItem>(Node))
    {
        // Get array element type from input pin
        for (UEdGraphPin* Pin : GetArrayItemNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == TEXT("TargetArray"))
            {
                if (Pin->PinType.PinSubCategory != NAME_None)
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategory.ToString());
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
                }
                
                // Log array element type
                FString ArrayInfo = FString::Printf(TEXT("Get Array Item: %s, Element Type: %s"),
                    *OutNodeDef.Name,
                    *OutNodeDef.MemberName);
                FN2CLogger::Get().Log(ArrayInfo, EN2CLogSeverity::Debug);
                break;
            }
        }
        return;
    }
    
    // Handle make container nodes
    if (UK2Node_MakeContainer* MakeContainerNode = Cast<UK2Node_MakeContainer>(Node))
    {
        // Get container type from node class
        FString ContainerType = TEXT("Unknown");
        if (MakeContainerNode->IsA<UK2Node_MakeArray>())
        {
            ContainerType = TEXT("Array");
        }
        else if (MakeContainerNode->IsA<UK2Node_MakeMap>())
        {
            ContainerType = TEXT("Map");
        }
        else if (MakeContainerNode->IsA<UK2Node_MakeSet>())
        {
            ContainerType = TEXT("Set");
        }
        
        // Get element type from output pin
        for (UEdGraphPin* Pin : MakeContainerNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                if (Pin->PinType.PinSubCategory != NAME_None)
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategory.ToString());
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
                }
                
                // Log container type
                FString ContainerInfo = FString::Printf(TEXT("Make Container: %s, Type: %s, Element Type: %s"),
                    *OutNodeDef.Name,
                    *ContainerType,
                    *OutNodeDef.MemberName);
                FN2CLogger::Get().Log(ContainerInfo, EN2CLogSeverity::Debug);
                break;
            }
        }
        return;
    }
}
