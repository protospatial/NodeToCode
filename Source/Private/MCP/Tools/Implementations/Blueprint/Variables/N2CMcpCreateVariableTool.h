// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraphPin.h"

// Forward declarations
class UBlueprint;

/**
 * MCP tool that creates a new variable in the active Blueprint
 * Uses type identifiers from search-variable-types to create variables with specific types
 */
class FN2CMcpCreateVariableTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	// Variable creation settings
	struct FVariableCreationSettings
	{
		FString VariableName;
		FString TypeIdentifier;
		FString DefaultValue;
		FString Category = TEXT("Default");
		bool bInstanceEditable = false;
		bool bBlueprintReadOnly = false;
		FString Tooltip;
		/** none, replicated or repnotify */
		FString ReplicationCondition = TEXT("none");
	};

	/**
	 * Adds the member variable and sets its flags on the variable description (FBPVariableDescription), in one
	 * FScopedTransaction, so one undo removes it with its flags and the OnRep function a repnotify variable gets
	 * (card 0154). The name is made unique with FBlueprintEditorUtils::FindUniqueKismetName. Marks the Blueprint
	 * modified; inside a tool call (FN2CMcpToolCallScope) the compile runs once when the call ends. Game thread.
	 * @return The variable's name, or NAME_None and OutError (nothing is changed then)
	 */
	static FName CreateMemberVariable(UBlueprint* Blueprint, const FVariableCreationSettings& Settings,
		const FEdGraphPinType& PinType, FString& OutError);

	/**
	 * False and an error for a Blueprint the editor does not let you add variables to (Interface, Function Library and
	 * Macro Library Blueprints, as FBlueprintEditor::NewDocument_IsVisibleForType(CGT_NewVariable)) or one that is
	 * regenerating on load
	 */
	static bool ValidateBlueprintModifiable(const UBlueprint* Blueprint, FString& OutError);

private:
	// Result building
	TSharedPtr<FJsonObject> BuildSuccessResult(const UBlueprint* Blueprint, const FString& RequestedName,
		FName ActualName, const FEdGraphPinType& ResolvedPinType, const FString& ContainerType) const;

	// Validation
	bool ValidateVariableName(const FString& VariableName, FString& OutError) const;
};
