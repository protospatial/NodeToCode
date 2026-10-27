// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for setting the default value of a member variable in a Blueprint.
 * The value is written on the generated class's default object, as the Details panel does
 * (FN2CMcpVariableDefaults::SetMemberDefault, card 0283): the variable description's DefaultValue is emptied by
 * every full compile, so it is not where the default lives.
 */
class FN2CMcpSetMemberVariableDefaultValueTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Builds the success result JSON
     * @param Blueprint The Blueprint that was modified
     * @param VariableName The name of the variable that was modified
     * @param VarDesc The variable description
     * @param OldDefaultValue The previous default value
     * @param NewDefaultValue The new default value
     * @return JSON object with result data
     */
    TSharedPtr<FJsonObject> BuildSuccessResult(
        const UBlueprint* Blueprint,
        const FString& VariableName,
        const FBPVariableDescription& VarDesc,
        const FString& OldDefaultValue,
        const FString& NewDefaultValue) const;
};
