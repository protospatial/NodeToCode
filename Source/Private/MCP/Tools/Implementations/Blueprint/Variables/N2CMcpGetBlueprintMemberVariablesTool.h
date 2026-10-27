// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * @class FN2CMcpGetBlueprintMemberVariablesTool
 * @brief MCP tool for retrieving all member variables from the currently focused Blueprint
 * 
 * This tool provides functionality to extract information about all member variables
 * defined in a Blueprint, including their types, categories, properties, and metadata.
 */
class FN2CMcpGetBlueprintMemberVariablesTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Builds a JSON object containing information about a Blueprint variable
     * @param Blueprint The Blueprint that declares the variable (its class defaults hold the default value)
     * @param VarDesc The variable description from the Blueprint
     * @return JSON object with variable details
     */
    TSharedPtr<FJsonObject> BuildVariableInfo(const class UBlueprint* Blueprint, const struct FBPVariableDescription& VarDesc) const;
    
    /**
     * Converts pin type information to a JSON object. The top-level fields describe the element type (for a map, its
     * value: what create-variable's typeIdentifier takes); a map also has keyType and valueType (card 0283)
     * @param PinType The pin type to convert
     * @return JSON object with type information
     */
    TSharedPtr<FJsonObject> PinTypeToJson(const struct FEdGraphPinType& PinType) const;
};