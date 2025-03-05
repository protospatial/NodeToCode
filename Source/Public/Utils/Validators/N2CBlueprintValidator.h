// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Utils/Validators/N2CNodeValidator.h"
#include "Utils/N2CLogger.h"

/**
 * @class FN2CBlueprintValidator
 * @brief Validates blueprint definitions to ensure they meet requirements
 */
class NODETOCODE_API FN2CBlueprintValidator
{
public:
    /** Validate a blueprint definition */
    bool Validate(const FN2CBlueprint& Blueprint, FString& OutError);
    
    /** Validate a single graph */
    bool ValidateGraph(const FN2CGraph& Graph, FString& OutError);
    
    /** Validate flow references in a graph */
    bool ValidateFlowReferences(const FN2CGraph& Graph, FString& OutError);
    
    /** Validate a struct definition */
    bool ValidateStruct(const FN2CStruct& Struct, FString& OutError);
    
    /** Validate an enum definition */
    bool ValidateEnum(const FN2CEnum& Enum, FString& OutError);
    
private:
    /** Validate required fields */
    bool ValidateRequired(const FN2CBlueprint& Blueprint, FString& OutError);
    
    /** Validate all graphs in the blueprint */
    bool ValidateGraphs(const FN2CBlueprint& Blueprint, FString& OutError);
    
    /** Validate all structs in the blueprint */
    bool ValidateStructs(const FN2CBlueprint& Blueprint, FString& OutError);
    
    /** Validate all enums in the blueprint */
    bool ValidateEnums(const FN2CBlueprint& Blueprint, FString& OutError);
    
    /** Node validator instance */
    FN2CNodeValidator NodeValidator;
};
