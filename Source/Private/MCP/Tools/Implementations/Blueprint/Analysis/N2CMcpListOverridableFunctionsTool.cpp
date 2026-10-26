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
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"

REGISTER_MCP_TOOL(FN2CMcpListOverridableFunctionsTool)

FMcpToolDefinition FN2CMcpListOverridableFunctionsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("list-overridable-functions"),
		TEXT("Lists all functions that can be overridden from parent classes and interfaces"),
		TEXT("Blueprint Discovery")
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
	
	// Track processed functions to avoid duplicates
	TSet<FName> ProcessedFunctions;
	
	// Process parent class hierarchy
	UClass* CurrentClass = Blueprint->ParentClass;
	while (CurrentClass)
	{
		// Don't go beyond UObject
		if (CurrentClass == UObject::StaticClass())
		{
			break;
		}
		
		// Iterate through all functions in the class
		for (TFieldIterator<UFunction> FuncIt(CurrentClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			if (!Function || ProcessedFunctions.Contains(Function->GetFName()))
			{
				continue;
			}
			
			// Check if this function can be overridden
			if (CanOverrideFunction(Function))
			{
				bool bIsImplemented = IsFunctionImplemented(Blueprint, Function);
				
				// Skip implemented functions if not requested
				if (bIsImplemented && !bIncludeImplemented)
				{
					continue;
				}
				
				ProcessedFunctions.Add(Function->GetFName());
				
				TSharedPtr<FJsonObject> FunctionInfo = CreateFunctionInfo(
					Function, 
					bIsImplemented,
					TEXT("ParentClass"),
					CurrentClass->GetName()
				);
				
				if (FunctionInfo.IsValid())
				{
					FunctionsArray.Add(MakeShareable(new FJsonValueObject(FunctionInfo)));
				}
			}
		}
		
		CurrentClass = CurrentClass->GetSuperClass();
	}
	
	// Process implemented interfaces
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		UClass* InterfaceClass = InterfaceDesc.Interface;
		if (!InterfaceClass)
		{
			continue;
		}
		
		// Iterate through all functions in the interface
		for (TFieldIterator<UFunction> FuncIt(InterfaceClass); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			if (!Function || ProcessedFunctions.Contains(Function->GetFName()))
			{
				continue;
			}
			
			// All interface functions with BlueprintImplementableEvent or BlueprintNativeEvent can be implemented
			if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
			{
				bool bIsImplemented = IsFunctionImplemented(Blueprint, Function);
				
				// Skip implemented functions if not requested
				if (bIsImplemented && !bIncludeImplemented)
				{
					continue;
				}
				
				ProcessedFunctions.Add(Function->GetFName());
				
				TSharedPtr<FJsonObject> FunctionInfo = CreateFunctionInfo(
					Function,
					bIsImplemented,
					TEXT("Interface"),
					InterfaceClass->GetName()
				);
				
				if (FunctionInfo.IsValid())
				{
					FunctionsArray.Add(MakeShareable(new FJsonValueObject(FunctionInfo)));
				}
			}
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

bool FN2CMcpListOverridableFunctionsTool::CanOverrideFunction(const UFunction* Function) const
{
	if (!Function)
	{
		return false;
	}
	
	// Function must be marked as BlueprintEvent (BlueprintImplementableEvent or BlueprintNativeEvent)
	if (!Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
	{
		return false;
	}
	
	// Skip functions that are final
	if (Function->HasAnyFunctionFlags(FUNC_Final))
	{
		return false;
	}
	
	// Skip deprecated functions
	if (Function->HasMetaData(TEXT("DeprecatedFunction")) || Function->HasMetaData(TEXT("Deprecated")))
	{
		return false;
	}
	
	// Skip editor-only functions in game blueprints
	if (Function->HasAnyFunctionFlags(FUNC_EditorOnly))
	{
		// Could add logic here to check if the blueprint is editor-only
		// For now, we'll include them and let the user decide
	}
	
	return true;
}

bool FN2CMcpListOverridableFunctionsTool::IsFunctionImplemented(const UBlueprint* Blueprint, const UFunction* Function) const
{
	if (!Blueprint || !Function)
	{
		return false;
	}
	
	// Check if there's an override for this function
	UK2Node_Event* EventNode = FBlueprintEditorUtils::FindOverrideForFunction(
		const_cast<UBlueprint*>(Blueprint), 
		Function->GetOwnerClass(), 
		Function->GetFName()
	);
	
	if (EventNode)
	{
		return true;
	}
	
	// Also check function graphs
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == Function->GetFName())
		{
			return true;
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
	
	// Function type
	FString FunctionType = TEXT("Event");
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
	{
		FunctionType = TEXT("Function");
	}
	FunctionInfo->SetStringField(TEXT("functionType"), FunctionType);
	
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
