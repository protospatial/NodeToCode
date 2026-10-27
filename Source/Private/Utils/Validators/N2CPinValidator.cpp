// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CPinValidator.h"

#include "Models/N2CJsonContract.h"

namespace N2CDiagnostics
{
	void Add(TArray<FN2CDiagnostic>& Out, const EN2CDiagnosticSeverity Severity, const TCHAR* Code, const FString& Path,
		const FString& Message)
	{
		if (Severity == EN2CDiagnosticSeverity::Error)
		{
			FN2CLogger::Get().LogError(Message);
		}
		else
		{
			FN2CLogger::Get().LogWarning(Message);
		}
		FN2CDiagnostic& Diagnostic = Out.AddDefaulted_GetRef();
		Diagnostic.Severity = Severity;
		Diagnostic.Code = Code;
		Diagnostic.Path = Path;
		Diagnostic.Message = Message;
	}

	bool HasError(const TArray<FN2CDiagnostic>& Diagnostics)
	{
		return Diagnostics.ContainsByPredicate([](const FN2CDiagnostic& Diagnostic) { return Diagnostic.IsError(); });
	}

	FString FirstError(const TArray<FN2CDiagnostic>& Diagnostics)
	{
		const FN2CDiagnostic* Error = Diagnostics.FindByPredicate([](const FN2CDiagnostic& Diagnostic) { return Diagnostic.IsError(); });
		return Error ? Error->Message : FString();
	}
}

namespace
{
	void PinWarn(TArray<FN2CDiagnostic>& Out, const FString& Path, const FString& Message,
		const TCHAR* Code = N2CJsonContract::Diagnostic::PinShape)
	{
		N2CDiagnostics::Add(Out, EN2CDiagnosticSeverity::Warning, Code, Path, Message);
	}
}

bool FN2CPinValidator::Validate(const FN2CPinDefinition& Pin, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    const bool bValid = Validate(Pin, FString(), Diagnostics);
    if (Diagnostics.Num() > 0)
    {
        OutError = Diagnostics.Last().Message;
    }
    return bValid;
}

bool FN2CPinValidator::Validate(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& OutDiagnostics)
{
    // Log basic pin info
    N2C_LOG(Debug, TEXT("Validating Pin: ID=%s, Name=%s, Type=%s, SubType=%s"),
        *Pin.ID,
        *Pin.Name,
        *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)),
        *Pin.SubType);

    ValidateRequired(Pin, Path, OutDiagnostics);
    ValidateTypeSpecific(Pin, Path, OutDiagnostics);
    ValidateContainerFlags(Pin, Path, OutDiagnostics);

    N2C_LOG(Debug, TEXT("Pin %s flags: Connected=%d, IsRef=%d, IsConst=%d, IsArray=%d, IsMap=%d, IsSet=%d"),
        *Pin.ID,
        Pin.bConnected ? 1 : 0,
        Pin.bIsReference ? 1 : 0,
        Pin.bIsConst ? 1 : 0,
        Pin.bIsArray ? 1 : 0,
        Pin.bIsMap ? 1 : 0,
        Pin.bIsSet ? 1 : 0);

    // A pin never fails a document: every pin finding is a warning (card 0240)
    return true;
}

void FN2CPinValidator::ValidateRequired(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    if (Pin.ID.IsEmpty())
    {
        PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Empty ID for pin %s"), *Pin.Name));
    }

    // Allow empty names for all pins
    if (Pin.Name.IsEmpty())
    {
        N2C_LOG(Debug, TEXT("Pin %s has empty name"), *Pin.ID);
    }
}

void FN2CPinValidator::ValidateTypeSpecific(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    const FString TypeName = StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type));
    switch (Pin.Type)
    {
        case EN2CPinType::Array:
        case EN2CPinType::Set:
        case EN2CPinType::Map:
            // Container types require valid SubType
            if (Pin.SubType.IsEmpty())
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Container type %s missing SubType for pin %s"), *TypeName, *Pin.ID));
            }
            // Container flags must match type
            if (Pin.Type == EN2CPinType::Array && !Pin.bIsArray)
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Array type without array flag for pin %s"), *Pin.ID));
            }
            if (Pin.Type == EN2CPinType::Map && !Pin.bIsMap)
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Map type without map flag for pin %s"), *Pin.ID));
            }
            if (Pin.Type == EN2CPinType::Set && !Pin.bIsSet)
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Set type without set flag for pin %s"), *Pin.ID));
            }
            break;

        case EN2CPinType::Struct:
        case EN2CPinType::Object:
        case EN2CPinType::Class:
        case EN2CPinType::Interface:
            // These types require valid SubType for type information
            if (Pin.SubType.IsEmpty())
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: %s type missing SubType for pin %s"), *TypeName, *Pin.ID));
            }
            break;

        case EN2CPinType::Exec:
            // Exec pins can't have default values or be const/reference
            if (!Pin.DefaultValue.IsEmpty() || Pin.bIsConst || Pin.bIsReference)
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Invalid Exec pin configuration for pin %s"), *Pin.ID));
            }
            break;

        case EN2CPinType::Delegate:
        case EN2CPinType::MulticastDelegate:
            // Delegates can't be const
            if (Pin.bIsConst)
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Const delegate pin %s"), *Pin.ID));
            }
            break;

        case EN2CPinType::SoftObject:
        case EN2CPinType::SoftClass:
            // Soft references require class path in SubType
            if (Pin.SubType.IsEmpty())
            {
                PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Soft reference missing class path for pin %s"), *Pin.ID));
            }
            break;

        case EN2CPinType::Unknown:
            // A pin type this build does not know (a later 2.x document, card 0445): accepted with a warning
            PinWarn(Out, Path, FString::Printf(TEXT("Pin %s has a pin type this build does not know: '%s'"), *Pin.ID, *Pin.UnknownType),
                N2CJsonContract::Diagnostic::UnknownKind);
            break;

        default:
            N2C_LOG(Debug, TEXT("Pin %s has standard type %s"), *Pin.ID, *TypeName);
            break;
    }
}

void FN2CPinValidator::ValidateContainerFlags(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    // Only one container type allowed; the LLM will need to determine the most appropriate one
    if ((Pin.bIsArray && Pin.bIsMap) || (Pin.bIsArray && Pin.bIsSet) || (Pin.bIsMap && Pin.bIsSet))
    {
        PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Pin %s has multiple container types"), *Pin.ID));
    }

    // Only a map has a value type (card 0238): "type" is then its key's
    if (Pin.HasValueType() && !Pin.bIsMap)
    {
        PinWarn(Out, Path, FString::Printf(TEXT("Pin validation warning: Pin %s has a value_type but is not a map"), *Pin.ID));
    }

    // Const and reference validation - allow for certain types
    if (Pin.bIsConst && Pin.bIsReference)
    {
        N2C_LOG(Debug, TEXT("Pin %s is both const and reference - this is valid for certain engine types"), *Pin.ID);
    }
}
