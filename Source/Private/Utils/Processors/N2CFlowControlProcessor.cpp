// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CFlowControlProcessor.h"

#include "K2Node_SwitchEnum.h"
#include "Utils/N2CStableNames.h"

void FN2CFlowControlProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle sequence nodes
    if (Node->IsA<UK2Node_ExecutionSequence>())
    {
        OutNodeDef.Title = TEXT("Sequence");
        
        // Log sequence details
        N2C_LOG(Debug, TEXT("Sequence Node: %s"), *OutNodeDef.ID);
        return;
    }
    
    // Handle branch (if-then-else) nodes
    if (Node->IsA<UK2Node_IfThenElse>())
    {
        OutNodeDef.Title = TEXT("Branch");
        
        // Log branch details
        N2C_LOG(Debug, TEXT("Branch Node: %s"), *OutNodeDef.ID);
        return;
    }
    
    // Handle select nodes
    if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
    {
        OutNodeDef.Title = TEXT("Select");
        
        // Get option pins manually since GetOptionPins might not be properly exported
        TArray<UEdGraphPin*> OptionPins;
        for (UEdGraphPin* Pin : SelectNode->Pins)
        {
            // Option pins are typically input pins that aren't the selector or exec pins
            if (Pin && Pin->Direction == EGPD_Input && 
                Pin->PinName != TEXT("Index") && 
                Pin->PinType.PinCategory != TEXT("exec"))
            {
                OptionPins.Add(Pin);
            }
        }
        
        // Log select details
        N2C_LOG(Debug, TEXT("Select Node: %s, Options: %d"),
            *OutNodeDef.ID,
            OptionPins.Num());
        return;
    }
    
    // Handle switch nodes
    if (UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node))
    {
        // The engine's title stays as the hint ("Switch on E_WeaponType"); member_name is the switched enum, or the
        // switched type in pin type spellings ("Integer", "String", "Name") (card 0239, translation-core-32)
        if (const UK2Node_SwitchEnum* SwitchEnum = Cast<UK2Node_SwitchEnum>(SwitchNode))
        {
            if (SwitchEnum->Enum)
            {
                OutNodeDef.MemberName = N2CStableNames::TypeObjectName(SwitchEnum->Enum);
            }
        }
        else if (const UEdGraphPin* Selection = SwitchNode->GetSelectionPin())
        {
            const FEdGraphPinType& Type = Selection->PinType;
            OutNodeDef.MemberName = Type.PinSubCategoryObject.IsValid()
                ? N2CStableNames::TypeObjectName(Type.PinSubCategoryObject.Get())
                : StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(
                    N2CStableNames::PinType(Type.PinCategory, Type.PinSubCategory, nullptr)));
        }
        N2C_LOG(Debug, TEXT("Switch Node: %s on %s"), *OutNodeDef.ID, *OutNodeDef.MemberName);
        return;
    }
    
    // Handle multi-gate nodes
    if (UK2Node_MultiGate* MultiGateNode = Cast<UK2Node_MultiGate>(Node))
    {
        OutNodeDef.Title = TEXT("Multi Gate");
        
        // Get output pins manually since GetOutPins might not be properly exported
        TArray<UEdGraphPin*> OutPins;
        for (UEdGraphPin* Pin : MultiGateNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                OutPins.Add(Pin);
            }
        }
        
        // Log multi-gate details
        N2C_LOG(Debug, TEXT("Multi Gate Node: %s, Outputs: %d"),
            *OutNodeDef.ID,
            OutPins.Num());
        return;
    }
    
    // Handle do-once-multi-input nodes
    if (Node->IsA<UK2Node_DoOnceMultiInput>())
    {
        OutNodeDef.Title = TEXT("Do Once Multi Input");
        
        // Log do-once details
        N2C_LOG(Debug, TEXT("Do Once Multi Input Node: %s"), *OutNodeDef.ID);
        return;
    }
}
