// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool that lists all overridable functions from parent classes and implementable interface functions.
 * This includes BlueprintImplementableEvent and BlueprintNativeEvent functions.
 */
class FN2CMcpListOverridableFunctionsTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	/**
	 * Resolves the target Blueprint from a path or the focused editor.
	 * @param BlueprintPath Optional path to a specific Blueprint
	 * @return The resolved Blueprint or nullptr if not found
	 */
	// class UBlueprint* ResolveTargetBlueprint(const FString& BlueprintPath) const; // Removed

	/**
	 * Collects all overridable functions from parent classes and interfaces.
	 * @param Blueprint The Blueprint to analyze
	 * @param bIncludeImplemented Whether to include already implemented functions
	 * @return JSON object containing function information
	 */
	TSharedPtr<FJsonObject> CollectOverridableFunctions(const class UBlueprint* Blueprint, bool bIncludeImplemented) const;

	/**
	 * Checks if a function can be overridden in Blueprints, as the editor's Override menu decides it.
	 * @param Blueprint The Blueprint that would override it
	 * @param Function The function to check
	 * @return True if the function can be overridden
	 */
	bool CanOverrideFunction(const class UBlueprint* Blueprint, const class UFunction* Function) const;

	/** The function that first declared Function: its super function chain's end (a Blueprint parent's event stub leads
	 * to the native event, an interface implementation to the interface function) */
	static const class UFunction* GetDeclaringFunction(const class UFunction* Function);

	/**
	 * Checks if a function is already implemented in the Blueprint.
	 * @param Blueprint The Blueprint to check
	 * @param Function The declaring function (GetDeclaringFunction) to check for
	 * @return True if an event node, a function graph or an interface graph implements it
	 */
	bool IsFunctionImplemented(const class UBlueprint* Blueprint, const class UFunction* Function) const;

	/**
	 * Creates a JSON object with function information.
	 * @param Function The function to describe
	 * @param bIsImplemented Whether the function is already implemented
	 * @param SourceType Where the function comes from (e.g., "ParentClass", "Interface")
	 * @param SourceName The name of the parent class or interface
	 * @return JSON object with function details
	 */
	TSharedPtr<FJsonObject> CreateFunctionInfo(
		const class UFunction* Function, 
		bool bIsImplemented,
		const FString& SourceType,
		const FString& SourceName) const;

	/**
	 * Extracts parameter information from a function.
	 * @param Function The function to analyze
	 * @param OutInputParams Array to fill with input parameter information
	 * @param OutOutputParams Array to fill with output parameter information
	 */
	void ExtractFunctionParameters(
		const class UFunction* Function,
		TArray<TSharedPtr<FJsonValue>>& OutInputParams,
		TArray<TSharedPtr<FJsonValue>>& OutOutputParams) const;

	/**
	 * Converts a property to a JSON representation.
	 * @param Property The property to convert
	 * @return JSON object representing the property type
	 */
	TSharedPtr<FJsonObject> ConvertPropertyToJson(const class FProperty* Property) const;

	/**
	 * Gets function metadata as JSON.
	 * @param Function The function to get metadata from
	 * @return JSON object containing function metadata
	 */
	TSharedPtr<FJsonObject> GetFunctionMetadata(const class UFunction* Function) const;
};
