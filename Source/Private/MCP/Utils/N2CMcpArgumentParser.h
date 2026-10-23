// Copyright Protospatial 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FN2CMcpArgumentParser
{
public:
    explicit FN2CMcpArgumentParser(const TSharedPtr<FJsonObject>& InArguments);

    bool HasArgument(const FString& FieldName) const;

    // String
    bool TryGetRequiredString(const FString& FieldName, FString& OutValue, FString& OutErrorMsg, bool bAllowEmpty = false);

    /** Longest value accepted for a field that becomes an FName or object name (FName's NAME_SIZE limit is 1024) */
    static constexpr int32 MaxNameLength = 255;

    /** A required string that becomes an FName or object name: also rejects values longer than MaxNameLength */
    bool TryGetRequiredName(const FString& FieldName, FString& OutValue, FString& OutErrorMsg);
    FString GetOptionalString(const FString& FieldName, const FString& DefaultValue = TEXT("")) const;

    // Bool
    bool TryGetRequiredBool(const FString& FieldName, bool& OutValue, FString& OutErrorMsg);
    bool GetOptionalBool(const FString& FieldName, bool DefaultValue = false) const;

    // Number (double)
    bool TryGetRequiredNumber(const FString& FieldName, double& OutValue, FString& OutErrorMsg);
    double GetOptionalNumber(const FString& FieldName, double DefaultValue = 0.0) const;

    // Integer (int32)
    bool TryGetRequiredInt(const FString& FieldName, int32& OutValue, FString& OutErrorMsg);
    int32 GetOptionalInt(const FString& FieldName, int32 DefaultValue = 0) const;

    // JsonObject (const TSharedPtr<FJsonObject>* for direct access without copying)
    bool TryGetRequiredObject(const FString& FieldName, const TSharedPtr<FJsonObject>*& OutValue, FString& OutErrorMsg) const;
    const TSharedPtr<FJsonObject>* GetOptionalObjectPtr(const FString& FieldName) const; // Renamed
    
    // JsonObject (TSharedPtr<FJsonObject> for when a modifiable copy might be needed, though generally arguments are read-only)
    bool TryGetRequiredObject(const FString& FieldName, TSharedPtr<FJsonObject>& OutValue, FString& OutErrorMsg);
    TSharedPtr<FJsonObject> GetOptionalObject(const FString& FieldName, TSharedPtr<FJsonObject> DefaultValue = nullptr) const;


    // JsonArray (const TArray<TSharedPtr<FJsonValue>>* for direct access)
    bool TryGetRequiredArray(const FString& FieldName, const TArray<TSharedPtr<FJsonValue>>*& OutValue, FString& OutErrorMsg) const;
    const TArray<TSharedPtr<FJsonValue>>* GetOptionalArray(const FString& FieldName) const;

private:
    const TSharedPtr<FJsonObject> ArgumentsJson;
};
