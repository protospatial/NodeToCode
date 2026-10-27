// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * @class FN2CMcpSetInputPinValueTool
 * @brief MCP tool for setting the default value of an input pin on a Blueprint node
 * 
 * This tool allows setting the default value of input pins on Blueprint nodes,
 * similar to entering values in the Details panel or inline on the node.
 * 
 * @example Set a string value on a Print String node:
 * {
 *   "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
 *   "pinGuid": "BBE5F1A04B2E8F9E003C6B8F12345678",
 *   "value": "Hello, World!"
 * }
 * 
 * @example Reset a pin to its default value (the editor's Reset to Default):
 * {
 *   "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
 *   "pinGuid": "BBE5F1A04B2E8F9E003C6B8F12345678",
 *   "value": ""
 * }
 * 
 * @note Only works on input pins that accept default values (not exec, reference, or container pins)
 * @note The value is checked by the K2 schema (IsPinDefaultValid) before the node is touched: a refused value is an
 *       error with the schema's message, and the pin keeps its old default. newValue is read back from the pin.
 */
class FN2CMcpSetInputPinValueTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Finds a pin on a node using GUID with fallback to name
     * @param Node The node to search
     * @param PinGuid The GUID of the pin
     * @param PinName Optional pin name for fallback
     * @return The found pin or nullptr
     */
    class UEdGraphPin* FindPinOnNode(class UEdGraphNode* Node, const FString& PinGuid, const FString& PinName) const;
    
    /**
     * Validates that a pin can have its default value set
     * @param Pin The pin to validate
     * @param OutErrorMessage Error message if validation fails
     * @return True if the pin can have a default value
     */
    bool ValidatePinForDefaultValue(const class UEdGraphPin* Pin, FString& OutErrorMessage) const;

};