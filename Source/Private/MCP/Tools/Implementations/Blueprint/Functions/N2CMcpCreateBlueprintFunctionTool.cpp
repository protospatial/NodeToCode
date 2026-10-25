// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateBlueprintFunctionTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpCreateBlueprintFunctionTool)

FMcpToolDefinition FN2CMcpCreateBlueprintFunctionTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("create-blueprint-function"),
		TEXT("Creates a new Blueprint function with specified parameters and opens it in the editor"),
		TEXT("Blueprint Function Management")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// functionName property
	TSharedPtr<FJsonObject> FunctionNameProp = MakeShareable(new FJsonObject);
	FunctionNameProp->SetStringField(TEXT("type"), TEXT("string"));
	FunctionNameProp->SetStringField(TEXT("description"), TEXT("Name of the function to create"));
	Properties->SetObjectField(TEXT("functionName"), FunctionNameProp);

	// blueprintPath property (optional)
	TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
	BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
	BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Asset path of the Blueprint (e.g., '/Game/Blueprints/MyActor.MyActor'). If not provided, uses focused Blueprint."));
	Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

	// parameters property (optional)
	TSharedPtr<FJsonObject> ParametersProp = MakeShareable(new FJsonObject);
	ParametersProp->SetStringField(TEXT("type"), TEXT("array"));
	ParametersProp->SetStringField(TEXT("description"), TEXT("Array of parameter definitions"));
	
	TSharedPtr<FJsonObject> ParameterItemSchema = MakeShareable(new FJsonObject);
	ParameterItemSchema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> ParamProperties = MakeShareable(new FJsonObject);
	
	// Parameter properties
	TSharedPtr<FJsonObject> ParamNameProp = MakeShareable(new FJsonObject);
	ParamNameProp->SetStringField(TEXT("type"), TEXT("string"));
	ParamProperties->SetObjectField(TEXT("name"), ParamNameProp);
	
	TSharedPtr<FJsonObject> DirectionProp = MakeShareable(new FJsonObject);
	DirectionProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> DirectionEnum;
	DirectionEnum.Add(MakeShareable(new FJsonValueString(TEXT("input"))));
	DirectionEnum.Add(MakeShareable(new FJsonValueString(TEXT("output"))));
	DirectionProp->SetArrayField(TEXT("enum"), DirectionEnum);
	DirectionProp->SetStringField(TEXT("default"), TEXT("input"));
	ParamProperties->SetObjectField(TEXT("direction"), DirectionProp);
	
	TSharedPtr<FJsonObject> TypeProp = MakeShareable(new FJsonObject);
	TypeProp->SetStringField(TEXT("type"), TEXT("string"));
	ParamProperties->SetObjectField(TEXT("type"), TypeProp);
	
	TSharedPtr<FJsonObject> SubTypeProp = MakeShareable(new FJsonObject);
	SubTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	ParamProperties->SetObjectField(TEXT("subType"), SubTypeProp);
	
	TSharedPtr<FJsonObject> ContainerProp = MakeShareable(new FJsonObject);
	ContainerProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> ContainerEnum;
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("none"))));
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("array"))));
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("set"))));
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("map"))));
	ContainerProp->SetArrayField(TEXT("enum"), ContainerEnum);
	ContainerProp->SetStringField(TEXT("default"), TEXT("none"));
	ParamProperties->SetObjectField(TEXT("container"), ContainerProp);
	
	TSharedPtr<FJsonObject> KeyTypeProp = MakeShareable(new FJsonObject);
	KeyTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	ParamProperties->SetObjectField(TEXT("keyType"), KeyTypeProp);
	
	TSharedPtr<FJsonObject> IsReferenceProp = MakeShareable(new FJsonObject);
	IsReferenceProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IsReferenceProp->SetBoolField(TEXT("default"), false);
	ParamProperties->SetObjectField(TEXT("isReference"), IsReferenceProp);
	
	TSharedPtr<FJsonObject> IsConstProp = MakeShareable(new FJsonObject);
	IsConstProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IsConstProp->SetBoolField(TEXT("default"), false);
	ParamProperties->SetObjectField(TEXT("isConst"), IsConstProp);
	
	TSharedPtr<FJsonObject> DefaultValueProp = MakeShareable(new FJsonObject);
	DefaultValueProp->SetStringField(TEXT("type"), TEXT("string"));
	ParamProperties->SetObjectField(TEXT("defaultValue"), DefaultValueProp);
	
	ParameterItemSchema->SetObjectField(TEXT("properties"), ParamProperties);
	
	TArray<TSharedPtr<FJsonValue>> ParamRequiredArray;
	ParamRequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("name"))));
	ParamRequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("type"))));
	ParameterItemSchema->SetArrayField(TEXT("required"), ParamRequiredArray);
	
	ParametersProp->SetObjectField(TEXT("items"), ParameterItemSchema);
	Properties->SetObjectField(TEXT("parameters"), ParametersProp);

	// functionFlags property (optional)
	TSharedPtr<FJsonObject> FunctionFlagsProp = MakeShareable(new FJsonObject);
	FunctionFlagsProp->SetStringField(TEXT("type"), TEXT("object"));
	FunctionFlagsProp->SetStringField(TEXT("description"), TEXT("Function configuration flags"));
	Properties->SetObjectField(TEXT("functionFlags"), FunctionFlagsProp);
	
	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("functionName"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpCreateBlueprintFunctionTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
        FN2CMcpArgumentParser ArgParser(Arguments);
        FString Error;

        FString FunctionName;
        if (!ArgParser.TryGetRequiredString(TEXT("functionName"), FunctionName, Error))
        {
            return FMcpToolCallResult::CreateErrorResult(Error);
        }
        
        FString BlueprintPath = ArgParser.GetOptionalString(TEXT("blueprintPath"));
        
        TArray<FParameterDefinition> Parameters;
        const TArray<TSharedPtr<FJsonValue>>* ParametersArrayJson = ArgParser.GetOptionalArray(TEXT("parameters"));
        if (ParametersArrayJson)
        {
            // The FJsonValueArray constructor expects a TArray<TSharedPtr<FJsonValue>>, not a pointer to it.
            // So we need to dereference it.
            TSharedPtr<FJsonValue> ParametersValue = MakeShareable(new FJsonValueArray(*ParametersArrayJson));
            if (!ParseParameters(ParametersValue, Parameters, Error))
            {
                return FMcpToolCallResult::CreateErrorResult(Error);
            }
        }
        
        FFunctionFlags Flags;
		TSharedPtr<FJsonObject> FlagsObjectJson = ArgParser.GetOptionalObject(TEXT("functionFlags"));
		if (FlagsObjectJson.IsValid())
        {
            ParseFunctionFlags(FlagsObjectJson, Flags);
        }
		
		// Resolve target Blueprint using the new utility
		UBlueprint* TargetBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, Error);
		if (!TargetBlueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}
		
		// Validate function name
		if (!ValidateFunctionName(TargetBlueprint, FunctionName, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}

		// Resolve every parameter type before the Blueprint is touched, so a bad type is an error, not a function
		// created without that parameter
		for (const FParameterDefinition& Param : Parameters)
		{
			FEdGraphPinType ParamPinType;
			FString ParamTypeError;
			if (!FN2CMcpTypeResolver::ResolvePinType(Param.Type, Param.SubType, Param.Container, Param.KeyType, Param.bIsReference, Param.bIsConst, ParamPinType, ParamTypeError))
			{
				return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("INVALID_PARAMETER_TYPE: parameter '%s': %s"), *Param.Name, *ParamTypeError));
			}
		}
		
		// Begin transaction for undo/redo support
		FScopedTransaction Transaction(NSLOCTEXT("NodeToCode", "CreateFunction", "Create Blueprint Function"));
		TargetBlueprint->Modify();
		
		// Create the function graph
		UEdGraph* FunctionGraph = CreateFunctionGraph(TargetBlueprint, FunctionName);
		if (!FunctionGraph)
		{
			Transaction.Cancel();
			return FMcpToolCallResult::CreateErrorResult(TEXT("INTERNAL_ERROR: Failed to create function graph"));
		}
		
		// Find the function entry and result nodes
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		FunctionGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
		
		if (EntryNodes.Num() != 1)
		{
			Transaction.Cancel();
			return FMcpToolCallResult::CreateErrorResult(TEXT("INTERNAL_ERROR: Invalid function graph structure"));
		}
		
		UK2Node_FunctionEntry* EntryNode = EntryNodes[0];
		UK2Node_FunctionResult* ResultNode = nullptr;
		
		// Check if we need a result node (if there are output parameters)
		bool bNeedsResultNode = false;
		for (const FParameterDefinition& Param : Parameters)
		{
			if (Param.Direction == TEXT("output"))
			{
				bNeedsResultNode = true;
				break;
			}
		}
		
		if (bNeedsResultNode)
		{
			ResultNode = FindOrCreateResultNode(EntryNode);
		}
		
		// Create function parameters
		CreateFunctionParameters(EntryNode, ResultNode, Parameters);
		
		// Set function metadata
		SetFunctionMetadata(EntryNode, Flags);
		
		// Get or create function GUID
		FGuid FunctionGuid = GetOrCreateFunctionGuid(FunctionGraph);
		
		// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
		FN2CMcpBlueprintUtils::MarkModified(TargetBlueprint, /*bStructural*/ true);
		
		// Open the function in the editor
		OpenFunctionInEditor(TargetBlueprint, FunctionGraph);
		
		// Show notification
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("NodeToCode", "FunctionCreated", "Function '{0}' created successfully"),
			FText::FromString(FunctionName)
		));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		
		// Build and return success result
		TSharedPtr<FJsonObject> ResultJsonObj = BuildSuccessResult(TargetBlueprint, FunctionName, FunctionGuid, FunctionGraph); // Renamed to avoid conflict
		
		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultJsonObj.ToSharedRef(), Writer); // Use ResultJsonObj

		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}

bool FN2CMcpCreateBlueprintFunctionTool::ValidateFunctionName(const UBlueprint* Blueprint, const FString& FunctionName, FString& OutError) const
{
	if (FunctionName.IsEmpty())
	{
		OutError = TEXT("Function name cannot be empty");
		return false;
	}
	
	// Check for valid identifier characters
	if (!FName::IsValidXName(FunctionName, INVALID_OBJECTNAME_CHARACTERS))
	{
		OutError = TEXT("Function name contains invalid characters");
		return false;
	}
	
	// Check if function already exists
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == *FunctionName)
		{
			OutError = FString::Printf(TEXT("NAME_COLLISION: Function '%s' already exists"), *FunctionName);
			return false;
		}
	}
	
	// Check against parent class functions
	if (Blueprint->ParentClass)
	{
		UFunction* ExistingFunction = Blueprint->ParentClass->FindFunctionByName(*FunctionName);
		if (ExistingFunction && !ExistingFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
		{
			OutError = FString::Printf(TEXT("NAME_COLLISION: Cannot override non-BlueprintEvent function '%s'"), *FunctionName);
			return false;
		}
	}
	
	return true;
}

UEdGraph* FN2CMcpCreateBlueprintFunctionTool::CreateFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName)
{
	// Create a new graph for the function
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);
	
	if (!NewGraph)
	{
		FN2CLogger::Get().LogError(TEXT("Failed to create new graph for function"));
		return nullptr;
	}
	
	// Add the function graph to the blueprint
	// This creates the entry/exit nodes and sets up the graph properly
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, true, nullptr);
	
	FN2CLogger::Get().Log(FString::Printf(
		TEXT("Created function '%s' in Blueprint '%s'"), 
		*FunctionName, *Blueprint->GetName()), EN2CLogSeverity::Info);
	
	return NewGraph;
}

void FN2CMcpCreateBlueprintFunctionTool::CreateFunctionParameters(UK2Node_FunctionEntry* EntryNode, 
	UK2Node_FunctionResult* ResultNode, const TArray<FParameterDefinition>& Parameters)
{
	for (const FParameterDefinition& Param : Parameters)
	{
		FEdGraphPinType PinType;
		FString ConversionError;
		
		if (!FN2CMcpTypeResolver::ResolvePinType(Param.Type, Param.SubType, Param.Container, Param.KeyType, Param.bIsReference, Param.bIsConst, PinType, ConversionError))
		{
			FN2CLogger::Get().LogError(FString::Printf(
				TEXT("Failed to convert parameter '%s': %s"), *Param.Name, *ConversionError));
			continue;
		}
		
		CreateParameter(EntryNode, ResultNode, Param, PinType);
	}
	
	// Reconstruct and refresh the nodes
	EntryNode->ReconstructNode();
	if (ResultNode)
	{
		ResultNode->ReconstructNode();
	}
}

void FN2CMcpCreateBlueprintFunctionTool::SetFunctionMetadata(UK2Node_FunctionEntry* EntryNode, const FFunctionFlags& Flags)
{
	if (!EntryNode)
	{
		return;
	}
	
	// Set function flags
	int32 ExtraFlags = EntryNode->GetExtraFlags();
	
	if (Flags.bIsPure)
	{
		ExtraFlags |= FUNC_BlueprintPure;
	}
	
	if (Flags.bIsCallInEditor)
	{
		// Note: FUNC_CallInEditor is not a standard function flag in UE5
		// This would typically be set via metadata instead
		EntryNode->MetaData.bCallInEditor = true;
	}
	
	if (Flags.bIsStatic)
	{
		ExtraFlags |= FUNC_Static;
	}
	
	if (Flags.bIsConst)
	{
		ExtraFlags |= FUNC_Const;
	}
	
	// Set the extra flags
	EntryNode->SetExtraFlags(ExtraFlags);
	
	// Set metadata
	EntryNode->MetaData.Category = FText::FromString(Flags.Category);
	
	if (!Flags.Keywords.IsEmpty())
	{
		EntryNode->MetaData.Keywords = FText::FromString(Flags.Keywords);
	}
	
	if (!Flags.Tooltip.IsEmpty())
	{
		EntryNode->MetaData.ToolTip = FText::FromString(Flags.Tooltip);
	}
	
	// Set access specifier
	if (Flags.AccessSpecifier == TEXT("private"))
	{
		EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() | FUNC_Private);
	}
	else if (Flags.AccessSpecifier == TEXT("protected"))
	{
		EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() | FUNC_Protected);
	}
	// public is default, no flag needed
	
	// Handle replication flags
	if (Flags.bIsReliableReplication || Flags.bIsMulticast)
	{
		int32 NetFlags = EntryNode->GetExtraFlags();
		
		if (Flags.bIsReliableReplication)
		{
			NetFlags |= FUNC_NetReliable;
			if (!Flags.bIsMulticast)
			{
				NetFlags |= FUNC_NetServer;  // Server RPC
			}
		}
		
		if (Flags.bIsMulticast)
		{
			NetFlags |= FUNC_NetMulticast;
		}
		
		EntryNode->SetExtraFlags(NetFlags);
	}
}

FGuid FN2CMcpCreateBlueprintFunctionTool::GetOrCreateFunctionGuid(UEdGraph* FunctionGraph) const
{
	if (!FunctionGraph)
	{
		return FGuid();
	}
	
	// Use the utility to get or create a consistent GUID for this function
	return FN2CMcpFunctionGuidUtils::GetOrCreateFunctionGuid(FunctionGraph);
}

void FN2CMcpCreateBlueprintFunctionTool::CreateParameter(UK2Node_FunctionEntry* EntryNode, 
	UK2Node_FunctionResult* ResultNode, const FParameterDefinition& ParamDef, const FEdGraphPinType& PinType)
{
	if (ParamDef.Direction == TEXT("input"))
	{
		// Input parameters are added to the entry node as output pins
		if (EntryNode)
		{
			EntryNode->CreateUserDefinedPin(*ParamDef.Name, PinType, EGPD_Output);
			
			// Set default value if provided
			if (!ParamDef.DefaultValue.IsEmpty())
			{
				if (UEdGraphPin* NewPin = EntryNode->FindPin(*ParamDef.Name))
				{
					NewPin->DefaultValue = ParamDef.DefaultValue;
				}
			}
		}
	}
	else if (ParamDef.Direction == TEXT("output"))
	{
		// Output parameters are added to the result node as input pins
		if (ResultNode)
		{
			ResultNode->CreateUserDefinedPin(*ParamDef.Name, PinType, EGPD_Input);
		}
	}
}

UK2Node_FunctionResult* FN2CMcpCreateBlueprintFunctionTool::FindOrCreateResultNode(UK2Node_FunctionEntry* EntryNode)
{
	if (!EntryNode)
	{
		return nullptr;
	}
	
	// Use the built-in utility to find or create the result node
	return FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
}

TSharedPtr<FJsonObject> FN2CMcpCreateBlueprintFunctionTool::BuildSuccessResult(const UBlueprint* Blueprint, 
	const FString& FunctionName, const FGuid& FunctionGuid, UEdGraph* FunctionGraph) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("functionGuid"), FunctionGuid.ToString());
	Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	
	// Add graph info
	if (FunctionGraph)
	{
		TSharedPtr<FJsonObject> GraphInfo = MakeShareable(new FJsonObject);
		GraphInfo->SetStringField(TEXT("graphName"), FunctionGraph->GetName());
		GraphInfo->SetNumberField(TEXT("nodeCount"), FunctionGraph->Nodes.Num());
		Result->SetObjectField(TEXT("graphInfo"), GraphInfo);
	}
	
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Function '%s' created successfully with GUID %s"), 
		*FunctionName, *FunctionGuid.ToString()));
	
	return Result;
}

bool FN2CMcpCreateBlueprintFunctionTool::ParseParameters(const TSharedPtr<FJsonValue>& ParametersValue, 
	TArray<FParameterDefinition>& OutParameters, FString& OutError) const
{
	const TArray<TSharedPtr<FJsonValue>>* ParamsArray = nullptr;
	if (!ParametersValue->TryGetArray(ParamsArray) || !ParamsArray)
	{
		OutError = TEXT("Parameters must be an array");
		return false;
	}
	
	for (const TSharedPtr<FJsonValue>& ParamValue : *ParamsArray)
	{
		const TSharedPtr<FJsonObject>* ParamObject = nullptr;
		if (!ParamValue->TryGetObject(ParamObject) || !ParamObject)
		{
			OutError = TEXT("Each parameter must be an object");
			return false;
		}
		
		FParameterDefinition ParamDef;
		
		// Required fields
		if (!(*ParamObject)->TryGetStringField(TEXT("name"), ParamDef.Name))
		{
			OutError = TEXT("Parameter missing required field: name");
			return false;
		}
		
		if (!(*ParamObject)->TryGetStringField(TEXT("type"), ParamDef.Type))
		{
			OutError = TEXT("Parameter missing required field: type");
			return false;
		}
		
		// Optional fields
		(*ParamObject)->TryGetStringField(TEXT("direction"), ParamDef.Direction);
		(*ParamObject)->TryGetStringField(TEXT("subType"), ParamDef.SubType);
		(*ParamObject)->TryGetStringField(TEXT("container"), ParamDef.Container);
		(*ParamObject)->TryGetStringField(TEXT("keyType"), ParamDef.KeyType);
		(*ParamObject)->TryGetBoolField(TEXT("isReference"), ParamDef.bIsReference);
		(*ParamObject)->TryGetBoolField(TEXT("isConst"), ParamDef.bIsConst);
		(*ParamObject)->TryGetStringField(TEXT("defaultValue"), ParamDef.DefaultValue);
		
		OutParameters.Add(ParamDef);
	}
	
	return true;
}

bool FN2CMcpCreateBlueprintFunctionTool::ParseFunctionFlags(const TSharedPtr<FJsonObject>& FlagsObject, 
	FFunctionFlags& OutFlags) const
{
	// Parse all optional flags
	FlagsObject->TryGetBoolField(TEXT("isPure"), OutFlags.bIsPure);
	FlagsObject->TryGetBoolField(TEXT("isCallInEditor"), OutFlags.bIsCallInEditor);
	FlagsObject->TryGetBoolField(TEXT("isStatic"), OutFlags.bIsStatic);
	FlagsObject->TryGetBoolField(TEXT("isConst"), OutFlags.bIsConst);
	FlagsObject->TryGetBoolField(TEXT("isOverride"), OutFlags.bIsOverride);
	FlagsObject->TryGetBoolField(TEXT("isReliableReplication"), OutFlags.bIsReliableReplication);
	FlagsObject->TryGetBoolField(TEXT("isMulticast"), OutFlags.bIsMulticast);
	
	FlagsObject->TryGetStringField(TEXT("category"), OutFlags.Category);
	FlagsObject->TryGetStringField(TEXT("keywords"), OutFlags.Keywords);
	FlagsObject->TryGetStringField(TEXT("tooltip"), OutFlags.Tooltip);
	FlagsObject->TryGetStringField(TEXT("accessSpecifier"), OutFlags.AccessSpecifier);
	
	return true;
}

void FN2CMcpCreateBlueprintFunctionTool::OpenFunctionInEditor(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
	if (!Blueprint || !FunctionGraph)
	{
		return;
	}
	
	// Use the utility function to open or focus the Blueprint editor
	TSharedPtr<IBlueprintEditor> Editor;
	FString ErrorMsg;
	bool bSuccess = FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, Editor, ErrorMsg);
	
	if (!bSuccess)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to open Blueprint editor: %s"), *ErrorMsg));
		return;
	}
	
	// Jump to the function graph
	if (Editor.IsValid())
	{
		Editor->JumpToHyperlink(FunctionGraph, false);
		Editor->FocusWindow();
		FN2CLogger::Get().Log(FString::Printf(TEXT("Navigated to function: %s"), *FunctionGraph->GetName()), EN2CLogSeverity::Debug);
	}
}
