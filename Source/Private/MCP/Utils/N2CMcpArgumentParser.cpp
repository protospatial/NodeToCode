// Copyright Protospatial 2025. All Rights Reserved.

#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Kismet2/Kismet2NameValidators.h"

FN2CMcpArgumentParser::FN2CMcpArgumentParser(const TSharedPtr<FJsonObject>& InArguments)
    : ArgumentsJson(InArguments)
{
}

bool FN2CMcpArgumentParser::HasArgument(const FString& FieldName) const
{
    if (!ArgumentsJson.IsValid())
    {
        return false;
    }
    return ArgumentsJson->HasField(FieldName);
}

// String
bool FN2CMcpArgumentParser::TryGetRequiredString(const FString& FieldName, FString& OutValue, FString& OutErrorMsg, bool bAllowEmpty)
{
    if (!ArgumentsJson.IsValid())
    {
        OutErrorMsg = TEXT("Arguments JSON object is invalid.");
        return false;
    }
    if (!ArgumentsJson->HasField(FieldName))
    {
        OutErrorMsg = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
        return false;
    }
    // Only a JSON string: TryGetStringField would also turn a number or bool into text
    const TSharedPtr<FJsonValue> Value = ArgumentsJson->TryGetField(FieldName);
    if (!Value.IsValid() || Value->Type != EJson::String)
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' must be a string."), *FieldName);
        return false;
    }
    OutValue = Value->AsString();
    if (!bAllowEmpty && OutValue.IsEmpty())
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' cannot be empty."), *FieldName);
        return false;
    }
    return true;
}

bool FN2CMcpArgumentParser::TryGetRequiredName(const FString& FieldName, FString& OutValue, FString& OutErrorMsg)
{
    if (!TryGetRequiredString(FieldName, OutValue, OutErrorMsg))
    {
        return false;
    }
    if (OutValue.Len() > MaxNameLength)
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' is %d characters long; names are limited to %d characters."),
            *FieldName, OutValue.Len(), MaxNameLength);
        return false;
    }
    return true;
}

bool FN2CMcpArgumentParser::TryGetRequiredIdentifier(const FString& FieldName, FString& OutValue, FString& OutErrorMsg)
{
    return TryGetRequiredString(FieldName, OutValue, OutErrorMsg) && ValidateIdentifier(FieldName, OutValue, OutErrorMsg);
}

bool FN2CMcpArgumentParser::ValidateIdentifier(const FString& FieldName, const FString& Value, FString& OutErrorMsg)
{
    // FKismetNameValidator::IsValid(FString) and IsValid(FName), less the Blueprint's taken names
    const int32 MaxLength = FKismetNameValidator::GetMaximumNameLength();
    if (Value.IsEmpty() || FName(*Value.Left(MaxLength + 1)) == NAME_None)
    {
        OutErrorMsg = FString::Printf(TEXT("INVALID_ARGUMENT: Field '%s' must be a name; empty and 'None' are not names."), *FieldName);
        return false;
    }
    if (Value.Len() > MaxLength)
    {
        OutErrorMsg = FString::Printf(TEXT("INVALID_ARGUMENT: Field '%s' is %d characters long; Blueprint names are limited to %d characters."),
            *FieldName, Value.Len(), MaxLength);
        return false;
    }
    if (!FName::IsValidXName(Value, UE_BLUEPRINT_INVALID_NAME_CHARACTERS))
    {
        OutErrorMsg = FString::Printf(TEXT("INVALID_ARGUMENT: Field '%s' ('%s') contains a character Blueprint names cannot hold: %s"),
            *FieldName, *Value, UE_BLUEPRINT_INVALID_NAME_CHARACTERS);
        return false;
    }
    return true;
}

bool FN2CMcpArgumentParser::TryGetOptionalEnum(const FString& FieldName, const TArray<FString>& AllowedValues,
    const FString& DefaultValue, FString& OutValue, FString& OutErrorMsg) const
{
    const TSharedPtr<FJsonValue> Value = ArgumentsJson.IsValid() ? ArgumentsJson->TryGetField(FieldName) : nullptr;
    if (!Value.IsValid() || Value->Type == EJson::Null || Value->Type == EJson::None)
    {
        OutValue = DefaultValue;
        return true;
    }
    if (Value->Type == EJson::String)
    {
        const FString Given = Value->AsString();
        for (const FString& Allowed : AllowedValues)
        {
            if (Allowed.Equals(Given, ESearchCase::IgnoreCase))
            {
                OutValue = Allowed;
                return true;
            }
        }
        OutErrorMsg = FString::Printf(TEXT("INVALID_ARGUMENT: Field '%s' is '%s'; valid values: %s."),
            *FieldName, *Given, *FString::Join(AllowedValues, TEXT(", ")));
        return false;
    }
    OutErrorMsg = FString::Printf(TEXT("INVALID_ARGUMENT: Field '%s' must be a string, one of: %s."),
        *FieldName, *FString::Join(AllowedValues, TEXT(", ")));
    return false;
}

TArray<FString> FN2CMcpArgumentParser::FindUnknownFields(const TSharedPtr<FJsonObject>& Arguments, const TSharedPtr<FJsonObject>& InputSchema)
{
    TArray<FString> Unknown;
    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!Arguments.IsValid() || !InputSchema.IsValid())
    {
        return Unknown;
    }
    InputSchema->TryGetObjectField(TEXT("properties"), Properties);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Argument : Arguments->Values)
    {
        if (!Properties || !(*Properties).IsValid() || !(*Properties)->HasField(Argument.Key))
        {
            Unknown.Add(Argument.Key);
        }
    }
    return Unknown;
}

FString FN2CMcpArgumentParser::GetOptionalString(const FString& FieldName, const FString& DefaultValue) const
{
    if (!ArgumentsJson.IsValid()) return DefaultValue;
    FString Value;
    if (ArgumentsJson->TryGetStringField(FieldName, Value))
    {
        return Value;
    }
    return DefaultValue;
}

// Bool
bool FN2CMcpArgumentParser::TryGetRequiredBool(const FString& FieldName, bool& OutValue, FString& OutErrorMsg)
{
    if (!ArgumentsJson.IsValid())
    {
        OutErrorMsg = TEXT("Arguments JSON object is invalid.");
        return false;
    }
    if (!ArgumentsJson->HasField(FieldName))
    {
        OutErrorMsg = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
        return false;
    }
    if (!ArgumentsJson->TryGetBoolField(FieldName, OutValue))
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' must be a boolean."), *FieldName);
        return false;
    }
    return true;
}

bool FN2CMcpArgumentParser::GetOptionalBool(const FString& FieldName, bool DefaultValue) const
{
    if (!ArgumentsJson.IsValid()) return DefaultValue;
    bool Value;
    if (ArgumentsJson->TryGetBoolField(FieldName, Value))
    {
        return Value;
    }
    return DefaultValue;
}

// Number (double)
bool FN2CMcpArgumentParser::TryGetRequiredNumber(const FString& FieldName, double& OutValue, FString& OutErrorMsg)
{
    if (!ArgumentsJson.IsValid())
    {
        OutErrorMsg = TEXT("Arguments JSON object is invalid.");
        return false;
    }
    if (!ArgumentsJson->HasField(FieldName))
    {
        OutErrorMsg = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
        return false;
    }
    if (!ArgumentsJson->TryGetNumberField(FieldName, OutValue))
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' must be a number."), *FieldName);
        return false;
    }
    return true;
}

double FN2CMcpArgumentParser::GetOptionalNumber(const FString& FieldName, double DefaultValue) const
{
    if (!ArgumentsJson.IsValid()) return DefaultValue;
    double Value;
    if (ArgumentsJson->TryGetNumberField(FieldName, Value))
    {
        return Value;
    }
    return DefaultValue;
}

// Integer (int32)
bool FN2CMcpArgumentParser::TryGetRequiredInt(const FString& FieldName, int32& OutValue, FString& OutErrorMsg)
{
    if (!ArgumentsJson.IsValid())
    {
        OutErrorMsg = TEXT("Arguments JSON object is invalid.");
        return false;
    }
    if (!ArgumentsJson->HasField(FieldName))
    {
        OutErrorMsg = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
        return false;
    }
    if (!ArgumentsJson->TryGetNumberField(FieldName, OutValue)) // TryGetNumberField works for int32
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' must be an integer."), *FieldName);
        return false;
    }
    return true;
}

int32 FN2CMcpArgumentParser::GetOptionalInt(const FString& FieldName, int32 DefaultValue) const
{
    if (!ArgumentsJson.IsValid()) return DefaultValue;
    int32 Value;
    if (ArgumentsJson->TryGetNumberField(FieldName, Value))
    {
        return Value;
    }
    return DefaultValue;
}

// JsonObject (const TSharedPtr<FJsonObject>* for direct access)
// Implementation for GetOptionalObjectPtr
const TSharedPtr<FJsonObject>* FN2CMcpArgumentParser::GetOptionalObjectPtr(const FString& FieldName) const
{
    if (!ArgumentsJson.IsValid() || !ArgumentsJson->HasField(FieldName)) return nullptr;
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (ArgumentsJson->TryGetObjectField(FieldName, Value) && Value && (*Value).IsValid())
    {
        return Value;
    }
    return nullptr;
}

bool FN2CMcpArgumentParser::TryGetRequiredObject(const FString& FieldName, const TSharedPtr<FJsonObject>*& OutValue, FString& OutErrorMsg) const
{
    if (!ArgumentsJson.IsValid())
    {
        OutErrorMsg = TEXT("Arguments JSON object is invalid.");
        return false;
    }
    if (!ArgumentsJson->HasField(FieldName))
    {
        OutErrorMsg = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
        return false;
    }
    if (!ArgumentsJson->TryGetObjectField(FieldName, OutValue) || !OutValue || !(*OutValue).IsValid())
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' must be a valid JSON object."), *FieldName);
        return false;
    }
    return true;
}

// JsonObject (TSharedPtr<FJsonObject> for when a copy might be needed)
bool FN2CMcpArgumentParser::TryGetRequiredObject(const FString& FieldName, TSharedPtr<FJsonObject>& OutValue, FString& OutErrorMsg)
{
    const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
    // This TryGetRequiredObject call should now unambiguously call the one that takes (const FString&, const TSharedPtr<FJsonObject>*&, FString&)
    if (!TryGetRequiredObject(FieldName, ObjectPtr, OutErrorMsg)) 
    {
        return false;
    }
    OutValue = *ObjectPtr; // Dereference to get the TSharedPtr<FJsonObject>
    return true;
}

TSharedPtr<FJsonObject> FN2CMcpArgumentParser::GetOptionalObject(const FString& FieldName, TSharedPtr<FJsonObject> DefaultValue) const
{
    const TSharedPtr<FJsonObject>* ObjectPtr = GetOptionalObjectPtr(FieldName);
    if (ObjectPtr && (*ObjectPtr).IsValid())
    {
        return *ObjectPtr;
    }
    return DefaultValue;
}


// JsonArray
bool FN2CMcpArgumentParser::TryGetRequiredArray(const FString& FieldName, const TArray<TSharedPtr<FJsonValue>>*& OutValue, FString& OutErrorMsg) const
{
    if (!ArgumentsJson.IsValid())
    {
        OutErrorMsg = TEXT("Arguments JSON object is invalid.");
        return false;
    }
    if (!ArgumentsJson->HasField(FieldName))
    {
        OutErrorMsg = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
        return false;
    }
    if (!ArgumentsJson->TryGetArrayField(FieldName, OutValue) || !OutValue)
    {
        OutErrorMsg = FString::Printf(TEXT("Field '%s' must be a valid JSON array."), *FieldName);
        return false;
    }
    return true;
}

const TArray<TSharedPtr<FJsonValue>>* FN2CMcpArgumentParser::GetOptionalArray(const FString& FieldName) const
{
    if (!ArgumentsJson.IsValid() || !ArgumentsJson->HasField(FieldName)) return nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
    if (ArgumentsJson->TryGetArrayField(FieldName, Value))
    {
        return Value;
    }
    return nullptr;
}
