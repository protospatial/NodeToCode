// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SubobjectDataHandle.h"

class UBlueprint;
class USimpleConstructionScript;
class USCS_Node;
class UActorComponent;
class USceneComponent;
class UClass;

/**
 * One component of an Actor Blueprint as the actor is built: a native component of the C++ parent class (on the class
 * default object), a component a parent Blueprint adds (its SCS), or one this Blueprint adds (its own SCS).
 */
struct FN2CComponentEntry
{
    /** The variable name agents use: the SCS node's variable name, or a native component's property name */
    FName Name;
    /** The template's object name (a native component's subobject name, CharacterMesh0 for a Character's Mesh) */
    FName ObjectName;
    UClass* Class = nullptr;
    /** The component template; for an inherited SCS node, this Blueprint's override of it when there is one */
    const UActorComponent* Template = nullptr;
    /** The SCS node, null for a native component */
    USCS_Node* Node = nullptr;
    /** The Blueprint whose SCS holds Node */
    UBlueprint* OwnerBlueprint = nullptr;
    bool bNative = false;
    /** Native or added by a parent Blueprint */
    bool bInherited = false;
    bool bScene = false;
    /** The SCS's DefaultSceneRoot node (the placeholder root the engine adds and removes) */
    bool bDefaultSceneRoot = false;
    /** Index of the component this one is attached to, INDEX_NONE at the top level */
    int32 Parent = INDEX_NONE;
    FName SocketName;
    TArray<int32> Children;
};

/** The component hierarchy of an Actor Blueprint, read without changing anything */
struct FN2CComponentTree
{
    TArray<FN2CComponentEntry> Entries;
    /** Components attached to nothing (the root, and components that are not scene components) */
    TArray<int32> TopLevel;
    /** The actor's root component, INDEX_NONE when it has none */
    int32 Root = INDEX_NONE;

    /** A component by variable name, else by template object name; INDEX_NONE when there is none */
    int32 Find(const FString& Name) const;
    /** Every component under Index, parents before their children */
    void GetDescendants(int32 Index, TArray<int32>& OutDescendants) const;
    /** The names of the scene components, for error messages */
    FString SceneComponentNames() const;
};

/**
 * Utility class for MCP tools that work with Blueprint components. Reads go through FN2CComponentTree (never changes
 * the Blueprint); add and delete go through the engine's USubobjectDataSubsystem, as the Blueprint editor's
 * Components panel does (card 0167).
 */
class NODETOCODE_API FN2CMcpComponentUtils
{
public:
    /**
     * Checks that the Blueprint can have components: an Actor Blueprint that supports a construction script (not a
     * level script, macro library, function library, interface or const Blueprint). Never creates anything.
     */
    static bool CheckComponentBlueprint(UBlueprint* Blueprint, FString& OutErrorMsg);

    /** Builds the component hierarchy (native, inherited and own components) without changing the Blueprint */
    static bool BuildComponentTree(UBlueprint* Blueprint, FN2CComponentTree& OutTree, FString& OutErrorMsg);

    /**
     * The list-components-in-actor result: {components (a tree in "children"), totalCount (every component listed),
     * rootComponent}. A component left out by bIncludeInherited or the type filter has its listed descendants moved up
     * to where it would have been.
     */
    static TSharedPtr<FJsonObject> BuildComponentHierarchy(const FN2CComponentTree& Tree, bool bIncludeInherited,
        const FString& ComponentTypeFilter);

    /** One component as JSON (without children) */
    static TSharedPtr<FJsonObject> ComponentToJson(const FN2CComponentTree& Tree, int32 Index);

    /**
     * Resolves a component class from an object path (/Script/Engine.StaticMeshComponent, a Blueprint's generated
     * class or the Blueprint's own path, to which _C is added) or a short name (StaticMeshComponent,
     * UStaticMeshComponent, BP_Health), native classes first, and validates it.
     */
    static UClass* ResolveComponentClass(const FString& ClassPath, FString& OutErrorMsg);

    /** The class is an ActorComponent that can be added to a Blueprint */
    static bool ValidateComponentClass(UClass* ComponentClass, FString& OutErrorMsg);

    /** The name is free for a new component variable of this Blueprint (NAME_EXISTS or INVALID_NAME otherwise) */
    static bool ValidateNewComponentName(UBlueprint* Blueprint, const FString& Name, FString& OutErrorMsg);

    /**
     * Gathers the Blueprint's subobject handles from USubobjectDataSubsystem (the actor first). Can create
     * inherited-component override templates, as opening the Blueprint editor does, so call it inside the edit's
     * transaction.
     */
    static bool GatherSubobjects(UBlueprint* Blueprint, TArray<FSubobjectDataHandle>& OutHandles,
        FSubobjectDataHandle& OutActorHandle, FString& OutErrorMsg);

    /** The gathered component handle with this variable name, or an invalid handle */
    static FSubobjectDataHandle FindSubobjectHandle(const TArray<FSubobjectDataHandle>& Handles, FName VariableName);

    /** The SCS node of the Blueprint (its own SCS) whose template is Template, or null */
    static USCS_Node* FindOwnSCSNodeForTemplate(UBlueprint* Blueprint, const UActorComponent* Template);

    /**
     * Applies a transform to a scene component template (Modify first, so an open transaction records it)
     * @param TransformJson JSON object containing location, rotation, and scale; each part optional
     */
    static bool ApplyTransformToComponent(USceneComponent* SceneComponent,
        const TSharedPtr<FJsonObject>& TransformJson, FString& OutErrorMsg);

    /**
     * Reads a componentTypeFilter argument into "all", "scene", "actor" or "primitive" (any case; empty is "all").
     * Any other value: false, with INVALID_ARGUMENT in OutErrorMsg.
     */
    static bool ParseComponentTypeFilter(const FString& InFilter, FString& OutFilter, FString& OutErrorMsg);

    /**
     * Checks if a component class passes a parsed type filter ("all", "scene", "actor", "primitive"). A missing
     * class passes "all" only; an unknown filter passes nothing.
     */
    static bool PassesComponentTypeFilter(UClass* ComponentClass, const FString& TypeFilter);

private:
    static void AppendHierarchyRecursive(const FN2CComponentTree& Tree, int32 Index, bool bIncludeInherited,
        const FString& ComponentTypeFilter, TArray<TSharedPtr<FJsonValue>>& OutJsonArray, int32& InOutCount);

    static TSharedPtr<FJsonObject> GetComponentTransformJson(const USceneComponent* Component);
};
