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
	};

	// Main operations
	// UBlueprint* ResolveTargetBlueprint(const FString& BlueprintPath) const; // Removed
	UEdGraph* FindFunctionByGuid(UBlueprint* Blueprint, const FGuid& FunctionGuid) const;
	
	// Validation methods
	bool ValidateFunctionDeletion(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bForce, FString& OutError) const;
	bool IsSystemFunction(const FString& FunctionName) const;
	bool CheckForOverrides(UBlueprint* Blueprint, const FString& FunctionName, TArray<UClass*>& OutChildClasses) const;
	
	// Reference detection
	void FindFunctionReferences(UBlueprint* Blueprint, const FString& FunctionName, 
		TArray<UK2Node_CallFunction*>& OutCallNodes) const;
	void CollectReferenceInfo(const TArray<UK2Node_CallFunction*>& CallNodes, 
		TArray<FRemovedReference>& OutReferences) const;
	
	// Deletion operations
	bool RemoveFunctionReferences(const TArray<UK2Node_CallFunction*>& CallNodes, 
		TArray<FRemovedReference>& OutRemovedReferences);
	bool DeleteFunctionGraph(UBlueprint* Blueprint, UEdGraph* FunctionGraph);
	
	// Post-deletion operations
	void RefreshBlueprintEditor(UBlueprint* Blueprint) const;
	void ShowDeletionNotification(const FString& FunctionName, bool bSuccess) const;
	
	// Result building
	TSharedPtr<FJsonObject> BuildSuccessResult(const FString& FunctionName, const FGuid& FunctionGuid,
		const FString& BlueprintPath, const TArray<FRemovedReference>& RemovedReferences) const;
	
	// Helper methods
	// UBlueprint* GetFocusedBlueprint() const; // Removed
	FString GetFunctionDisplayName(UEdGraph* FunctionGraph) const;
	UK2Node_FunctionEntry* GetFunctionEntryNode(UEdGraph* FunctionGraph) const;
};
