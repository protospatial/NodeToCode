// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CNodeValidator.h"

bool FN2CNodeValidator::Validate(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Log node basic info
    N2C_LOG(Debug, TEXT("Validating Node: ID=%s, Title=%s, Type=%s, MemberParent=%s, MemberName=%s"),
        *Node.ID,
        *Node.Title,
        *StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(Node.NodeType)),
        *Node.MemberParent,
        *Node.MemberName);

    // Validate required fields
    if (!ValidateRequired(Node, OutError))
    {
        return false;
    }

    // A node type this build does not know (a later 2.x document, card 0445) is accepted with a warning. A captured
    // node class with no mapping (card 0234: "Unknown" with its "class") is expected, for example a plugin's node
    if (Node.NodeType == EN2CNodeType::Unknown)
    {
        if (!Node.UnknownType.IsEmpty())
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Node %s has a node type this build does not know: '%s'"),
                *Node.ID, *Node.UnknownType));
        }
        else
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("Node %s is of a node class with no N2C type: '%s'"),
                *Node.ID, *Node.NodeClass), EN2CLogSeverity::Info);
        }
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

    N2C_LOG(Debug, TEXT("Node %s (%s) validation successful"), *Node.ID, *Node.Title);
    return true;
}

bool FN2CNodeValidator::ValidateRequired(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Check required fields
    if (Node.ID.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Node validation failed: Empty ID for node %s"), *Node.Title);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // The title is a hint, written only when it says more than member_name (card 0138), so it may be empty

    return true;
}

bool FN2CNodeValidator::ValidateFlags(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Validate pure/latent combinations
    if (Node.bPure && Node.bLatent)
    {
        OutError = FString::Printf(TEXT("Node validation failed: Node %s (%s) cannot be both pure and latent"), *Node.ID, *Node.Title);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Pure nodes shouldn't have exec pins except for knot nodes
    if (Node.bPure && HasExecPins(Node) && Node.NodeType != EN2CNodeType::Knot)
    {
        OutError = FString::Printf(TEXT("Node validation failed: Pure node %s (%s) has exec pins"), *Node.ID, *Node.Title);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    return true;
}

bool FN2CNodeValidator::ValidatePins(const FN2CNodeDefinition& Node, FString& OutError)
{
    // Log pin counts
    N2C_LOG(Debug, TEXT("Node %s (%s) has %d input pins and %d output pins"),
        *Node.ID,
        *Node.Title,
        Node.InputPins.Num(),
        Node.OutputPins.Num());

    // Check for duplicate pin IDs
    TSet<FString> PinIds;
    
    // Check input pins
    for (const FN2CPinDefinition& Pin : Node.InputPins)
    {
        N2C_LOG(Debug, TEXT("Validating input pin %s (%s) on node %s"), *Pin.ID, *Pin.Name, *Node.ID);
        
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
        N2C_LOG(Debug, TEXT("Validating output pin %s (%s) on node %s"), *Pin.ID, *Pin.Name, *Node.ID);
        
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

    N2C_LOG(Debug, TEXT("All pins validated successfully for node %s"), *Node.ID);
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
            N2C_LOG(Debug, TEXT("Node %s (%s) has exec input pin: %s"), *Node.ID, *Node.Title, *Pin.Name);
            break;
        }
    }

    // Check output pins
    for (const FN2CPinDefinition& Pin : Node.OutputPins)
    {
        if (Pin.Type == EN2CPinType::Exec)
        {
            hasExecOutput = true;
            N2C_LOG(Debug, TEXT("Node %s (%s) has exec output pin: %s"), *Node.ID, *Node.Title, *Pin.Name);
            break;
        }
    }

    if (hasExecInput || hasExecOutput)
    {
        N2C_LOG(Debug, TEXT("Node %s (%s) exec pins: Input=%d, Output=%d"),
            *Node.ID, *Node.Title, hasExecInput ? 1 : 0, hasExecOutput ? 1 : 0);
    }

    return hasExecInput && hasExecOutput;
}
