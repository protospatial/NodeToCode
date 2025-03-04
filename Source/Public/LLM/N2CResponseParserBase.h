// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CTranslation.h"
#include "Dom/JsonObject.h"
#include "N2CResponseParserBase.generated.h"

/**
 * @class UN2CResponseParserBase
 * @brief Base class for parsing LLM responses into translation structs
 */
UCLASS(Abstract)
class NODETOCODE_API UN2CResponseParserBase : public UObject
{
    GENERATED_BODY()

public:
    /** Initialize with configuration */
    virtual void Initialize();

    /** Parse JSON response into translation structs */
    UFUNCTION(BlueprintCallable, Category = "LLM Integration")
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse
    );

public:
    /** Common error handling for provider responses */
    bool HandleCommonErrorResponse(
        const TSharedPtr<FJsonObject>& JsonObject,
        const FString& ErrorFieldName,
        FString& OutErrorMessage);
        
    /** Common content extraction from standard message formats */
    bool ExtractStandardMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        const FString& ArrayFieldName,
        const FString& MessageObjName,
        const FString& ContentFieldName,
        FString& OutContent);

protected:
    /** Remove newlines from string */
    FString RemoveNewlines(const FString& Input) const;

    /** Validate response format */
    bool ValidateResponseFormat(const TSharedPtr<FJsonObject>& JsonObject) const;

    /** Extract graph data from JSON */
    void ExtractGraphData(
        const TSharedPtr<FJsonObject>& GraphObject,
        FN2CGraphTranslation& OutGraph
    );

    /** Extract code data from JSON */
    void ExtractCodeData(
        const TSharedPtr<FJsonObject>& CodeObject,
        FN2CGeneratedCode& OutCode
    );
    
    /** Process JSON content with code block markers */
    bool ProcessJsonContentWithMarkers(FString& Content);
};
