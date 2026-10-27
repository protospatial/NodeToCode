// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpComponentUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "SubobjectData.h"
#include "SubobjectDataSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// ---- FN2CComponentTree ----

int32 FN2CComponentTree::Find(const FString& Name) const
{
    if (Name.IsEmpty())
    {
        return INDEX_NONE;
    }
    const FName Wanted(*Name);
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        if (Entries[Index].Name == Wanted)
        {
            return Index;
        }
    }
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        if (Entries[Index].ObjectName == Wanted)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

void FN2CComponentTree::GetDescendants(int32 Index, TArray<int32>& OutDescendants) const
{
    if (!Entries.IsValidIndex(Index))
    {
        return;
    }
    for (const int32 Child : Entries[Index].Children)
    {
        OutDescendants.Add(Child);
        GetDescendants(Child, OutDescendants);
    }
}

FString FN2CComponentTree::SceneComponentNames() const
{
    TArray<FString> Names;
    for (const FN2CComponentEntry& Entry : Entries)
    {
        if (Entry.bScene)
        {
            Names.Add(Entry.Name.ToString());
        }
    }
    return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

// ---- Reading ----

bool FN2CMcpComponentUtils::CheckComponentBlueprint(UBlueprint* Blueprint, FString& OutErrorMsg)
{
    if (!Blueprint)
    {
        OutErrorMsg = TEXT("NO_BLUEPRINT|Blueprint is null");
        return false;
    }
    if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutErrorMsg = TEXT("NOT_ACTOR_BLUEPRINT|Blueprint must derive from Actor to have components");
        return false;
    }
    // Level scripts, macro and function libraries, interfaces and const Blueprints have no construction script; the
    // engine never gives them an SCS and neither do these tools (card 0167)
    if (FBlueprintEditorUtils::IsLevelScriptBlueprint(Blueprint) || !FBlueprintEditorUtils::SupportsConstructionScript(Blueprint))
    {
        OutErrorMsg = FString::Printf(TEXT("NO_CONSTRUCTION_SCRIPT|Blueprint '%s' cannot have components: it is a level script, ")
            TEXT("macro library, function library, interface or const Blueprint"), *Blueprint->GetName());
        return false;
    }
    return true;
}

namespace N2CComponentTreePrivate
{
    int32 AddEntry(FN2CComponentTree& Tree, FN2CComponentEntry&& Entry)
    {
        const int32 Parent = Entry.Parent;
        const int32 Index = Tree.Entries.Add(MoveTemp(Entry));
        if (Tree.Entries.IsValidIndex(Parent))
        {
            Tree.Entries[Parent].Children.Add(Index);
        }
        else
        {
            Tree.TopLevel.Add(Index);
        }
        return Index;
    }

    void AddSCSNodeRecursive(FN2CComponentTree& Tree, USCS_Node* Node, int32 Parent, UBlueprint* OwnerBlueprint,
        UBlueprint* ForBlueprint, bool bInherited)
    {
        if (!Node)
        {
            return;
        }
        FN2CComponentEntry Entry;
        Entry.Name = Node->GetVariableName();
        Entry.Class = Node->ComponentClass;
        Entry.Node = Node;
        Entry.OwnerBlueprint = OwnerBlueprint;
        Entry.bInherited = bInherited;
        Entry.Template = Node->ComponentTemplate;
        if (bInherited && ForBlueprint)
        {
            // This Blueprint's override of an inherited component (its changed defaults), read only: never created here
            if (const UInheritableComponentHandler* Handler = ForBlueprint->GetInheritableComponentHandler(false))
            {
                if (const UActorComponent* Override = Handler->GetOverridenComponentTemplate(FComponentKey(Node)))
                {
                    Entry.Template = Override;
                }
            }
        }
        Entry.ObjectName = Node->ComponentTemplate ? Node->ComponentTemplate->GetFName() : Entry.Name;
        Entry.bScene = Entry.Class && Entry.Class->IsChildOf(USceneComponent::StaticClass());
        const USimpleConstructionScript* SCS = Node->GetSCS();
        Entry.bDefaultSceneRoot = SCS && SCS->GetDefaultSceneRootNode() == Node;
        Entry.Parent = Parent;
        Entry.SocketName = Node->AttachToName;
        const int32 Index = AddEntry(Tree, MoveTemp(Entry));
        for (USCS_Node* Child : Node->GetChildNodes())
        {
            AddSCSNodeRecursive(Tree, Child, Index, OwnerBlueprint, ForBlueprint, bInherited);
        }
    }
}

bool FN2CMcpComponentUtils::BuildComponentTree(UBlueprint* Blueprint, FN2CComponentTree& OutTree, FString& OutErrorMsg)
{
    using namespace N2CComponentTreePrivate;
    OutTree = FN2CComponentTree();
    if (!CheckComponentBlueprint(Blueprint, OutErrorMsg))
    {
        return false;
    }

    // Native components: the C++ classes' default subobjects, on the class default object (SCS components never are)
    const AActor* CDO = Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
    if (CDO)
    {
        TInlineComponentArray<UActorComponent*> Components;
        CDO->GetComponents(Components);
        const USceneComponent* NativeRoot = CDO->GetRootComponent();
        TMap<const UActorComponent*, int32> NativeIndex;
        for (UActorComponent* Component : Components)
        {
            if (!Component)
            {
                continue;
            }
            FN2CComponentEntry Entry;
            const FName VariableName = FComponentEditorUtils::FindVariableNameGivenComponentInstance(Component);
            Entry.ObjectName = Component->GetFName();
            Entry.Name = VariableName.IsNone() ? Entry.ObjectName : VariableName;
            Entry.Class = Component->GetClass();
            Entry.Template = Component;
            Entry.bNative = true;
            Entry.bInherited = true;
            Entry.bScene = Component->IsA<USceneComponent>();
            NativeIndex.Add(Component, OutTree.Entries.Add(MoveTemp(Entry)));
        }
        // Attach them as the class default object has them; a scene component without a parent sits under the root
        for (const TPair<const UActorComponent*, int32>& Pair : NativeIndex)
        {
            FN2CComponentEntry& Entry = OutTree.Entries[Pair.Value];
            if (const USceneComponent* Scene = Cast<USceneComponent>(Pair.Key))
            {
                if (Scene == NativeRoot)
                {
                    continue;
                }
                const int32* ParentIndex = Scene->GetAttachParent() ? NativeIndex.Find(Scene->GetAttachParent()) : nullptr;
                if (!ParentIndex && NativeRoot)
                {
                    ParentIndex = NativeIndex.Find(NativeRoot);
                }
                if (ParentIndex)
                {
                    Entry.Parent = *ParentIndex;
                    Entry.SocketName = Scene->GetAttachSocketName();
                }
            }
        }
        for (const TPair<const UActorComponent*, int32>& Pair : NativeIndex)
        {
            const int32 Parent = OutTree.Entries[Pair.Value].Parent;
            if (OutTree.Entries.IsValidIndex(Parent))
            {
                OutTree.Entries[Parent].Children.Add(Pair.Value);
            }
            else
            {
                OutTree.TopLevel.Add(Pair.Value);
            }
        }
        if (const int32* RootIndex = NativeRoot ? NativeIndex.Find(NativeRoot) : nullptr)
        {
            OutTree.Root = *RootIndex;
        }
    }

    // SCS components, the oldest parent Blueprint first, as USimpleConstructionScript::ExecuteScriptOnActor builds
    // them: a root node names its parent (a native or inherited component) or goes under the actor's root
    TArray<UBlueprint*> Hierarchy;
    if (Blueprint->GeneratedClass)
    {
        UBlueprint::GetBlueprintHierarchyFromClass(Blueprint->GeneratedClass, Hierarchy);
    }
    if (Hierarchy.Num() == 0 || Hierarchy[0] != Blueprint)
    {
        Hierarchy.Insert(Blueprint, 0);
    }
    for (int32 Level = Hierarchy.Num() - 1; Level >= 0; --Level)
    {
        UBlueprint* LevelBlueprint = Hierarchy[Level];
        USimpleConstructionScript* SCS = LevelBlueprint ? LevelBlueprint->SimpleConstructionScript.Get() : nullptr;
        if (!SCS)
        {
            continue;
        }
        const int32 RootAtStart = OutTree.Root;
        for (USCS_Node* RootNode : SCS->GetRootNodes())
        {
            if (!RootNode)
            {
                continue;
            }
            // The DefaultSceneRoot is not built when the actor already has a root
            if (RootNode == SCS->GetDefaultSceneRootNode() && RootAtStart != INDEX_NONE)
            {
                continue;
            }
            const bool bScene = RootNode->ComponentClass && RootNode->ComponentClass->IsChildOf(USceneComponent::StaticClass());
            int32 Parent = INDEX_NONE;
            if (!RootNode->ParentComponentOrVariableName.IsNone())
            {
                // A native parent is named by its object name, an inherited one by its variable name
                for (int32 Index = 0; Index < OutTree.Entries.Num(); ++Index)
                {
                    const FN2CComponentEntry& Candidate = OutTree.Entries[Index];
                    if (RootNode->bIsParentComponentNative
                        ? (Candidate.bNative && Candidate.ObjectName == RootNode->ParentComponentOrVariableName)
                        : (!Candidate.bNative && Candidate.Name == RootNode->ParentComponentOrVariableName))
                    {
                        Parent = Index;
                        break;
                    }
                }
            }
            if (Parent == INDEX_NONE && bScene)
            {
                Parent = RootAtStart;
            }
            const int32 Before = OutTree.Entries.Num();
            AddSCSNodeRecursive(OutTree, RootNode, Parent, LevelBlueprint, Blueprint, Level > 0);
            if (bScene && Parent == INDEX_NONE && OutTree.Root == INDEX_NONE && OutTree.Entries.Num() > Before)
            {
                OutTree.Root = Before;
            }
        }
    }
    return true;
}

TSharedPtr<FJsonObject> FN2CMcpComponentUtils::BuildComponentHierarchy(const FN2CComponentTree& Tree, bool bIncludeInherited,
    const FString& ComponentTypeFilter)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> ComponentsArray;
    int32 Count = 0;
    for (const int32 Index : Tree.TopLevel)
    {
        AppendHierarchyRecursive(Tree, Index, bIncludeInherited, ComponentTypeFilter, ComponentsArray, Count);
    }
    Result->SetArrayField(TEXT("components"), ComponentsArray);
    Result->SetNumberField(TEXT("totalCount"), Count);
    Result->SetStringField(TEXT("rootComponent"), Tree.Entries.IsValidIndex(Tree.Root) ? Tree.Entries[Tree.Root].Name.ToString() : FString());
    return Result;
}

void FN2CMcpComponentUtils::AppendHierarchyRecursive(const FN2CComponentTree& Tree, int32 Index, bool bIncludeInherited,
    const FString& ComponentTypeFilter, TArray<TSharedPtr<FJsonValue>>& OutJsonArray, int32& InOutCount)
{
    if (!Tree.Entries.IsValidIndex(Index))
    {
        return;
    }
    const FN2CComponentEntry& Entry = Tree.Entries[Index];
    const bool bShow = (bIncludeInherited || !Entry.bInherited) && PassesComponentTypeFilter(Entry.Class, ComponentTypeFilter);
    if (!bShow)
    {
        // Left out: its listed descendants take its place
        for (const int32 Child : Entry.Children)
        {
            AppendHierarchyRecursive(Tree, Child, bIncludeInherited, ComponentTypeFilter, OutJsonArray, InOutCount);
        }
        return;
    }
    TSharedPtr<FJsonObject> Json = ComponentToJson(Tree, Index);
    TArray<TSharedPtr<FJsonValue>> ChildrenArray;
    ++InOutCount;
    for (const int32 Child : Entry.Children)
    {
        AppendHierarchyRecursive(Tree, Child, bIncludeInherited, ComponentTypeFilter, ChildrenArray, InOutCount);
    }
    Json->SetArrayField(TEXT("children"), ChildrenArray);
    OutJsonArray.Add(MakeShared<FJsonValueObject>(Json));
}

TSharedPtr<FJsonObject> FN2CMcpComponentUtils::ComponentToJson(const FN2CComponentTree& Tree, int32 Index)
{
    if (!Tree.Entries.IsValidIndex(Index))
    {
        return nullptr;
    }
    const FN2CComponentEntry& Entry = Tree.Entries[Index];
    TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
    NodeJson->SetStringField(TEXT("name"), Entry.Name.ToString());
    if (Entry.Class)
    {
        NodeJson->SetStringField(TEXT("className"), Entry.Class->GetName());
        NodeJson->SetStringField(TEXT("classPath"), Entry.Class->GetPathName());
    }
    NodeJson->SetStringField(TEXT("nodeGuid"), Entry.Node ? Entry.Node->VariableGuid.ToString() : FString());
    NodeJson->SetBoolField(TEXT("isSceneComponent"), Entry.bScene);
    NodeJson->SetBoolField(TEXT("isRootComponent"), Index == Tree.Root);
    if (Tree.Entries.IsValidIndex(Entry.Parent))
    {
        NodeJson->SetStringField(TEXT("parentComponent"), Tree.Entries[Entry.Parent].Name.ToString());
    }
    else
    {
        NodeJson->SetField(TEXT("parentComponent"), MakeShared<FJsonValueNull>());
    }
    NodeJson->SetStringField(TEXT("attachSocketName"), Entry.SocketName.ToString());
    if (const USceneComponent* SceneComp = Cast<USceneComponent>(Entry.Template))
    {
        NodeJson->SetObjectField(TEXT("transform"), GetComponentTransformJson(SceneComp));
    }
    NodeJson->SetBoolField(TEXT("isInherited"), Entry.bInherited);
    NodeJson->SetBoolField(TEXT("isNative"), Entry.bNative);
    if (Entry.bNative && Entry.ObjectName != Entry.Name)
    {
        NodeJson->SetStringField(TEXT("objectName"), Entry.ObjectName.ToString());
    }
    if (Entry.bInherited && !Entry.bNative && Entry.OwnerBlueprint)
    {
        NodeJson->SetStringField(TEXT("inheritedFrom"), Entry.OwnerBlueprint->GetName());
    }
    NodeJson->SetArrayField(TEXT("children"), TArray<TSharedPtr<FJsonValue>>());
    return NodeJson;
}

// ---- Classes and names ----

UClass* FN2CMcpComponentUtils::ResolveComponentClass(const FString& InClassPath, FString& OutErrorMsg)
{
    const FString ClassPath = InClassPath.TrimStartAndEnd();
    if (ClassPath.IsEmpty())
    {
        OutErrorMsg = TEXT("EMPTY_CLASS_PATH|Component class path is empty");
        return nullptr;
    }

    auto FindByPath = [](const FString& Path) -> UClass*
    {
        if (UClass* Found = FindObject<UClass>(nullptr, *Path))
        {
            return Found;
        }
        // Native classes are always loaded; only content paths are worth a load
        if (Path.StartsWith(TEXT("/")) && !Path.StartsWith(TEXT("/Script/")))
        {
            return LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        }
        return nullptr;
    };

    UClass* ComponentClass = nullptr;
    FString ShortName = ClassPath;
    if (ClassPath.Contains(TEXT("/")) || ClassPath.Contains(TEXT(".")))
    {
        ComponentClass = FindByPath(ClassPath);
        if (!ComponentClass && !ClassPath.EndsWith(TEXT("_C")))
        {
            // A Blueprint's own path: /Game/BP_Health.BP_Health, or the package path /Game/BP_Health
            FString WithClass = ClassPath;
            if (!ClassPath.Contains(TEXT(".")))
            {
                WithClass = ClassPath + TEXT(".") + FPackageName::GetShortName(ClassPath);
            }
            ComponentClass = FindByPath(WithClass + TEXT("_C"));
        }
        int32 Dot = INDEX_NONE;
        ShortName = ClassPath.FindLastChar(TEXT('.'), Dot) ? ClassPath.RightChop(Dot + 1) : FPackageName::GetShortName(ClassPath);
    }

    if (!ComponentClass && !ShortName.IsEmpty())
    {
        // A short name, or the class part of a path that did not resolve (a module typo): native classes first,
        // then the name without its U prefix and a Blueprint's generated class
        TArray<FString> Candidates;
        Candidates.Add(ShortName);
        if (ShortName.Len() > 1 && ShortName[0] == TEXT('U') && FChar::IsUpper(ShortName[1]))
        {
            Candidates.Add(ShortName.RightChop(1));
        }
        if (!ShortName.EndsWith(TEXT("_C")))
        {
            Candidates.Add(ShortName + TEXT("_C"));
        }
        for (const FString& Candidate : Candidates)
        {
            ComponentClass = FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst);
            if (ComponentClass)
            {
                break;
            }
        }
    }

    if (!ComponentClass)
    {
        OutErrorMsg = FString::Printf(TEXT("CLASS_NOT_FOUND|Component class '%s' not found. Use a class path such as ")
            TEXT("'/Script/Engine.StaticMeshComponent', a loaded class name such as 'StaticMeshComponent', or a ")
            TEXT("component Blueprint's path"), *ClassPath);
        return nullptr;
    }
    if (!ValidateComponentClass(ComponentClass, OutErrorMsg))
    {
        return nullptr;
    }
    return ComponentClass;
}

bool FN2CMcpComponentUtils::ValidateComponentClass(UClass* ComponentClass, FString& OutErrorMsg)
{
    if (!ComponentClass)
    {
        OutErrorMsg = TEXT("INVALID_CLASS|Component class is null");
        return false;
    }
    if (!ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        OutErrorMsg = FString::Printf(TEXT("NOT_COMPONENT|Class '%s' is not an ActorComponent"), *ComponentClass->GetName());
        return false;
    }
    if (ComponentClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || FKismetEditorUtilities::IsClassABlueprintSkeleton(ComponentClass)
        || (ComponentClass->ClassWithin && ComponentClass->ClassWithin != UObject::StaticClass()))
    {
        OutErrorMsg = FString::Printf(TEXT("CANNOT_INSTANTIATE|Class '%s' cannot be instantiated (abstract, deprecated, ")
            TEXT("has newer version, a skeleton class, or limited to an outer by Within)"), *ComponentClass->GetName());
        return false;
    }
    return true;
}

bool FN2CMcpComponentUtils::ValidateNewComponentName(UBlueprint* Blueprint, const FString& Name, FString& OutErrorMsg)
{
    if (!FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
    {
        OutErrorMsg = FString::Printf(TEXT("INVALID_NAME|'%s' is not a valid component name (no spaces or any of %s)"),
            *Name, INVALID_OBJECTNAME_CHARACTERS);
        return false;
    }
    FKismetNameValidator Validator(Blueprint);
    const EValidatorResult Result = Validator.IsValid(Name);
    if (Result == EValidatorResult::Ok)
    {
        return true;
    }
    const bool bTaken = Result == EValidatorResult::AlreadyInUse || Result == EValidatorResult::ExistingName
        || Result == EValidatorResult::LocallyInUse;
    OutErrorMsg = FString::Printf(TEXT("%s|%s"), bTaken ? TEXT("NAME_EXISTS") : TEXT("INVALID_NAME"),
        *INameValidatorInterface::GetErrorString(Name, Result));
    return false;
}

// ---- Subobject data ----

bool FN2CMcpComponentUtils::GatherSubobjects(UBlueprint* Blueprint, TArray<FSubobjectDataHandle>& OutHandles,
    FSubobjectDataHandle& OutActorHandle, FString& OutErrorMsg)
{
    OutHandles.Reset();
    OutActorHandle = FSubobjectDataHandle::InvalidHandle;
    USubobjectDataSubsystem* Subsystem = USubobjectDataSubsystem::Get();
    if (!Subsystem)
    {
        OutErrorMsg = TEXT("NO_SUBOBJECT_SUBSYSTEM|The engine's SubobjectDataSubsystem is not available");
        return false;
    }
    Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, OutHandles);
    for (const FSubobjectDataHandle& Handle : OutHandles)
    {
        const FSubobjectData* Data = Handle.GetData();
        if (Data && Data->IsActor())
        {
            OutActorHandle = Handle;
            break;
        }
    }
    if (!OutActorHandle.IsValid())
    {
        OutErrorMsg = FString::Printf(TEXT("NO_SUBOBJECT_DATA|Could not read the components of '%s' (compile it and retry)"),
            Blueprint ? *Blueprint->GetName() : TEXT("<null>"));
        return false;
    }
    return true;
}

FSubobjectDataHandle FN2CMcpComponentUtils::FindSubobjectHandle(const TArray<FSubobjectDataHandle>& Handles, FName VariableName)
{
    for (const FSubobjectDataHandle& Handle : Handles)
    {
        const FSubobjectData* Data = Handle.GetData();
        if (Data && Data->IsComponent() && Data->GetVariableName() == VariableName)
        {
            return Handle;
        }
    }
    return FSubobjectDataHandle::InvalidHandle;
}

USCS_Node* FN2CMcpComponentUtils::FindOwnSCSNodeForTemplate(UBlueprint* Blueprint, const UActorComponent* Template)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript || !Template)
    {
        return nullptr;
    }
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->ComponentTemplate == Template)
        {
            return Node;
        }
    }
    return nullptr;
}

// ---- Transforms and filters ----

bool FN2CMcpComponentUtils::ApplyTransformToComponent(USceneComponent* SceneComponent,
    const TSharedPtr<FJsonObject>& TransformJson, FString& OutErrorMsg)
{
    if (!SceneComponent)
    {
        OutErrorMsg = TEXT("NO_COMPONENT|Scene component is null");
        return false;
    }
    if (!TransformJson.IsValid())
    {
        return true;
    }
    SceneComponent->Modify();

    const TSharedPtr<FJsonObject>* LocationObj;
    if (TransformJson->TryGetObjectField(TEXT("location"), LocationObj))
    {
        double X = 0, Y = 0, Z = 0;
        (*LocationObj)->TryGetNumberField(TEXT("x"), X);
        (*LocationObj)->TryGetNumberField(TEXT("y"), Y);
        (*LocationObj)->TryGetNumberField(TEXT("z"), Z);
        SceneComponent->SetRelativeLocation(FVector(X, Y, Z));
    }

    const TSharedPtr<FJsonObject>* RotationObj;
    if (TransformJson->TryGetObjectField(TEXT("rotation"), RotationObj))
    {
        double Pitch = 0, Yaw = 0, Roll = 0;
        (*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch);
        (*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw);
        (*RotationObj)->TryGetNumberField(TEXT("roll"), Roll);
        SceneComponent->SetRelativeRotation(FRotator(Pitch, Yaw, Roll));
    }

    const TSharedPtr<FJsonObject>* ScaleObj;
    if (TransformJson->TryGetObjectField(TEXT("scale"), ScaleObj))
    {
        double X = 1, Y = 1, Z = 1;
        (*ScaleObj)->TryGetNumberField(TEXT("x"), X);
        (*ScaleObj)->TryGetNumberField(TEXT("y"), Y);
        (*ScaleObj)->TryGetNumberField(TEXT("z"), Z);
        SceneComponent->SetRelativeScale3D(FVector(X, Y, Z));
    }
    return true;
}

bool FN2CMcpComponentUtils::ParseComponentTypeFilter(const FString& InFilter, FString& OutFilter, FString& OutErrorMsg)
{
    // FString comparison ignores case: "Primitive" is "primitive"; an empty value is the default
    static const TCHAR* const Filters[] = { TEXT("all"), TEXT("scene"), TEXT("actor"), TEXT("primitive") };
    const FString Trimmed = InFilter.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        OutFilter = TEXT("all");
        return true;
    }
    for (const TCHAR* Filter : Filters)
    {
        if (Trimmed == Filter)
        {
            OutFilter = Filter;
            return true;
        }
    }
    OutErrorMsg = FString::Printf(TEXT("INVALID_ARGUMENT|componentTypeFilter '%s' is not one of: all, scene, actor, primitive"), *InFilter);
    return false;
}

bool FN2CMcpComponentUtils::PassesComponentTypeFilter(UClass* ComponentClass, const FString& TypeFilter)
{
    if (TypeFilter == TEXT("all"))
    {
        return true;
    }
    // A component whose class is missing (a deleted component Blueprint) is of no known type
    if (!ComponentClass)
    {
        return false;
    }
    if (TypeFilter == TEXT("scene"))
    {
        return ComponentClass->IsChildOf(USceneComponent::StaticClass());
    }
    if (TypeFilter == TEXT("actor"))
    {
        return ComponentClass->IsChildOf(UActorComponent::StaticClass())
            && !ComponentClass->IsChildOf(USceneComponent::StaticClass());
    }
    if (TypeFilter == TEXT("primitive"))
    {
        return ComponentClass->IsChildOf(UPrimitiveComponent::StaticClass());
    }
    return false;
}

TSharedPtr<FJsonObject> FN2CMcpComponentUtils::GetComponentTransformJson(const USceneComponent* Component)
{
    if (!Component)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> TransformJson = MakeShared<FJsonObject>();

    const FVector Location = Component->GetRelativeLocation();
    TSharedPtr<FJsonObject> LocationJson = MakeShared<FJsonObject>();
    LocationJson->SetNumberField(TEXT("x"), Location.X);
    LocationJson->SetNumberField(TEXT("y"), Location.Y);
    LocationJson->SetNumberField(TEXT("z"), Location.Z);
    TransformJson->SetObjectField(TEXT("location"), LocationJson);

    const FRotator Rotation = Component->GetRelativeRotation();
    TSharedPtr<FJsonObject> RotationJson = MakeShared<FJsonObject>();
    RotationJson->SetNumberField(TEXT("pitch"), Rotation.Pitch);
    RotationJson->SetNumberField(TEXT("yaw"), Rotation.Yaw);
    RotationJson->SetNumberField(TEXT("roll"), Rotation.Roll);
    TransformJson->SetObjectField(TEXT("rotation"), RotationJson);

    const FVector Scale = Component->GetRelativeScale3D();
    TSharedPtr<FJsonObject> ScaleJson = MakeShared<FJsonObject>();
    ScaleJson->SetNumberField(TEXT("x"), Scale.X);
    ScaleJson->SetNumberField(TEXT("y"), Scale.Y);
    ScaleJson->SetNumberField(TEXT("z"), Scale.Z);
    TransformJson->SetObjectField(TEXT("scale"), ScaleJson);

    return TransformJson;
}
