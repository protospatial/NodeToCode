// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

// Forward declarations
class UBlueprint;
class UEdGraph;
class UK2Node_CallFunction;
class UK2Node_FunctionEntry;

/**
 * MCP tool that deletes a Blueprint function identified by its GUID
 * Supports reference detection, force deletion, and full undo/redo support
 */
class FN2CMcpDeleteBlueprintFunctionTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	// Structures for tracking references and results
	struct FRemovedReference
	{
		FString GraphName;
		FString NodeId;
		FString NodeTitle;
		/** Set for a call site in another Blueprint */
		FString BlueprintPath;
	};

	/** Every call to the function, from its own Blueprint and from others (card 0280) */
	struct FFunctionReferences
	{
		/** Call nodes in the function's own Blueprint: force removes them */
		TArray<UK2Node_CallFunction*> Local;
		/** Call nodes in other loaded Blueprints: never removed; force deletes the function anyway */
		TArray<UK2Node_CallFunction*> External;
		/** Blueprint packages the asset registry lists as referencing this one that are not loaded, so not checked */
		TArray<FString> UncheckedReferencers;
	};

	// Main operations
	// UBlueprint* ResolveTargetBlueprint(const FString& BlueprintPath) const; // Removed
	UEdGraph* FindFunctionByGuid(UBlueprint* Blueprint, const FGuid& FunctionGuid) const;
	
	// Validation methods
	bool ValidateFunctionDeletion(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bForce, FString& OutError) const;
	bool IsSystemFunction(const FString& FunctionName) const;
	bool CheckForOverrides(UBlueprint* Blueprint, const FString& FunctionName, TArray<UClass*>& OutChildClasses) const;
	
	// Reference detection
	void FindFunctionReferences(UBlueprint* Blueprint, const FName FunctionName, FFunctionReferences& OutReferences) const;
	/** The call node calls FunctionName of Blueprint's class (its skeleton or generated class), not a same-named function of another class */
	static bool CallsBlueprintFunction(const UK2Node_CallFunction* CallNode, const UBlueprint* Blueprint, const FName FunctionName);
	void CollectReferenceInfo(const TArray<UK2Node_CallFunction*>& CallNodes, 
		TArray<FRemovedReference>& OutReferences) const;
	
	// Deletion operations
	bool RemoveFunctionReferences(UBlueprint* Blueprint, const TArray<UK2Node_CallFunction*>& CallNodes,
		TArray<FRemovedReference>& OutRemovedReferences);
	/** Closes the Blueprint editor's tabs on the graph and its subgraphs, so no tab is left on a graph the Blueprint no longer owns */
	void CloseFunctionTabs(UBlueprint* Blueprint, UEdGraph* FunctionGraph) const;
	bool DeleteFunctionGraph(UBlueprint* Blueprint, UEdGraph* FunctionGraph);
	
	// Post-deletion operations
	void RefreshBlueprintEditor(UBlueprint* Blueprint) const;
	void ShowDeletionNotification(const FString& FunctionName, bool bSuccess) const;
	
	// Result building
	TSharedPtr<FJsonObject> BuildSuccessResult(const FString& FunctionName, const FGuid& FunctionGuid,
		const FString& BlueprintPath, const TArray<FRemovedReference>& RemovedReferences,
		const TArray<FRemovedReference>& ExternalReferences, const TArray<FString>& UncheckedReferencers) const;
	
	// Helper methods
	// UBlueprint* GetFocusedBlueprint() const; // Removed
	FString GetFunctionDisplayName(UEdGraph* FunctionGraph) const;
	UK2Node_FunctionEntry* GetFunctionEntryNode(UEdGraph* FunctionGraph) const;
};
