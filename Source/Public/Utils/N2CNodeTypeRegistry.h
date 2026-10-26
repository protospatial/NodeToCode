// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"

class UClass;
class UK2Node;
class UK2Node_Variable;

/**
 * @class FN2CNodeTypeRegistry
 * @brief Maps Blueprint node classes to EN2CNodeType
 *
 * GetNodeType walks the node's class up its super classes and the nearest mapped ancestor wins (card 0234): at each
 * level a class mapping (RegisterNodeClass) is checked, then a class-name mapping (RegisterNodeType, the class name
 * without its "K2Node_" prefix, so a node class from another plugin can be mapped without a module dependency on it).
 * A node with no mapped ancestor is an Event when its class implements IK2Node_EventNodeInterface, and otherwise
 * Unknown: N2C JSON then says "Unknown" with the node's "class", and the base processor fills it.
 * Variable gets and sets are then refined by scope and access (DetermineVariableNodeType).
 *
 * Third-party nodes: call RegisterNodeClass or RegisterNodeType (for example from a module's StartupModule) to give
 * a node class, and every class derived from it, a type.
 */
class NODETOCODE_API FN2CNodeTypeRegistry
{
public:
    /** Get the singleton instance */
    static FN2CNodeTypeRegistry& Get();

    /** Register a node type for a class name, without the "K2Node_" prefix (for example "CallFunction") */
    void RegisterNodeType(const FName& ClassName, EN2CNodeType NodeType);

    /** Register a node type for a class; it wins over a class-name mapping of the same class */
    void RegisterNodeClass(const UClass* Class, EN2CNodeType NodeType);

    /** The node's type: its nearest mapped ancestor's, refined for variable nodes; Unknown for null or unmapped */
    EN2CNodeType GetNodeType(const UK2Node* Node);

    /**
     * The type of a variable get or set: access first (get, set, other), then scope. A local variable is
     * LocalVariableGet or LocalVariableSet, a function parameter read is FunctionParameter and a parameter write
     * LocalVariableSet (Blueprint writes a local copy), a member VariableGet or VariableSet. When the variable's
     * property does not resolve, the access and the reference's scope still decide.
     */
    EN2CNodeType DetermineVariableNodeType(const UK2Node_Variable* Node);

private:
    /** Constructor - initializes default mappings */
    FN2CNodeTypeRegistry();

    /** Mappings from class names (without the "K2Node_" prefix) to node types */
    TMap<FName, EN2CNodeType> ClassNameMappings;

    /** Mappings from class pointers to node types */
    TMap<const UClass*, EN2CNodeType> ClassMappings;

    /** Initialize default mappings */
    void InitializeDefaultMappings();

    /** Get base node type from class name (strips K2Node_ prefix) */
    static FString GetBaseNodeType(const FString& ClassName);

    /** The mapping of Class itself (class mapping, then class-name mapping), if any */
    bool FindOwnMapping(const UClass* Class, EN2CNodeType& OutType) const;
};
