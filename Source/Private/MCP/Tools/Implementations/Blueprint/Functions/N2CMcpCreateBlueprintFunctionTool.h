// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraphPin.h"

// Forward declarations
class UBlueprint;
class UEdGraph;
class UK2Node_FunctionEntry;
class UK2Node_FunctionResult;

/**
 * MCP tool that creates a new Blueprint function with specified parameters
 * Supports various parameter types with containers and automatically opens the created function
 *
 * Everything the call asks for is checked before the Blueprint is touched (card 0280): the name with the editor's own
 * FKismetNameValidator (no graph, variable, component, dispatcher or parent function may have it, because
 * CreateNewGraph renames a graph it collides with), each parameter's direction, type and default value, and the flags.
 * A refused call changes nothing.
 */
class FN2CMcpCreateBlueprintFunctionTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	// Parameter definition structure
	struct FParameterDefinition
	{
		FString Name;
		FString Direction = TEXT("input");  // "input" or "output", lower case once parsed
		FString Type;
		FString SubType;
		FString Container = TEXT("none");  // "none", "array", "set", "map"
		FString KeyType;  // For map containers
		bool bIsReference = false;
		bool bIsConst = false;
		FString DefaultValue;

		// Filled by ResolveParameters, before the graph is made
		FEdGraphPinType PinType;
		/** DefaultValue in the form the pin stores it (FN2CMcpPinDefault::Prepare) */
		FString PreparedDefault;
	};

	/** A pin the call made, as the result reports it */
	struct FCreatedPin
	{
		FString RequestedName;
		FString ActualName;
		FString PinId;
		FString Direction;
		FString DefaultValue;
	};

	// Function flags structure
	struct FFunctionFlags
	{
		bool bIsPure = false;
		bool bIsCallInEditor = false;
		bool bIsStatic = false;
		bool bIsConst = false;
		FString Category = TEXT("Default");
		FString Keywords;
		FString Tooltip;
		FString AccessSpecifier = TEXT("public");  // "public", "protected", "private", lower case once parsed
	};

	// Main operations
	bool ValidateFunctionName(const UBlueprint* Blueprint, const FString& FunctionName, FString& OutError) const;
	bool ValidateFunctionFlags(const UBlueprint* Blueprint, const FFunctionFlags& Flags, FString& OutError) const;
	/** Resolves every parameter's type and checks its default value against a scratch pin of that type */
	bool ResolveParameters(TArray<FParameterDefinition>& Parameters, FString& OutError) const;
	UEdGraph* CreateFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName);
	void CreateFunctionParameters(UK2Node_FunctionEntry* EntryNode, UK2Node_FunctionResult* ResultNode,
		const TArray<FParameterDefinition>& Parameters, TArray<FCreatedPin>& OutPins, TArray<FString>& OutWarnings);
	void SetFunctionMetadata(UK2Node_FunctionEntry* EntryNode, const FFunctionFlags& Flags);
	FGuid GetOrCreateFunctionGuid(UEdGraph* FunctionGraph) const;

	// Parameter creation
	UK2Node_FunctionResult* FindOrCreateResultNode(UK2Node_FunctionEntry* EntryNode);

	// Result building
	TSharedPtr<FJsonObject> BuildSuccessResult(const UBlueprint* Blueprint, const FString& FunctionName,
		const FGuid& FunctionGuid, UEdGraph* FunctionGraph, const TArray<FCreatedPin>& Pins, const TArray<FString>& Warnings) const;

	// Parsing helpers
	bool ParseParameters(const TSharedPtr<FJsonValue>& ParametersValue, TArray<FParameterDefinition>& OutParameters, FString& OutError) const;
	bool ParseFunctionFlags(const TSharedPtr<FJsonObject>& FlagsObject, FFunctionFlags& OutFlags, FString& OutError) const;

	// Editor integration
	void OpenFunctionInEditor(UBlueprint* Blueprint, UEdGraph* FunctionGraph);
};
