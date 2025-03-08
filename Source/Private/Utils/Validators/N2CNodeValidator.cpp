// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CNodeValidator.h"

bool FN2CNodeValidator::Validate(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Log node basic info
    FString NodeInfo = FString::Printf(TEXT("Validating Node: ID=%s, Name=%s, Type=%s, MemberParent=%s, MemberName=%s"),
        *Node.ID,
        *Node.Name,
        *StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(Node.NodeType)),
        *Node.MemberParent,
        *Node.MemberName);
    FN2CLogger::Get().Log(NodeInfo, EN2CLogSeverity::Debug);

    // Validate required fields
    if (!ValidateRequired(Node, OutError))
    {
        return false;
    }

    // Validate node flags
    if (!ValidateFlags(Node, OutError))
    {
        return false;
    }

    // Validate pins
    if (!ValidatePins(Node, OutError))
    {
        return false;
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Node %s (%s) validation successful"), *Node.ID, *Node.Name), EN2CLogSeverity::Debug);
    return true;
}

bool FN2CNodeValidator::ValidateRequired(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Check required fields
    if (Node.ID.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Node validation failed: Empty ID for node %s"), *Node.Name);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    if (Node.Name.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Node validation failed: Empty Name for node %s"), *Node.ID);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    return true;
}

bool FN2CNodeValidator::ValidateFlags(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Validate pure/latent combinations
    if (Node.bPure && Node.bLatent)
    {
        OutError = FString::Printf(TEXT("Node validation failed: Node %s (%s) cannot be both pure and latent"), *Node.ID, *Node.Name);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Pure nodes shouldn't have exec pins except for knot nodes
    if (Node.bPure && HasExecPins(Node) && Node.NodeType != EN2CNodeType::Knot)
    {
        OutError = FString::Printf(TEXT("Node validation failed: Pure node %s (%s) has exec pins"), *Node.ID, *Node.Name);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    return true;
}

bool FN2CNodeValidator::ValidatePins(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Log pin counts
    FString PinInfo = FString::Printf(TEXT("Node %s (%s) has %d input pins and %d output pins"),
        *Node.ID,
        *Node.Name,
        Node.InputPins.Num(),
        Node.OutputPins.Num());
    FN2CLogger::Get().Log(PinInfo, EN2CLogSeverity::Debug);

    // Check for duplicate pin IDs
    TSet<FString> PinIds;
    
    // Check input pins
    for (const FN2CPinDefinition& Pin : Node.InputPins)
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("Validating input pin %s (%s) on node %s"), *Pin.ID, *Pin.Name, *Node.ID), EN2CLogSeverity::Debug);
        
        FString PinError;
        if (!PinValidator.Validate(Pin, PinError))
        {
            OutError = FString::Printf(TEXT("Invalid input pin %s (%s) on node %s: %s"), *Pin.ID, *Pin.Name, *Node.ID, *PinError);
            return false;
        }

        if (PinIds.Contains(Pin.ID))
        {
            OutError = FString::Printf(TEXT("Duplicate pin ID %s found on node %s"), *Pin.ID, *Node.ID);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
        PinIds.Add(Pin.ID);
    }

    // Check output pins
    for (const FN2CPinDefinition& Pin : Node.OutputPins)
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("Validating output pin %s (%s) on node %s"), *Pin.ID, *Pin.Name, *Node.ID), EN2CLogSeverity::Debug);
        
        FString PinError;
        if (!PinValidator.Validate(Pin, PinError))
        {
            OutError = FString::Printf(TEXT("Invalid output pin %s (%s) on node %s: %s"), *Pin.ID, *Pin.Name, *Node.ID, *PinError);
            return false;
        }

        if (PinIds.Contains(Pin.ID))
        {
            OutError = FString::Printf(TEXT("Duplicate pin ID %s found on node %s"), *Pin.ID, *Node.ID);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
        PinIds.Add(Pin.ID);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("All pins validated successfully for node %s"), *Node.ID), EN2CLogSeverity::Debug);
    return true;
}

bool FN2CNodeValidator::HasExecPins(const FN2CNodeDefinition& Node) const
{
    bool hasExecInput = false;
    bool hasExecOutput = false;

    // Check input pins
    for (const FN2CPinDefinition& Pin : Node.InputPins)
    {
        if (Pin.Type == EN2CPinType::Exec)
        {
            hasExecInput = true;
            FN2CLogger::Get().Log(FString::Printf(TEXT("Node %s (%s) has exec input pin: %s"), *Node.ID, *Node.Name, *Pin.Name), EN2CLogSeverity::Debug);
            break;
        }
    }

    // Check output pins
    for (const FN2CPinDefinition& Pin : Node.OutputPins)
    {
        if (Pin.Type == EN2CPinType::Exec)
        {
            hasExecOutput = true;
            FN2CLogger::Get().Log(FString::Printf(TEXT("Node %s (%s) has exec output pin: %s"), *Node.ID, *Node.Name, *Pin.Name), EN2CLogSeverity::Debug);
            break;
        }
    }

    if (hasExecInput || hasExecOutput)
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("Node %s (%s) exec pins: Input=%d, Output=%d"), 
            *Node.ID, *Node.Name, hasExecInput ? 1 : 0, hasExecOutput ? 1 : 0), EN2CLogSeverity::Debug);
    }

    return hasExecInput && hasExecOutput;
}
