// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateBlueprintFunctionTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpPinDefaultUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_Knot.h"
#include "ScopedTransaction.h"
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
		N2CMcpToolsets::BlueprintFunction
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
	
	FN2CGraphTarget::AddSchemaProperties(Definition, /*bGraphArguments*/ false);

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
		if (FlagsObjectJson.IsValid() && !ParseFunctionFlags(FlagsObjectJson, Flags, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}

		// The Blueprint blueprintPath names, else the focused one (the graph resolver's Blueprint step, card 0152)
		UBlueprint* TargetBlueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath, Error);
		if (!TargetBlueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}

		// Everything is checked before the Blueprint is touched (card 0280): a cancelled transaction does not undo
		// what was done inside it, so a refusal after CreateFunctionGraph would leave a half-made function behind
		if (!ValidateFunctionName(TargetBlueprint, FunctionName, Error)
			|| !ValidateFunctionFlags(TargetBlueprint, Flags, Error)
			|| !ResolveParameters(Parameters, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
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
		TArray<FCreatedPin> CreatedPins;
		TArray<FString> Warnings;
		CreateFunctionParameters(EntryNode, ResultNode, Parameters, CreatedPins, Warnings);

		// Set function metadata
		SetFunctionMetadata(EntryNode, Flags);

		// The function's stable ID: its GraphGuid, unchanged by renames and moves (card 0152)
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
		TSharedPtr<FJsonObject> ResultJsonObj = BuildSuccessResult(TargetBlueprint, FunctionName, FunctionGuid, FunctionGraph, CreatedPins, Warnings);

		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultJsonObj.ToSharedRef(), Writer);

		// The new function as a target the graph and function tools take (blueprintPath plus graphGuid)
		FN2CGraphTarget Target;
		Target.Blueprint = TargetBlueprint;
		Target.Graph = FunctionGraph;
		Target.ResolvedBy = BlueprintPath.IsEmpty() ? EN2CGraphResolvedBy::Focus : EN2CGraphResolvedBy::Explicit;
		return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultString));
	});
}

namespace N2CCreateFunctionPrivate
{
	/** What already uses Name in the Blueprint, for the NAME_COLLISION message */
	FString DescribeNameUse(const UBlueprint* Blueprint, const FName Name)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName() == Name)
			{
				const FString Kind = FN2CGraphTarget::GraphKind(Graph);
				if (Kind == TEXT("delegate"))
				{
					return TEXT("an event dispatcher");
				}
				if (Kind == TEXT("event"))
				{
					return TEXT("an event graph");
				}
				return FString::Printf(TEXT("a %s graph"), *Kind);
			}
		}
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name) != INDEX_NONE)
		{
			return TEXT("a member variable");
		}
		if (FBlueprintEditorUtils::FindSCS_Node(Blueprint, Name))
		{
			return TEXT("a component");
		}
		const UClass* SearchClass = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass.Get() : Blueprint->ParentClass.Get();
		if (SearchClass)
		{
			if (const UFunction* Function = SearchClass->FindFunctionByName(Name))
			{
				return FString::Printf(TEXT("a function of %s"), *Function->GetOwnerClass()->GetName());
			}
			if (const FProperty* Property = SearchClass->FindPropertyByName(Name))
			{
				return FString::Printf(TEXT("a property of %s"), *Property->GetOwnerClass()->GetName());
			}
		}
		return TEXT("another member or object of the Blueprint");
	}

	/** A pin of Type on a transient node of a transient K2 graph, to check a default value before the real pin exists */
	UEdGraphPin* MakeScratchPin(const FEdGraphPinType& Type)
	{
		UEdGraph* Graph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transient);
		Graph->Schema = UEdGraphSchema_K2::StaticClass();
		UK2Node_Knot* Node = NewObject<UK2Node_Knot>(Graph, NAME_None, RF_Transient);
		Graph->Nodes.Add(Node);
		return Node->CreatePin(EGPD_Output, Type, TEXT("Scratch"));
	}
}

bool FN2CMcpCreateBlueprintFunctionTool::ValidateFunctionName(const UBlueprint* Blueprint, const FString& FunctionName, FString& OutError) const
{
	if (FunctionName.IsEmpty())
	{
		OutError = TEXT("Function name cannot be empty");
		return false;
	}

	// The editor's own name rules (card 0280): FKismetNameValidator refuses a name any graph (event graph, function,
	// macro, event dispatcher), member variable, component or function of the class hierarchy already has.
	// CreateNewGraph would rename such a graph out of the way, breaking a dispatcher or the event graph.
	FKismetNameValidator Validator(Blueprint);
	const EValidatorResult Validation = Validator.IsValid(FunctionName);
	switch (Validation)
	{
	case EValidatorResult::Ok:
		return true;
	case EValidatorResult::AlreadyInUse:
	case EValidatorResult::ExistingName:
	{
		const FName Suggestion = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FunctionName);
		OutError = FString::Printf(TEXT("NAME_COLLISION: '%s' is already %s in %s. Pick another name, for example '%s'."),
			*FunctionName, *N2CCreateFunctionPrivate::DescribeNameUse(Blueprint, FName(*FunctionName)), *Blueprint->GetName(),
			*Suggestion.ToString());
		return false;
	}
	case EValidatorResult::TooLong:
		OutError = FString::Printf(TEXT("INVALID_NAME: function name '%s' is too long (at most %d characters)"),
			*FunctionName, FKismetNameValidator::GetMaximumNameLength());
		return false;
	case EValidatorResult::ContainsInvalidCharacters:
		OutError = FString::Printf(TEXT("INVALID_NAME: function name '%s' contains characters a Blueprint name cannot have"), *FunctionName);
		return false;
	default:
		OutError = FString::Printf(TEXT("INVALID_NAME: '%s' is not a valid function name"), *FunctionName);
		return false;
	}
}

bool FN2CMcpCreateBlueprintFunctionTool::ValidateFunctionFlags(const UBlueprint* Blueprint, const FFunctionFlags& Flags, FString& OutError) const
{
	// Every function of a function library is static already (AddFunctionGraph); elsewhere a static Blueprint function
	// has no self, which the editor never makes
	if (Flags.bIsStatic && Blueprint->BlueprintType != BPTYPE_FunctionLibrary)
	{
		OutError = FString::Printf(TEXT("INVALID_FLAGS: isStatic is only for Blueprint Function Libraries (their functions are static already); %s is not one"),
			*Blueprint->GetName());
		return false;
	}
	return true;
}

bool FN2CMcpCreateBlueprintFunctionTool::ResolveParameters(TArray<FParameterDefinition>& Parameters, FString& OutError) const
{
	for (FParameterDefinition& Param : Parameters)
	{
		FString TypeError;
		if (!FN2CMcpTypeResolver::ResolvePinType(Param.Type, Param.SubType, Param.Container, Param.KeyType, Param.bIsReference, Param.bIsConst, Param.PinType, TypeError))
		{
			OutError = FString::Printf(TEXT("INVALID_PARAMETER_TYPE: parameter '%s': %s"), *Param.Name, *TypeError);
			return false;
		}
		if (!Param.DefaultValue.IsEmpty())
		{
			// The value the pin would refuse is refused here, before the function exists
			UEdGraphPin* Scratch = N2CCreateFunctionPrivate::MakeScratchPin(Param.PinType);
			FString DefaultError;
			if (!Scratch || !FN2CMcpPinDefault::Prepare(*Scratch, Param.DefaultValue, Param.PreparedDefault, DefaultError))
			{
				OutError = FString::Printf(TEXT("INVALID_DEFAULT_VALUE: parameter '%s' refused the default '%s': %s"),
					*Param.Name, *Param.DefaultValue, Scratch ? *DefaultError : TEXT("no pin of that type could be made"));
				return false;
			}
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
	UK2Node_FunctionResult* ResultNode, const TArray<FParameterDefinition>& Parameters, TArray<FCreatedPin>& OutPins,
	TArray<FString>& OutWarnings)
{
	for (const FParameterDefinition& Param : Parameters)
	{
		// Inputs are output pins of the entry node; outputs are input pins of the result node
		const bool bInput = Param.Direction == TEXT("input");
		UK2Node_EditablePinBase* Node = bInput ? static_cast<UK2Node_EditablePinBase*>(EntryNode) : static_cast<UK2Node_EditablePinBase*>(ResultNode);
		if (!Node)
		{
			OutWarnings.Add(FString::Printf(TEXT("Parameter '%s' was not created: the function has no %s node"),
				*Param.Name, bInput ? TEXT("entry") : TEXT("result")));
			continue;
		}

		// A taken name is made unique (a second Value becomes Value1): the pin returned is the one this parameter got
		UEdGraphPin* NewPin = Node->CreateUserDefinedPin(FName(*Param.Name), Param.PinType, bInput ? EGPD_Output : EGPD_Input, /*bUseUniqueName*/ true);
		if (!NewPin)
		{
			OutWarnings.Add(FString::Printf(TEXT("Parameter '%s' was not created"), *Param.Name));
			continue;
		}

		FCreatedPin Created;
		Created.RequestedName = Param.Name;
		Created.ActualName = NewPin->PinName.ToString();
		Created.Direction = Param.Direction;

		if (!Param.PreparedDefault.IsEmpty())
		{
			// Stored in the FUserPinInfo, so the default survives every rebuild of the node (card 0280)
			TSharedPtr<FUserPinInfo>* PinInfo = Node->UserDefinedPins.FindByPredicate(
				[NewPin](const TSharedPtr<FUserPinInfo>& Info) { return Info.IsValid() && Info->PinName == NewPin->PinName; });
			if (PinInfo && PinInfo->IsValid() && Node->ModifyUserDefinedPinDefaultValue(*PinInfo, Param.PreparedDefault))
			{
				Created.DefaultValue = (*PinInfo)->PinDefaultValue;
			}
			else
			{
				OutWarnings.Add(FString::Printf(TEXT("Parameter '%s' refused the default '%s'"), *Created.ActualName, *Param.DefaultValue));
			}
		}
		OutPins.Add(Created);
	}

	// Reconstruct and refresh the nodes
	EntryNode->ReconstructNode();
	if (ResultNode)
	{
		ResultNode->ReconstructNode();
	}

	// Pin IDs after the rebuild
	for (FCreatedPin& Created : OutPins)
	{
		const bool bInput = Created.Direction == TEXT("input");
		UEdGraphNode* Node = bInput ? static_cast<UEdGraphNode*>(EntryNode) : static_cast<UEdGraphNode*>(ResultNode);
		if (const UEdGraphPin* Pin = Node ? Node->FindPin(FName(*Created.ActualName), bInput ? EGPD_Output : EGPD_Input) : nullptr)
		{
			Created.PinId = Pin->PinId.ToString();
		}
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

	if (Flags.bIsConst)
	{
		ExtraFlags |= FUNC_Const;
	}

	// One access specifier: AddFunctionGraph seeds FUNC_Public, so the mask is cleared first, as the editor's details
	// panel does (card 0280). Static is already set for a function library and refused elsewhere; net flags are refused
	ExtraFlags &= ~static_cast<int32>(FUNC_AccessSpecifiers);
	if (Flags.AccessSpecifier == TEXT("private"))
	{
		ExtraFlags |= FUNC_Private;
	}
	else if (Flags.AccessSpecifier == TEXT("protected"))
	{
		ExtraFlags |= FUNC_Protected;
	}
	else
	{
		ExtraFlags |= FUNC_Public;
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
}

FGuid FN2CMcpCreateBlueprintFunctionTool::GetOrCreateFunctionGuid(UEdGraph* FunctionGraph) const
{
	if (!FunctionGraph)
	{
		return FGuid();
	}

	// Use the utility to get or create a consistent GUID for this function
	return FN2CMcpFunctionGuidUtils::GetFunctionId(FunctionGraph);
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
	const FString& FunctionName, const FGuid& FunctionGuid, UEdGraph* FunctionGraph, const TArray<FCreatedPin>& Pins,
	const TArray<FString>& Warnings) const
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

	// The pins made, so a renamed duplicate is visible (card 0280)
	TArray<TSharedPtr<FJsonValue>> PinArray;
	for (const FCreatedPin& Pin : Pins)
	{
		TSharedPtr<FJsonObject> PinJson = MakeShareable(new FJsonObject);
		PinJson->SetStringField(TEXT("name"), Pin.RequestedName);
		PinJson->SetStringField(TEXT("actualName"), Pin.ActualName);
		PinJson->SetStringField(TEXT("pinId"), Pin.PinId);
		PinJson->SetStringField(TEXT("direction"), Pin.Direction);
		if (!Pin.DefaultValue.IsEmpty())
		{
			PinJson->SetStringField(TEXT("defaultValue"), Pin.DefaultValue);
		}
		PinArray.Add(MakeShareable(new FJsonValueObject(PinJson)));
	}
	Result->SetArrayField(TEXT("parameters"), PinArray);

	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarningArray;
		for (const FString& Warning : Warnings)
		{
			WarningArray.Add(MakeShareable(new FJsonValueString(Warning)));
		}
		Result->SetArrayField(TEXT("warnings"), WarningArray);
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

	for (int32 Index = 0; Index < ParamsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& ParamValue = (*ParamsArray)[Index];
		const TSharedPtr<FJsonObject>* ParamObject = nullptr;
		if (!ParamValue.IsValid() || !ParamValue->TryGetObject(ParamObject) || !ParamObject)
		{
			OutError = TEXT("Each parameter must be an object");
			return false;
		}

		FParameterDefinition ParamDef;

		// Required fields
		if (!(*ParamObject)->TryGetStringField(TEXT("name"), ParamDef.Name) || ParamDef.Name.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("Parameter %d missing required field: name"), Index);
			return false;
		}
		ParamDef.Name.TrimStartAndEndInline();

		if (!(*ParamObject)->TryGetStringField(TEXT("type"), ParamDef.Type))
		{
			OutError = FString::Printf(TEXT("Parameter '%s' missing required field: type"), *ParamDef.Name);
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

		// Before card 0280 any other direction was dropped and the call still succeeded
		ParamDef.Direction = ParamDef.Direction.TrimStartAndEnd().ToLower();
		if (ParamDef.Direction.IsEmpty())
		{
			ParamDef.Direction = TEXT("input");
		}
		if (ParamDef.Direction != TEXT("input") && ParamDef.Direction != TEXT("output"))
		{
			OutError = FString::Printf(TEXT("INVALID_PARAMETER: parameter '%s': direction '%s' is neither 'input' nor 'output' (a return value is 'output')"),
				*ParamDef.Name, *ParamDef.Direction);
			return false;
		}
		if (ParamDef.Direction == TEXT("output") && !ParamDef.DefaultValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("INVALID_PARAMETER: parameter '%s': defaultValue is for inputs; an output has no default"), *ParamDef.Name);
			return false;
		}

		OutParameters.Add(ParamDef);
	}

	return true;
}

bool FN2CMcpCreateBlueprintFunctionTool::ParseFunctionFlags(const TSharedPtr<FJsonObject>& FlagsObject,
	FFunctionFlags& OutFlags, FString& OutError) const
{
	// Parse all optional flags
	FlagsObject->TryGetBoolField(TEXT("isPure"), OutFlags.bIsPure);
	FlagsObject->TryGetBoolField(TEXT("isCallInEditor"), OutFlags.bIsCallInEditor);
	FlagsObject->TryGetBoolField(TEXT("isStatic"), OutFlags.bIsStatic);
	FlagsObject->TryGetBoolField(TEXT("isConst"), OutFlags.bIsConst);

	FlagsObject->TryGetStringField(TEXT("category"), OutFlags.Category);
	FlagsObject->TryGetStringField(TEXT("keywords"), OutFlags.Keywords);
	FlagsObject->TryGetStringField(TEXT("tooltip"), OutFlags.Tooltip);
	FlagsObject->TryGetStringField(TEXT("accessSpecifier"), OutFlags.AccessSpecifier);

	OutFlags.AccessSpecifier = OutFlags.AccessSpecifier.TrimStartAndEnd().ToLower();
	if (OutFlags.AccessSpecifier.IsEmpty())
	{
		OutFlags.AccessSpecifier = TEXT("public");
	}
	if (OutFlags.AccessSpecifier != TEXT("public") && OutFlags.AccessSpecifier != TEXT("protected") && OutFlags.AccessSpecifier != TEXT("private"))
	{
		OutError = FString::Printf(TEXT("INVALID_FLAGS: accessSpecifier '%s' is not 'public', 'protected' or 'private'"), *OutFlags.AccessSpecifier);
		return false;
	}

	// Refused rather than ignored (card 0280): before, isOverride was read and did nothing, and the net flags made a
	// function graph an RPC, which only a custom event can be in a Blueprint
	bool bFlag = false;
	if (FlagsObject->TryGetBoolField(TEXT("isOverride"), bFlag) && bFlag)
	{
		OutError = TEXT("INVALID_FLAGS: isOverride is not supported: create-blueprint-function makes a new function. Override a parent function from the Blueprint editor's Functions > Override menu.");
		return false;
	}
	bool bReliable = false;
	bool bMulticast = false;
	FlagsObject->TryGetBoolField(TEXT("isReliableReplication"), bReliable);
	FlagsObject->TryGetBoolField(TEXT("isMulticast"), bMulticast);
	if (bReliable || bMulticast)
	{
		OutError = TEXT("INVALID_FLAGS: isReliableReplication and isMulticast are not supported: a Blueprint function cannot be an RPC (only a custom event can be replicated)");
		return false;
	}

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
