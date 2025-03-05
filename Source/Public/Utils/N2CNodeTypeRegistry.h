// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"
#include "K2Node.h"

/**
 * @class FN2CNodeTypeRegistry
 * @brief Registry for mapping Blueprint node classes to N2CNodeType
 * 
 * Provides a centralized, data-driven approach to node type determination
 * that replaces the large if-else chains and switch statements.
 */
class NODETOCODE_API FN2CNodeTypeRegistry
{
public:
    /** Get the singleton instance */
    static FN2CNodeTypeRegistry& Get();
    
    /** Register node type mapping by class name */
    void RegisterNodeType(const FName& ClassName, EN2CNodeType NodeType);
    
    /** Register node type mapping by class pointer */
    void RegisterNodeClass(const UClass* Class, EN2CNodeType NodeType);
    
    /** Get node type for a specific node */
    EN2CNodeType GetNodeType(const UK2Node* Node);
    
    /** Determine variable node type */
    EN2CNodeType DetermineVariableNodeType(const UK2Node_Variable* Node);
    
private:
    /** Constructor - initializes default mappings */
    FN2CNodeTypeRegistry();
    
    /** Mappings from class names to node types */
    TMap<FName, EN2CNodeType> ClassNameMappings;
    
    /** Mappings from class pointers to node types */
    TMap<const UClass*, EN2CNodeType> ClassMappings;
    
    /** Initialize default mappings */
    void InitializeDefaultMappings();
    
    /** Get base node type from class name (strips K2Node_ prefix) */
    FString GetBaseNodeType(const FString& ClassName);
    
    /** Try to determine node type based on inheritance */
    bool MapFromInheritance(const UK2Node* Node, EN2CNodeType& OutType);
};
