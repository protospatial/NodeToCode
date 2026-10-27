// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CArrayProcessor.h"
#include "Utils/N2CStableNames.h"

namespace
{
    /** One side of a map as N2C JSON names it: its struct, enum or class when it has one, else its type */
    FString TerminalTypeName(const FName& Category, const FName& SubCategory, const UObject* SubCategoryObject)
    {
        if (SubCategoryObject)
        {
            return N2CStableNames::TypeObjectName(SubCategoryObject);
        }
        return StaticEnum<EN2CPinType>()->GetNameStringByValue(
            static_cast<int64>(N2CStableNames::PinType(Category, SubCategory, SubCategoryObject)));
    }
}

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
                    OutNodeDef.MemberName = Pin->PinType.PinSubCategory.ToString();
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = N2CStableNames::TypeObjectName(Pin->PinType.PinSubCategoryObject.Get());
                }
                
                // Log array element type
                N2C_LOG(Debug, TEXT("Make Array: %s, Element Type: %s"),
                    *OutNodeDef.Title,
                    *OutNodeDef.MemberName);
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
                // The main pin type is the key and PinValueType the value (card 0238, translation-core-13)
                const FEdGraphPinType& Type = Pin->PinType;
                const FString KeyType = TerminalTypeName(Type.PinCategory, Type.PinSubCategory,
                    Type.PinSubCategoryObject.Get());
                const FString ValueType = TerminalTypeName(Type.PinValueType.TerminalCategory,
                    Type.PinValueType.TerminalSubCategory, Type.PinValueType.TerminalSubCategoryObject.Get());
                
                OutNodeDef.MemberName = FString::Printf(TEXT("%s to %s"), *KeyType, *ValueType);
                
                // Log map key/value types
                N2C_LOG(Debug, TEXT("Make Map: %s, Key Type: %s, Value Type: %s"),
                    *OutNodeDef.Title,
                    *KeyType,
                    *ValueType);
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
                    OutNodeDef.MemberName = Pin->PinType.PinSubCategory.ToString();
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = N2CStableNames::TypeObjectName(Pin->PinType.PinSubCategoryObject.Get());
                }
                
                // Log set element type
                N2C_LOG(Debug, TEXT("Make Set: %s, Element Type: %s"),
                    *OutNodeDef.Title,
                    *OutNodeDef.MemberName);
                break;
            }
        }
        return;
    }
    
    // Handle get array item nodes
    if (UK2Node_GetArrayItem* GetArrayItemNode = Cast<UK2Node_GetArrayItem>(Node))
    {
        // The element type, from the array pin (named "Array" by the engine; the processor looked for "TargetArray" and
        // never found it, translation-core-29), in pin type spellings: the struct, enum, class or object type when there
        // is one, else the type ("Integer"). The title hint says whether it returns a copy or a reference.
        if (const UEdGraphPin* ArrayPin = GetArrayItemNode->GetTargetArrayPin())
        {
            const FEdGraphPinType& Type = ArrayPin->PinType;
            OutNodeDef.MemberName = Type.PinSubCategoryObject.IsValid()
                ? N2CStableNames::TypeObjectName(Type.PinSubCategoryObject.Get())
                : StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(
                    N2CStableNames::PinType(Type.PinCategory, Type.PinSubCategory, nullptr)));
            N2C_LOG(Debug, TEXT("Get Array Item: %s, Element Type: %s"), *OutNodeDef.Title, *OutNodeDef.MemberName);
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
                    OutNodeDef.MemberName = Pin->PinType.PinSubCategory.ToString();
                }
                else if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    OutNodeDef.MemberName = N2CStableNames::TypeObjectName(Pin->PinType.PinSubCategoryObject.Get());
                }
                
                // Log container type
                N2C_LOG(Debug, TEXT("Make Container: %s, Type: %s, Element Type: %s"),
                    *OutNodeDef.Title,
                    *ContainerType,
                    *OutNodeDef.MemberName);
                break;
            }
        }
        return;
    }
}
