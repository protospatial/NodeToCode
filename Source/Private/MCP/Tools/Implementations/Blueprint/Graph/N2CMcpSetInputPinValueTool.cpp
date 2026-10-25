// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSetInputPinValueTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register this tool with the MCP tool manager
REGISTER_MCP_TOOL(FN2CMcpSetInputPinValueTool)

#define LOCTEXT_NAMESPACE "NodeToCode"

FMcpToolDefinition FN2CMcpSetInputPinValueTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("set-input-pin-value"),
        TEXT("Sets the default value of an input pin on a Blueprint node of a graph (the one blueprintPath plus graphName or graphGuid name, else the focused graph). Only works on pins that accept default values (not exec, reference, or container pins).")
    ,

    	TEXT("Blueprint Graph Editing")

    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("nodeGuid"), TEXT("string"));
    Properties.Add(TEXT("pinGuid"), TEXT("string"));
    Properties.Add(TEXT("pinName"), TEXT("string"));  // Optional fallback
    Properties.Add(TEXT("value"), TEXT("string"));

    TArray<FString> Required;
    Required.Add(TEXT("nodeGuid"));
    Required.Add(TEXT("pinGuid"));
    Required.Add(TEXT("value"));

    Definition.InputSchema = BuildInputSchema(Properties, Required);

    FN2CGraphTarget::AddSchemaProperties(Definition);

    return Definition;
}

FMcpToolCallResult FN2CMcpSetInputPinValueTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        FN2CLogger::Get().Log(TEXT("SetInputPinValue: Starting execution"), EN2CLogSeverity::Debug);

        // Parse arguments
        FN2CMcpArgumentParser ArgParser(Arguments);
        FString ErrorMsg;

        FString NodeGuid;
        if (!ArgParser.TryGetRequiredString(TEXT("nodeGuid"), NodeGuid, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        FString PinGuid;
        if (!ArgParser.TryGetRequiredString(TEXT("pinGuid"), PinGuid, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        FString PinName = ArgParser.GetOptionalString(TEXT("pinName"));
        
        FString Value;
        if (!ArgParser.TryGetRequiredString(TEXT("value"), Value, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0152); every result
        // after this says which graph was used ('target')
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }
        UBlueprint* Blueprint = Target.Blueprint;
        UEdGraph* FocusedGraph = Target.Graph;

        // Find the node
        FGuid ParsedNodeGuid;
        if (!FGuid::Parse(NodeGuid, ParsedNodeGuid))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Invalid node GUID format")));
        }

        UEdGraphNode* TargetNode = nullptr;
        for (UEdGraphNode* Node : FocusedGraph->Nodes)
        {
            if (Node && Node->NodeGuid == ParsedNodeGuid)
            {
                TargetNode = Node;
                break;
            }
        }

        if (!TargetNode)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Node with GUID %s not found in graph"), *NodeGuid)
            ));
        }

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("SetInputPinValue: Found node: %s"), 
                *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString()), 
            EN2CLogSeverity::Debug
        );

        // Find the pin
        UEdGraphPin* TargetPin = FindPinOnNode(TargetNode, PinGuid, PinName);
        if (!TargetPin)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Pin with GUID %s not found on node"), *PinGuid)
            ));
        }

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("SetInputPinValue: Found pin: %s"), *TargetPin->GetDisplayName().ToString()), 
            EN2CLogSeverity::Debug
        );

        // Validate the pin can have a default value
        FString ValidationError;
        if (!ValidatePinForDefaultValue(TargetPin, ValidationError))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(ValidationError));
        }

        // Validate and format the value
        FString FormattedValue;
        if (!ValidateAndFormatValue(TargetPin, Value, FormattedValue, ValidationError))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(ValidationError));
        }

        // Store the old value for the result
        FString OldValue = TargetPin->GetDefaultAsString();

        // Set the default value using the schema
        const UEdGraphSchema* Schema = FocusedGraph->GetSchema();
        if (!Schema)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Graph has no schema")));
        }

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("SetInputPinValue: Setting value from '%s' to '%s'"), 
                *OldValue, *FormattedValue), 
            EN2CLogSeverity::Debug
        );

        // Begin transaction for undo/redo
        const FScopedTransaction Transaction(LOCTEXT("SetPinDefaultValue", "NodeToCode: Set Pin Default Value"));
        TargetNode->Modify();

        // Set the value based on pin type
        if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
        {
            Schema->TrySetDefaultText(*TargetPin, FText::FromString(FormattedValue));
        }
        else if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object || 
                 TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
                 TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
                 TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
        {
            Schema->TrySetDefaultObject(*TargetPin, nullptr);
            Schema->TrySetDefaultValue(*TargetPin, FormattedValue);
        }
        else
        {
            Schema->TrySetDefaultValue(*TargetPin, FormattedValue);
        }

        // Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
        FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

        // Show notification
        FNotificationInfo Info(FText::Format(
            LOCTEXT("PinValueSet", "Set value on pin '{0}'"),
            FText::FromString(TargetPin->GetDisplayName().ToString())
        ));
        Info.ExpireDuration = 2.0f;
        Info.bFireAndForget = true;
        Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle"));
        FSlateNotificationManager::Get().AddNotification(Info);

        // Build result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Pin value set successfully"));
        Result->SetStringField(TEXT("nodeGuid"), NodeGuid);
        Result->SetStringField(TEXT("nodeName"), TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
        Result->SetStringField(TEXT("pinGuid"), PinGuid);
        Result->SetStringField(TEXT("pinName"), TargetPin->GetDisplayName().ToString());
        Result->SetStringField(TEXT("pinType"), TargetPin->PinType.PinCategory.ToString());
        Result->SetStringField(TEXT("oldValue"), OldValue);
        Result->SetStringField(TEXT("newValue"), FormattedValue);

        // Add type-specific info
        if (!TargetPin->PinType.PinSubCategoryObject.IsValid())
        {
            Result->SetStringField(TEXT("subType"), TEXT("None"));
        }
        else
        {
            Result->SetStringField(TEXT("subType"), TargetPin->PinType.PinSubCategoryObject->GetName());
        }

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultString));
    });
}

UEdGraphPin* FN2CMcpSetInputPinValueTool::FindPinOnNode(UEdGraphNode* Node, const FString& PinGuid, 
    const FString& PinName) const
{
    if (!Node)
    {
        return nullptr;
    }

    // First try to find by GUID
    FGuid ParsedGuid;
    if (FGuid::Parse(PinGuid, ParsedGuid))
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->PinId == ParsedGuid)
            {
                return Pin;
            }
        }
    }

    // Fallback to name if provided
    if (!PinName.IsEmpty())
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && (Pin->PinName.ToString() == PinName || Pin->GetDisplayName().ToString() == PinName))
            {
                return Pin;
            }
        }
    }

    return nullptr;
}

bool FN2CMcpSetInputPinValueTool::ValidatePinForDefaultValue(const UEdGraphPin* Pin, FString& OutErrorMessage) const
{
    if (!Pin)
    {
        OutErrorMessage = TEXT("Pin is null");
        return false;
    }

    // Must be an input pin
    if (Pin->Direction != EGPD_Input)
    {
        OutErrorMessage = TEXT("Can only set default values on input pins");
        return false;
    }

    // Check if pin is hidden
    if (Pin->bHidden)
    {
        OutErrorMessage = TEXT("Cannot set default value on hidden pin");
        return false;
    }

    // Check if default value should be hidden based on schema
    const UEdGraphSchema* Schema = Pin->GetSchema();
    if (Schema && Schema->ShouldHidePinDefaultValue(const_cast<UEdGraphPin*>(Pin)))
    {
        OutErrorMessage = TEXT("This pin does not accept default values");
        return false;
    }

    // Check specific pin categories that don't support defaults
    const FName& Category = Pin->PinType.PinCategory;
    if (Category == UEdGraphSchema_K2::PC_Exec)
    {
        OutErrorMessage = TEXT("Cannot set default value on execution pin");
        return false;
    }

    // Container types don't support inline defaults
    if (Pin->PinType.IsContainer())
    {
        OutErrorMessage = TEXT("Cannot set default value on container pins (arrays, sets, maps)");
        return false;
    }

    // Reference parameters that aren't auto-create don't support defaults
    if (Pin->PinType.bIsReference && !UEdGraphSchema_K2::IsAutoCreateRefTerm(Pin))
    {
        OutErrorMessage = TEXT("Cannot set default value on reference pin");
        return false;
    }

    // Check if the pin is connected (connected pins ignore default values)
    if (Pin->LinkedTo.Num() > 0)
    {
        OutErrorMessage = TEXT("Cannot set default value on connected pin. Disconnect it first.");
        return false;
    }

    return true;
}

bool FN2CMcpSetInputPinValueTool::ValidateAndFormatValue(const UEdGraphPin* Pin, const FString& Value, 
    FString& OutFormattedValue, FString& OutErrorMessage) const
{
    if (!Pin)
    {
        OutErrorMessage = TEXT("Pin is null");
        return false;
    }

    const FName& Category = Pin->PinType.PinCategory;
    OutFormattedValue = Value;

    // Type-specific validation
    if (Category == UEdGraphSchema_K2::PC_Boolean)
    {
        // Accept various boolean formats
        if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || 
            Value.Equals(TEXT("1")) || 
            Value.Equals(TEXT("yes"), ESearchCase::IgnoreCase))
        {
            OutFormattedValue = TEXT("true");
        }
        else if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || 
                 Value.Equals(TEXT("0")) || 
                 Value.Equals(TEXT("no"), ESearchCase::IgnoreCase) ||
                 Value.IsEmpty())
        {
            OutFormattedValue = TEXT("false");
        }
        else
        {
            OutErrorMessage = TEXT("Invalid boolean value. Use 'true' or 'false'");
            return false;
        }
    }
    else if (Category == UEdGraphSchema_K2::PC_Int)
    {
        // Validate integer
        if (!Value.IsEmpty() && !Value.IsNumeric())
        {
            // Check for negative numbers
            if (!(Value.StartsWith(TEXT("-")) && Value.Mid(1).IsNumeric()))
            {
                OutErrorMessage = TEXT("Invalid integer value");
                return false;
            }
        }
    }
    else if (Category == UEdGraphSchema_K2::PC_Int64)
    {
        // Validate 64-bit integer
        if (!Value.IsEmpty() && !Value.IsNumeric())
        {
            // Check for negative numbers
            if (!(Value.StartsWith(TEXT("-")) && Value.Mid(1).IsNumeric()))
            {
                OutErrorMessage = TEXT("Invalid 64-bit integer value");
                return false;
            }
        }
    }
    else if (Category == UEdGraphSchema_K2::PC_Real || Category == UEdGraphSchema_K2::PC_Double)
    {
        // Validate floating point
        if (!Value.IsEmpty())
        {
            // Simple validation - more complex validation happens in the schema
            bool bValid = false;
            for (TCHAR Char : Value)
            {
                if (FChar::IsDigit(Char) || Char == TEXT('.') || Char == TEXT('-') || Char == TEXT('+'))
                {
                    bValid = true;
                }
                else if (!FChar::IsWhitespace(Char))
                {
                    OutErrorMessage = TEXT("Invalid floating point value");
                    return false;
                }
            }
            if (!bValid && !Value.IsEmpty())
            {
                OutErrorMessage = TEXT("Invalid floating point value");
                return false;
            }
        }
    }
    else if (Category == UEdGraphSchema_K2::PC_Struct)
    {
        // For struct types, empty string means default value
        // More complex struct validation would require type-specific parsing
        if (!Value.IsEmpty())
        {
            // Basic validation for common struct types
            if (Pin->PinType.PinSubCategoryObject.IsValid())
            {
                FString StructName = Pin->PinType.PinSubCategoryObject->GetName();
                
                // Check for common struct types that require a particular format
                if (StructName == TEXT("Vector") || StructName == TEXT("Vector3f") || StructName == TEXT("Rotator"))
                {
                    // Remove any spaces from the value
                    FString CleanValue = Value;
                    CleanValue.ReplaceInline(TEXT(" "), TEXT(""));
                    OutFormattedValue = CleanValue;
                    
                    // Only error if parentheses or equal symbols are present
                    if (CleanValue.Contains(TEXT("(")) || CleanValue.Contains(TEXT(")")) || CleanValue.Contains(TEXT("=")))
                    {
                        OutErrorMessage = TEXT("Invalid format. Use '0.0,0.0,0.0' format (comma-separated values only)");
                        return false;
                    }
                }
                else if (StructName == TEXT("Vector2D"))
                {
                    // Remove any spaces from the value
                    FString CleanValue = Value;
                    CleanValue.ReplaceInline(TEXT(" "), TEXT(""));
                    OutFormattedValue = CleanValue;
                    
                    // Only error if parentheses or equal symbols are present
                    if (CleanValue.Contains(TEXT("(")) || CleanValue.Contains(TEXT(")")) || CleanValue.Contains(TEXT("=")))
                    {
                        OutErrorMessage = TEXT("Invalid format. Use '0.0,0.0' format (comma-separated values only)");
                        return false;
                    }
                }
            }   
        }
    }
    else if (Category == UEdGraphSchema_K2::PC_Enum || Category == UEdGraphSchema_K2::PC_Byte)
    {
        // For enums, we accept the enum value name or index
        // The schema will handle validation
        if (Pin->PinType.PinSubCategoryObject.IsValid())
        {
            UEnum* EnumType = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
            if (EnumType && !Value.IsEmpty())
            {
                // Check if it's a valid enum name
                if (EnumType->GetIndexByNameString(Value) == INDEX_NONE)
                {
                    // Check if it's a valid index
                    if (Value.IsNumeric())
                    {
                        int32 Index = FCString::Atoi(*Value);
                        if (Index < 0 || Index >= EnumType->NumEnums())
                        {
                            OutErrorMessage = FString::Printf(TEXT("Invalid enum index. Valid range is 0-%d"), 
                                EnumType->NumEnums() - 1);
                            return false;
                        }
                    }
                    else
                    {
                        OutErrorMessage = TEXT("Invalid enum value. Use the enum name or index");
                        return false;
                    }
                }
            }
        }
    }
    // For other types (String, Name, Text, Object, etc.), we accept the value as-is
    // The schema will handle type-specific validation

    return true;
}

#undef LOCTEXT_NAMESPACE