// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpListOverridableFunctionsTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CEditorIntegration.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Event.h"
#include "EdGraphSchema_K2.h"
#include "ObjectEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"

REGISTER_MCP_TOOL(FN2CMcpListOverridableFunctionsTool)

FMcpToolDefinition FN2CMcpListOverridableFunctionsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("list-overridable-functions"),
		TEXT("Lists the functions a Blueprint can override from its parent classes and its interfaces (its own and its parents'), as the editor's Override menu does. functionType says how to implement one: Event as an event node in an event graph, Function as a function graph; isImplemented counts event nodes, function graphs and interface graphs"),
		N2CMcpToolsets::BlueprintDiscovery
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// blueprintPath property (optional)
	TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
	BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
	BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Asset path of the Blueprint (e.g., '/Game/Blueprints/MyActor.MyActor'). If not provided, uses focused Blueprint."));
	Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

	// includeImplemented property (optional)
	TSharedPtr<FJsonObject> IncludeImplementedProp = MakeShareable(new FJsonObject);
	IncludeImplementedProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeImplementedProp->SetStringField(TEXT("description"), TEXT("Whether to include functions that are already implemented/overridden"));
	IncludeImplementedProp->SetBoolField(TEXT("default"), false);
	Properties->SetObjectField(TEXT("includeImplemented"), IncludeImplementedProp);

	// filterByCategory property (optional)
	TSharedPtr<FJsonObject> FilterByCategoryProp = MakeShareable(new FJsonObject);
	FilterByCategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	FilterByCategoryProp->SetStringField(TEXT("description"), TEXT("Filter functions by category (e.g., 'Input', 'Collision', 'Animation')"));
	Properties->SetObjectField(TEXT("filterByCategory"), FilterByCategoryProp);

	// searchTerm property (optional)
	TSharedPtr<FJsonObject> SearchTermProp = MakeShareable(new FJsonObject);
	SearchTermProp->SetStringField(TEXT("type"), TEXT("string"));
	SearchTermProp->SetStringField(TEXT("description"), TEXT("Search term to filter function names"));
	Properties->SetObjectField(TEXT("searchTerm"), SearchTermProp);
	
	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// No required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	// Mark as read-only
	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
	
	// Results carry Blueprint text (tooltips of functions a Blueprint parent declares): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpListOverridableFunctionsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FN2CMcpArgumentParser ArgParser(Arguments);
		
		// Optional parameters
		FString BlueprintPath = ArgParser.GetOptionalString(TEXT("blueprintPath"));
		bool bIncludeImplemented = ArgParser.GetOptionalBool(TEXT("includeImplemented"), false);
		FString FilterByCategory = ArgParser.GetOptionalString(TEXT("filterByCategory"));
		FString SearchTerm = ArgParser.GetOptionalString(TEXT("searchTerm"));
		
		// Resolve target Blueprint
		FString ResolveError;
		UBlueprint* TargetBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ResolveError);
		if (!TargetBlueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(ResolveError);
		}
		
		// Collect overridable functions
		TSharedPtr<FJsonObject> Result = CollectOverridableFunctions(TargetBlueprint, bIncludeImplemented);
		
		// Apply filters if provided
		if (!FilterByCategory.IsEmpty() || !SearchTerm.IsEmpty())
		{
			const TArray<TSharedPtr<FJsonValue>>* FunctionsArray = nullptr;
			if (Result->TryGetArrayField(TEXT("functions"), FunctionsArray) && FunctionsArray)
			{
				TArray<TSharedPtr<FJsonValue>> FilteredFunctions;
				
				for (const TSharedPtr<FJsonValue>& FuncValue : *FunctionsArray)
				{
					const TSharedPtr<FJsonObject>* FuncObject = nullptr;
					if (FuncValue->TryGetObject(FuncObject) && FuncObject && FuncObject->IsValid())
					{
						bool bInclude = true;
						
						// Apply category filter
						if (!FilterByCategory.IsEmpty())
						{
							FString Category;
							if ((*FuncObject)->TryGetStringField(TEXT("category"), Category))
							{
								bInclude = Category.Contains(FilterByCategory, ESearchCase::IgnoreCase);
							}
							else
							{
								bInclude = false;
							}
						}
						
						// Apply search term filter
						if (bInclude && !SearchTerm.IsEmpty())
						{
							FString FuncName;
							if ((*FuncObject)->TryGetStringField(TEXT("name"), FuncName))
							{
								bInclude = FuncName.Contains(SearchTerm, ESearchCase::IgnoreCase);
							}
						}
						
						if (bInclude)
						{
							FilteredFunctions.Add(FuncValue);
						}
					}
				}
				
				Result->SetArrayField(TEXT("functions"), FilteredFunctions);
				Result->SetNumberField(TEXT("functionCount"), FilteredFunctions.Num());
			}
		}
		
		// Add metadata
		Result->SetStringField(TEXT("blueprintName"), TargetBlueprint->GetName());
		Result->SetStringField(TEXT("blueprintPath"), TargetBlueprint->GetPathName());
		
		// Add parent class and interface info
		if (TargetBlueprint->ParentClass)
		{
			Result->SetStringField(TEXT("parentClass"), TargetBlueprint->ParentClass->GetName());
		}
		
		TArray<TSharedPtr<FJsonValue>> InterfacesArray;
		for (const FBPInterfaceDescription& InterfaceDesc : TargetBlueprint->ImplementedInterfaces)
		{
			if (InterfaceDesc.Interface)
			{
				InterfacesArray.Add(MakeShareable(new FJsonValueString(InterfaceDesc.Interface->GetName())));
			}
		}
		Result->SetArrayField(TEXT("implementedInterfaces"), InterfacesArray);
		
		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		
		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}

TSharedPtr<FJsonObject> FN2CMcpListOverridableFunctionsTool::CollectOverridableFunctions(const UBlueprint* Blueprint, bool bIncludeImplemented) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> FunctionsArray;

	if (!Blueprint || !Blueprint->ParentClass)
	{
		Result->SetArrayField(TEXT("functions"), FunctionsArray);
		Result->SetNumberField(TEXT("functionCount"), 0);
		return Result;
	}

	// One entry per function name, the first (most derived) declaration found, as the editor's Override menu lists them
	TSet<FName> ProcessedFunctions;

	// Adds one candidate. Function is the declaration found; it is described, and its implementation looked up, by the
	// function that first declared it (card 0289): a Blueprint parent that overrides an event carries a stub UFunction
	// whose owner is that parent's class, while the child's event node names the class that declared the event.
	auto AddCandidate = [&](const UFunction* Function)
	{
		if (!Function || ProcessedFunctions.Contains(Function->GetFName()))
		{
			return;
		}
		ProcessedFunctions.Add(Function->GetFName());
		const UFunction* Declaration = GetDeclaringFunction(Function);
		const bool bIsImplemented = IsFunctionImplemented(Blueprint, Declaration);
		if (bIsImplemented && !bIncludeImplemented)
		{
			return;
		}
		// Where it was declared: an interface (a Blueprint parent's implementation of an interface function leads back
		// to the interface too) or a class
		const UClass* DeclaringClass = Declaration->GetOwnerClass();
		const bool bFromInterface = DeclaringClass && DeclaringClass->HasAnyClassFlags(CLASS_Interface);
		TSharedPtr<FJsonObject> FunctionInfo = CreateFunctionInfo(Declaration, bIsImplemented,
			bFromInterface ? TEXT("Interface") : TEXT("ParentClass"), DeclaringClass ? DeclaringClass->GetName() : FString());
		if (FunctionInfo.IsValid())
		{
			FunctionsArray.Add(MakeShareable(new FJsonValueObject(FunctionInfo)));
		}
	};

	// Interfaces first: the Blueprint's own and those its parent classes implement (FindImplementedInterfaces with
	// bGetAllInterfaces; card 0289), each with the interfaces it extends. First, because a Blueprint parent that
	// implements an interface carries a UFunction of each interface event that has no super function, which the class
	// walk below would otherwise list as the parent's own
	TArray<UClass*> Interfaces;
	FBlueprintEditorUtils::FindImplementedInterfaces(Blueprint, /*bGetAllInterfaces=*/true, Interfaces);
	for (UClass* InterfaceClass : Interfaces)
	{
		if (!InterfaceClass)
		{
			continue;
		}
		for (TFieldIterator<UFunction> FuncIt(InterfaceClass, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
		{
			const UFunction* Function = *FuncIt;
			if (UEdGraphSchema_K2::CanKismetOverrideFunction(Function))
			{
				AddCandidate(Function);
			}
		}
	}

	// The parent class and its supers, as SMyBlueprint's Override menu walks them: every function the K2 schema lets a
	// Blueprint override, unless the class hides it or the Blueprint type refuses it
	UClass* ParentClass = Blueprint->ParentClass;
	for (TFieldIterator<UFunction> FuncIt(ParentClass, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
	{
		const UFunction* Function = *FuncIt;
		if (CanOverrideFunction(Blueprint, Function))
		{
			AddCandidate(Function);
		}
	}

	// Sort functions by name for consistent output
	FunctionsArray.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		const TSharedPtr<FJsonObject>* ObjA = nullptr;
		const TSharedPtr<FJsonObject>* ObjB = nullptr;

		if (A->TryGetObject(ObjA) && B->TryGetObject(ObjB) && ObjA && ObjB)
		{
			FString NameA, NameB;
			(*ObjA)->TryGetStringField(TEXT("name"), NameA);
			(*ObjB)->TryGetStringField(TEXT("name"), NameB);
			return NameA < NameB;
		}

		return false;
	});

	Result->SetArrayField(TEXT("functions"), FunctionsArray);
	Result->SetNumberField(TEXT("functionCount"), FunctionsArray.Num());

	return Result;
}

const UFunction* FN2CMcpListOverridableFunctionsTool::GetDeclaringFunction(const UFunction* Function)
{
	const UFunction* Declaration = Function;
	while (Declaration && Declaration->GetSuperFunction())
	{
		Declaration = Declaration->GetSuperFunction();
	}
	return Declaration;
}

bool FN2CMcpListOverridableFunctionsTool::CanOverrideFunction(const UBlueprint* Blueprint, const UFunction* Function) const
{
	// The engine's own rule (a BlueprintEvent that is not a delegate, not BlueprintInternalUseOnly, and not deprecated
	// unless the editor exposes deprecated functions), plus the two checks SMyBlueprint applies to its Override menu
	return Blueprint && Function
		&& UEdGraphSchema_K2::CanKismetOverrideFunction(Function)
		&& !FObjectEditorUtils::IsFunctionHiddenFromClass(Function, Blueprint->ParentClass)
		&& Blueprint->AllowFunctionOverride(Function);
}

bool FN2CMcpListOverridableFunctionsTool::IsFunctionImplemented(const UBlueprint* Blueprint, const UFunction* Declaration) const
{
	if (!Blueprint || !Declaration)
	{
		return false;
	}

	// An event node: FindOverrideForFunction matches nodes whose event class is a child of the class passed, so it must
	// be the declaring class (an event node in a child of a Blueprint parent names the native declaring class)
	if (FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, Declaration->GetOwnerClass(), Declaration->GetFName()))
	{
		return true;
	}

	// A function graph: an override of a function with outputs, or of an inherited interface function
	const FName Name = Declaration->GetFName();
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return true;
		}
	}

	// An interface function graph: the Blueprint's own interfaces keep their function graphs here, not in FunctionGraphs
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		for (const UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			if (Graph && Graph->GetFName() == Name)
			{
				return true;
			}
		}
	}

	return false;
}

TSharedPtr<FJsonObject> FN2CMcpListOverridableFunctionsTool::CreateFunctionInfo(
	const UFunction* Function,
	bool bIsImplemented,
	const FString& SourceType,
	const FString& SourceName) const
{
	if (!Function)
	{
		return nullptr;
	}
	
	TSharedPtr<FJsonObject> FunctionInfo = MakeShareable(new FJsonObject);
	
	// Basic info
	FunctionInfo->SetStringField(TEXT("name"), Function->GetName());
	FunctionInfo->SetStringField(TEXT("displayName"), Function->GetDisplayNameText().ToString());
	FunctionInfo->SetBoolField(TEXT("isImplemented"), bIsImplemented);
	FunctionInfo->SetStringField(TEXT("sourceType"), SourceType);
	FunctionInfo->SetStringField(TEXT("sourceName"), SourceName);
	
	// How the override is made: an event node when the K2 schema lets the function be placed as an event (no outputs,
	// not const or static, no ForceAsFunction), else a function graph. Whether Blueprints may call it is a separate
	// question (card 0289: functionType used to follow BlueprintCallable)
	FunctionInfo->SetStringField(TEXT("functionType"),
		UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function) ? TEXT("Event") : TEXT("Function"));
	FunctionInfo->SetBoolField(TEXT("isCallable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	
	// Implementation type
	FString ImplementationType = TEXT("BlueprintImplementableEvent");
	if (Function->HasAnyFunctionFlags(FUNC_Native))
	{
		ImplementationType = TEXT("BlueprintNativeEvent");
	}
	FunctionInfo->SetStringField(TEXT("implementationType"), ImplementationType);
	
	// Category
	FString Category = Function->GetMetaData(TEXT("Category"));
	if (Category.IsEmpty())
	{
		Category = TEXT("Default");
	}
	FunctionInfo->SetStringField(TEXT("category"), Category);
	
	// Extract parameters
	TArray<TSharedPtr<FJsonValue>> InputParams;
	TArray<TSharedPtr<FJsonValue>> OutputParams;
	ExtractFunctionParameters(Function, InputParams, OutputParams);
	
	FunctionInfo->SetArrayField(TEXT("inputs"), InputParams);
	FunctionInfo->SetArrayField(TEXT("outputs"), OutputParams);
	
	// Get metadata
	FunctionInfo->SetObjectField(TEXT("metadata"), GetFunctionMetadata(Function));
	
	// Function flags
	TSharedPtr<FJsonObject> Flags = MakeShareable(new FJsonObject);
	Flags->SetBoolField(TEXT("isConst"), Function->HasAnyFunctionFlags(FUNC_Const));
	Flags->SetBoolField(TEXT("isStatic"), Function->HasAnyFunctionFlags(FUNC_Static));
	Flags->SetBoolField(TEXT("isReliable"), Function->HasAnyFunctionFlags(FUNC_NetReliable));
	Flags->SetBoolField(TEXT("isServer"), Function->HasAnyFunctionFlags(FUNC_NetServer));
	Flags->SetBoolField(TEXT("isClient"), Function->HasAnyFunctionFlags(FUNC_NetClient));
	Flags->SetBoolField(TEXT("isMulticast"), Function->HasAnyFunctionFlags(FUNC_NetMulticast));
	Flags->SetBoolField(TEXT("isEditorOnly"), Function->HasAnyFunctionFlags(FUNC_EditorOnly));
	FunctionInfo->SetObjectField(TEXT("flags"), Flags);
	
	return FunctionInfo;
}

void FN2CMcpListOverridableFunctionsTool::ExtractFunctionParameters(
	const UFunction* Function,
	TArray<TSharedPtr<FJsonValue>>& OutInputParams,
	TArray<TSharedPtr<FJsonValue>>& OutOutputParams) const
{
	if (!Function)
	{
		return;
	}
	
	// Iterate through function properties
	for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property)
		{
			continue;
		}
		
		// Skip return value property
		if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}
		
		TSharedPtr<FJsonObject> ParamInfo = MakeShareable(new FJsonObject);
		ParamInfo->SetStringField(TEXT("name"), Property->GetName());
		ParamInfo->SetObjectField(TEXT("type"), ConvertPropertyToJson(Property));
		
		// Check if it's an output parameter
		bool bIsOutput = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
		
		if (bIsOutput)
		{
			OutOutputParams.Add(MakeShareable(new FJsonValueObject(ParamInfo)));
		}
		else
		{
			OutInputParams.Add(MakeShareable(new FJsonValueObject(ParamInfo)));
		}
	}
	
	// Check for return value
	if (Function->GetReturnProperty())
	{
		TSharedPtr<FJsonObject> ReturnInfo = MakeShareable(new FJsonObject);
		ReturnInfo->SetStringField(TEXT("name"), TEXT("ReturnValue"));
		ReturnInfo->SetObjectField(TEXT("type"), ConvertPropertyToJson(Function->GetReturnProperty()));
		OutOutputParams.Add(MakeShareable(new FJsonValueObject(ReturnInfo)));
	}
}

TSharedPtr<FJsonObject> FN2CMcpListOverridableFunctionsTool::ConvertPropertyToJson(const FProperty* Property) const
{
	TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);
	
	if (!Property)
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("unknown"));
		return TypeInfo;
	}
	
	// Get the property type
	FString TypeName = Property->GetCPPType();
	TypeInfo->SetStringField(TEXT("cppType"), TypeName);
	
	// Determine the category and additional info based on property class
	if (Property->IsA(FBoolProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("bool"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("bool"));
	}
	else if (Property->IsA(FByteProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("byte"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("byte"));
		
		// Check if it's an enum
		if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (ByteProp->Enum)
			{
				TypeInfo->SetStringField(TEXT("type"), TEXT("enum"));
				TypeInfo->SetStringField(TEXT("category"), TEXT("enum"));
				TypeInfo->SetStringField(TEXT("enumType"), ByteProp->Enum->GetName());
				TypeInfo->SetStringField(TEXT("enumPath"), ByteProp->Enum->GetPathName());
			}
		}
	}
	else if (Property->IsA(FIntProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("int"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("int"));
	}
	else if (Property->IsA(FInt64Property::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("int64"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("int64"));
	}
	else if (Property->IsA(FFloatProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("float"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("real"));
		TypeInfo->SetStringField(TEXT("subCategory"), TEXT("float"));
	}
	else if (Property->IsA(FDoubleProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("double"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("real"));
		TypeInfo->SetStringField(TEXT("subCategory"), TEXT("double"));
	}
	else if (Property->IsA(FStrProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("string"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("string"));
	}
	else if (Property->IsA(FNameProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("name"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("name"));
	}
	else if (Property->IsA(FTextProperty::StaticClass()))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("text"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("text"));
	}
	else if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("struct"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("struct"));
		if (StructProp->Struct)
		{
			TypeInfo->SetStringField(TEXT("structType"), StructProp->Struct->GetName());
			TypeInfo->SetStringField(TEXT("structPath"), StructProp->Struct->GetPathName());
		}
	}
	else if (const FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Property))
	{
		if (Property->IsA(FClassProperty::StaticClass()))
		{
			TypeInfo->SetStringField(TEXT("type"), TEXT("class"));
			TypeInfo->SetStringField(TEXT("category"), TEXT("class"));
		}
		else
		{
			TypeInfo->SetStringField(TEXT("type"), TEXT("object"));
			TypeInfo->SetStringField(TEXT("category"), TEXT("object"));
		}
		
		if (ObjectProp->PropertyClass)
		{
			TypeInfo->SetStringField(TEXT("objectType"), ObjectProp->PropertyClass->GetName());
			TypeInfo->SetStringField(TEXT("objectPath"), ObjectProp->PropertyClass->GetPathName());
		}
	}
	else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("array"));
		TypeInfo->SetStringField(TEXT("container"), TEXT("array"));
		if (ArrayProp->Inner)
		{
			TypeInfo->SetObjectField(TEXT("innerType"), ConvertPropertyToJson(ArrayProp->Inner));
		}
	}
	else if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("set"));
		TypeInfo->SetStringField(TEXT("container"), TEXT("set"));
		if (SetProp->ElementProp)
		{
			TypeInfo->SetObjectField(TEXT("elementType"), ConvertPropertyToJson(SetProp->ElementProp));
		}
	}
	else if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("map"));
		TypeInfo->SetStringField(TEXT("container"), TEXT("map"));
		if (MapProp->KeyProp)
		{
			TypeInfo->SetObjectField(TEXT("keyType"), ConvertPropertyToJson(MapProp->KeyProp));
		}
		if (MapProp->ValueProp)
		{
			TypeInfo->SetObjectField(TEXT("valueType"), ConvertPropertyToJson(MapProp->ValueProp));
		}
	}
	else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("enum"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("enum"));
		if (EnumProp->GetEnum())
		{
			TypeInfo->SetStringField(TEXT("enumType"), EnumProp->GetEnum()->GetName());
			TypeInfo->SetStringField(TEXT("enumPath"), EnumProp->GetEnum()->GetPathName());
		}
	}
	else
	{
		TypeInfo->SetStringField(TEXT("type"), TEXT("unknown"));
		TypeInfo->SetStringField(TEXT("category"), TEXT("unknown"));
	}
	
	// Add property flags
	TypeInfo->SetBoolField(TEXT("isReference"), Property->HasAnyPropertyFlags(CPF_ReferenceParm));
	TypeInfo->SetBoolField(TEXT("isConst"), Property->HasAnyPropertyFlags(CPF_ConstParm));
	
	return TypeInfo;
}

TSharedPtr<FJsonObject> FN2CMcpListOverridableFunctionsTool::GetFunctionMetadata(const UFunction* Function) const
{
	TSharedPtr<FJsonObject> Metadata = MakeShareable(new FJsonObject);
	
	if (!Function)
	{
		return Metadata;
	}
	
	// Tooltip
	FString Tooltip = Function->GetMetaData(TEXT("ToolTip"));
	if (!Tooltip.IsEmpty())
	{
		Metadata->SetStringField(TEXT("tooltip"), Tooltip);
	}
	
	// Display name
	FString DisplayName = Function->GetMetaData(TEXT("DisplayName"));
	if (!DisplayName.IsEmpty())
	{
		Metadata->SetStringField(TEXT("displayName"), DisplayName);
	}
	
	// Keywords
	FString Keywords = Function->GetMetaData(TEXT("Keywords"));
	if (!Keywords.IsEmpty())
	{
		Metadata->SetStringField(TEXT("keywords"), Keywords);
	}
	
	// Compact node title
	FString CompactNodeTitle = Function->GetMetaData(TEXT("CompactNodeTitle"));
	if (!CompactNodeTitle.IsEmpty())
	{
		Metadata->SetStringField(TEXT("compactNodeTitle"), CompactNodeTitle);
	}
	
	// Call in editor
	bool bCallInEditor = Function->GetBoolMetaData(TEXT("CallInEditor"));
	if (bCallInEditor)
	{
		Metadata->SetBoolField(TEXT("callInEditor"), true);
	}
	
	// Development only
	bool bDevelopmentOnly = Function->GetBoolMetaData(TEXT("DevelopmentOnly"));
	if (bDevelopmentOnly)
	{
		Metadata->SetBoolField(TEXT("developmentOnly"), true);
	}
	
	// Latent
	bool bLatent = Function->HasMetaData(TEXT("Latent"));
	if (bLatent)
	{
		Metadata->SetBoolField(TEXT("isLatent"), true);
		
		FString LatentInfo = Function->GetMetaData(TEXT("LatentInfo"));
		if (!LatentInfo.IsEmpty())
		{
			Metadata->SetStringField(TEXT("latentInfo"), LatentInfo);
		}
	}
	
	// WorldContext
	FString WorldContext = Function->GetMetaData(TEXT("WorldContext"));
	if (!WorldContext.IsEmpty())
	{
		Metadata->SetStringField(TEXT("worldContext"), WorldContext);
	}
	
	// BlueprintInternalUseOnly
	bool bInternalUseOnly = Function->GetBoolMetaData(TEXT("BlueprintInternalUseOnly"));
	if (bInternalUseOnly)
	{
		Metadata->SetBoolField(TEXT("internalUseOnly"), true);
	}
	
	return Metadata;
}
