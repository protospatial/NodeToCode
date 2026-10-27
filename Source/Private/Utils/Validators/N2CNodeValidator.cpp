// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CNodeValidator.h"

#include "Models/N2CJsonContract.h"

bool FN2CNodeValidator::Validate(const FN2CNodeDefinition& Node, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    const bool bValid = Validate(Node, FString(), Diagnostics);
    if (!bValid)
    {
        OutError = N2CDiagnostics::FirstError(Diagnostics);
    }
    return bValid;
}

bool FN2CNodeValidator::Validate(const FN2CNodeDefinition& Node, const FString& Path, TArray<FN2CDiagnostic>& OutDiagnostics)
{
    using namespace N2CJsonContract;
    N2C_LOG(Debug, TEXT("Validating Node: ID=%s, Title=%s, Type=%s, MemberParent=%s, MemberName=%s"),
        *Node.ID,
        *Node.Title,
        *StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(Node.NodeType)),
        *Node.MemberParent,
        *Node.MemberName);

    // A node without an ID cannot be named by any flow. The title is a hint, written only when it says more than
    // member_name (card 0138), so it may be empty.
    if (Node.ID.IsEmpty())
    {
        N2CDiagnostics::Add(OutDiagnostics, EN2CDiagnosticSeverity::Error, Diagnostic::MissingId, Path,
            FString::Printf(TEXT("Node validation failed: Empty ID for node %s"), *Node.Title));
        return false;
    }

    // A node type this build does not know (a later 2.x document, card 0445) is accepted with a warning. A captured
    // node class with no mapping (card 0234: "Unknown" with its "class") is expected, for example a plugin's node
    if (Node.NodeType == EN2CNodeType::Unknown)
    {
        if (!Node.UnknownType.IsEmpty())
        {
            N2CDiagnostics::Add(OutDiagnostics, EN2CDiagnosticSeverity::Warning, Diagnostic::UnknownKind, Path + TEXT(".type"),
                FString::Printf(TEXT("Node %s has a node type this build does not know: '%s'"), *Node.ID, *Node.UnknownType));
        }
        else
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("Node %s is of a node class with no N2C type: '%s'"),
                *Node.ID, *Node.NodeClass), EN2CLogSeverity::Info);
        }
    }

    // Flag combinations a capture never writes: a reader can still use the node, so warnings (card 0240)
    if (Node.bPure && Node.bLatent)
    {
        N2CDiagnostics::Add(OutDiagnostics, EN2CDiagnosticSeverity::Warning, Diagnostic::NodeFlags, Path,
            FString::Printf(TEXT("Node %s (%s) cannot be both pure and latent"), *Node.ID, *Node.Title));
    }
    if (Node.bPure && HasExecPins(Node) && Node.NodeType != EN2CNodeType::Knot)
    {
        N2CDiagnostics::Add(OutDiagnostics, EN2CDiagnosticSeverity::Warning, Diagnostic::NodeFlags, Path,
            FString::Printf(TEXT("Pure node %s (%s) has exec pins"), *Node.ID, *Node.Title));
    }

    // Pins: their findings are warnings; a pin ID used twice on the node is an error (flows could not tell them apart)
    bool bValid = true;
    TSet<FString> PinIds;
    const auto CheckPins = [&](const TArray<FN2CPinDefinition>& Pins, const TCHAR* Field)
    {
        for (int32 Index = 0; Index < Pins.Num(); ++Index)
        {
            const FN2CPinDefinition& Pin = Pins[Index];
            const FString PinPath = FString::Printf(TEXT("%s.%s[%d]"), *Path, Field, Index);
            N2C_LOG(Debug, TEXT("Validating %s pin %s (%s) on node %s"), Field, *Pin.ID, *Pin.Name, *Node.ID);
            PinValidator.Validate(Pin, PinPath, OutDiagnostics);
            if (!Pin.ID.IsEmpty() && PinIds.Contains(Pin.ID))
            {
                N2CDiagnostics::Add(OutDiagnostics, EN2CDiagnosticSeverity::Error, Diagnostic::DuplicateId, PinPath,
                    FString::Printf(TEXT("Duplicate pin ID %s found on node %s"), *Pin.ID, *Node.ID));
                bValid = false;
            }
            PinIds.Add(Pin.ID);
        }
    };
    CheckPins(Node.InputPins, TEXT("input_pins"));
    CheckPins(Node.OutputPins, TEXT("output_pins"));

    if (bValid)
    {
        N2C_LOG(Debug, TEXT("Node %s (%s) validation successful"), *Node.ID, *Node.Title);
    }
    return bValid;
}

bool FN2CNodeValidator::HasExecPins(const FN2CNodeDefinition& Node) const
{
    const auto IsExec = [](const FN2CPinDefinition& Pin) { return Pin.Type == EN2CPinType::Exec; };
    return Node.InputPins.ContainsByPredicate(IsExec) || Node.OutputPins.ContainsByPredicate(IsExec);
}
