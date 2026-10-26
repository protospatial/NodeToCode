// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeleteComponentInActorTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpComponentUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Variable.h"
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraph/EdGraph.h"
#include "ScopedTransaction.h"
#include "SubobjectData.h"
#include "SubobjectDataSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpDeleteComponentInActorTool)

FMcpToolDefinition FN2CMcpDeleteComponentInActorTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("delete-component-in-actor"),
        TEXT("Deletes a component this Actor Blueprint adds, as the Components panel does (one undoable step): its Get and Set nodes are removed, events bound to it are reported, and its children are deleted too or moved up to its parent"),
        TEXT("Blueprint Components")
    );

    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // componentName property
    TSharedPtr<FJsonObject> ComponentNameProp = MakeShareable(new FJsonObject);
    ComponentNameProp->SetStringField(TEXT("type"), TEXT("string"));
    ComponentNameProp->SetStringField(TEXT("description"), TEXT("Name of the component to delete"));
    Properties->SetObjectField(TEXT("componentName"), ComponentNameProp);

    // deleteChildren property
    TSharedPtr<FJsonObject> DeleteChildrenProp = MakeShareable(new FJsonObject);
    DeleteChildrenProp->SetStringField(TEXT("type"), TEXT("boolean"));
    DeleteChildrenProp->SetBoolField(TEXT("default"), false);
    DeleteChildrenProp->SetStringField(TEXT("description"), TEXT("If true, deletes all child components too. If false, the children are kept and move up to the deleted component's parent (deleting the root makes its first child the new root)"));
    Properties->SetObjectField(TEXT("deleteChildren"), DeleteChildrenProp);

    // blueprintPath property
    TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
    BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
    BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Optional asset path of the Blueprint. If not provided, uses the currently focused Blueprint"));
    Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("componentName"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    return Definition;
}

namespace N2CDeleteComponentPrivate
{
    /** A graph node the delete is about, found before the delete so the result can say what happened to it */
    struct FNodeRecord
    {
        TWeakObjectPtr<UEdGraph> Graph;
        FGuid NodeGuid;
        FString Title;
        FString Component;
    };

    TSharedPtr<FJsonObject> NodeRecordToJson(const FNodeRecord& Record)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("nodeGuid"), Record.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Json->SetStringField(TEXT("title"), Record.Title);
        Json->SetStringField(TEXT("graph"), Record.Graph.IsValid() ? Record.Graph->GetName() : FString());
        Json->SetStringField(TEXT("component"), Record.Component);
        return Json;
    }

    bool IsStillInGraph(const FNodeRecord& Record)
    {
        const UEdGraph* Graph = Record.Graph.Get();
        if (!Graph)
        {
            return false;
        }
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == Record.NodeGuid)
            {
                return true;
            }
        }
        return false;
    }
}

FMcpToolCallResult FN2CMcpDeleteComponentInActorTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        using namespace N2CDeleteComponentPrivate;

        FString ComponentName;
        if (!Arguments->TryGetStringField(TEXT("componentName"), ComponentName))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: componentName"));
        }
        ComponentName.TrimStartAndEndInline();

        bool bDeleteChildren = false;
        Arguments->TryGetBoolField(TEXT("deleteChildren"), bDeleteChildren);

        FString BlueprintPath;
        Arguments->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);

        FString ErrorMsg;
        UBlueprint* Blueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ErrorMsg);
        if (!Blueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // ---- Find and check the component before changing anything ----

        FN2CComponentTree Tree;
        if (!FN2CMcpComponentUtils::BuildComponentTree(Blueprint, Tree, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        const int32 Index = Tree.Find(ComponentName);
        if (Index == INDEX_NONE)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("COMPONENT_NOT_FOUND|Component '%s' not found in Blueprint"), *ComponentName));
        }
        const FN2CComponentEntry& Entry = Tree.Entries[Index];
        if (Entry.bNative)
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                TEXT("NATIVE_COMPONENT|'%s' is created by the C++ class %s and cannot be deleted from a Blueprint"),
                *Entry.Name.ToString(), *GetNameSafe(FBlueprintEditorUtils::FindFirstNativeClass(Blueprint->ParentClass))));
        }
        if (Entry.bInherited || !Entry.Node)
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                TEXT("INHERITED_COMPONENT|Cannot delete components inherited from parent Blueprint ('%s' is added by %s)"),
                *Entry.Name.ToString(), Entry.OwnerBlueprint ? *Entry.OwnerBlueprint->GetName() : TEXT("a parent Blueprint")));
        }
        if (Entry.bDefaultSceneRoot)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("DEFAULT_SCENE_ROOT|DefaultSceneRoot is the placeholder root the engine ")
                TEXT("keeps while no other scene component is the root; it cannot be deleted (the engine would add it back). ")
                TEXT("Delete or move its children instead"));
        }

        // The components that go: this one, and with deleteChildren everything under it
        TArray<int32> Descendants;
        Tree.GetDescendants(Index, Descendants);
        TArray<int32> ToDelete;
        ToDelete.Add(Index);
        if (bDeleteChildren)
        {
            ToDelete.Append(Descendants);
        }

        // Their variable nodes (the engine removes them) and bound events (the engine leaves them, now invalid)
        TArray<FNodeRecord> VariableNodes;
        TArray<FNodeRecord> BoundEvents;
        TArray<UK2Node_ComponentBoundEvent*> BoundEventNodes;
        TArray<UK2Node_Variable*> Nodes;
        FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Variable>(Blueprint, Nodes);
        for (const int32 DeleteIndex : ToDelete)
        {
            const FName VariableName = Tree.Entries[DeleteIndex].Name;
            for (const UK2Node_Variable* Node : Nodes)
            {
                if (Node && Node->GetVarName() == VariableName && Node->VariableReference.IsSelfContext())
                {
                    VariableNodes.Add({ Node->GetGraph(), Node->NodeGuid, Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), VariableName.ToString() });
                }
            }
            TArray<UK2Node_ComponentBoundEvent*> Events;
            FKismetEditorUtilities::FindAllBoundEventsForComponent(Blueprint, VariableName, Events);
            for (UK2Node_ComponentBoundEvent* Event : Events)
            {
                BoundEventNodes.Add(Event);
                BoundEvents.Add({ Event->GetGraph(), Event->NodeGuid, Event->GetNodeTitle(ENodeTitleType::ListView).ToString(), VariableName.ToString() });
            }
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("deletedComponentName"), Entry.Name.ToString());
        Result->SetStringField(TEXT("deletedComponentClass"), Entry.Class ? Entry.Class->GetName() : TEXT("Unknown"));
        Result->SetStringField(TEXT("deletedComponentGuid"), Entry.Node->VariableGuid.ToString());
        const TArray<int32> DirectChildren = Entry.Children;
        TArray<FName> ChildNames;
        for (const int32 Child : (bDeleteChildren ? Descendants : DirectChildren))
        {
            ChildNames.Add(Tree.Entries[Child].Name);
        }

        // ---- Delete through the engine's subobject subsystem, as the Components panel does, in one transaction ----
        {
            FScopedTransaction Transaction(FText::Format(
                NSLOCTEXT("NodeToCode", "DeleteComponentTransaction", "Delete Component '{0}'"), FText::FromName(Entry.Name)));

            TArray<FSubobjectDataHandle> Handles;
            FSubobjectDataHandle ActorHandle;
            if (!FN2CMcpComponentUtils::GatherSubobjects(Blueprint, Handles, ActorHandle, ErrorMsg))
            {
                Transaction.Cancel();
                return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
            }
            // Deepest first, so no component is promoted only to be deleted after it
            TArray<FSubobjectDataHandle> HandlesToDelete;
            for (int32 Position = ToDelete.Num() - 1; Position >= 0; --Position)
            {
                const FName VariableName = Tree.Entries[ToDelete[Position]].Name;
                const FSubobjectDataHandle Handle = FN2CMcpComponentUtils::FindSubobjectHandle(Handles, VariableName);
                const FSubobjectData* Data = Handle.GetData();
                if (!Data || !Data->CanDelete())
                {
                    Transaction.Cancel();
                    return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                        TEXT("CANNOT_DELETE|The engine does not allow deleting '%s'"), *VariableName.ToString()));
                }
                HandlesToDelete.Add(Handle);
            }

            // For bound events the engine's delete also posts a message log and opens the Blueprint editor on the
            // first one (FKismetEditorUtilities::BringKismetToFocusAttentionOnObject), which a tool call must not do
            // (and a headless 5.6 or 5.7 editor cannot close again). Hide them from it for the call: the events stay
            // (as the engine leaves them, never firing) and the result lists them.
            TArray<FName> BoundComponentNames;
            for (UK2Node_ComponentBoundEvent* Event : BoundEventNodes)
            {
                Event->Modify();
                BoundComponentNames.Add(Event->ComponentPropertyName);
                Event->ComponentPropertyName = NAME_None;
            }
            const int32 Deleted = USubobjectDataSubsystem::Get()->DeleteSubobjects(ActorHandle, HandlesToDelete, Blueprint);
            for (int32 EventIndex = 0; EventIndex < BoundEventNodes.Num(); ++EventIndex)
            {
                BoundEventNodes[EventIndex]->ComponentPropertyName = BoundComponentNames[EventIndex];
            }
            if (Deleted <= 0)
            {
                Transaction.Cancel();
                return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                    TEXT("DELETE_FAILED|The engine deleted nothing for '%s'"), *Entry.Name.ToString()));
            }
            FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
        }

        // Compile once, after the transaction has closed (card 0151)
        FN2CCompileDiagnostics Diagnostics;
        FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);

        // What happened to the children, read back after the compile
        FN2CComponentTree After;
        FN2CMcpComponentUtils::BuildComponentTree(Blueprint, After, ErrorMsg);
        TArray<TSharedPtr<FJsonValue>> AffectedChildren;
        for (const FName& ChildName : ChildNames)
        {
            TSharedPtr<FJsonObject> ChildInfo = MakeShared<FJsonObject>();
            ChildInfo->SetStringField(TEXT("name"), ChildName.ToString());
            const int32 ChildIndex = After.Find(ChildName.ToString());
            if (bDeleteChildren || ChildIndex == INDEX_NONE)
            {
                ChildInfo->SetStringField(TEXT("action"), TEXT("deleted"));
            }
            else
            {
                const int32 NewParent = After.Entries[ChildIndex].Parent;
                ChildInfo->SetStringField(TEXT("action"), TEXT("reparented"));
                if (After.Entries.IsValidIndex(NewParent))
                {
                    ChildInfo->SetStringField(TEXT("newParent"), After.Entries[NewParent].Name.ToString());
                }
                else
                {
                    ChildInfo->SetField(TEXT("newParent"), MakeShared<FJsonValueNull>());
                }
                ChildInfo->SetBoolField(TEXT("isRootComponent"), ChildIndex == After.Root);
            }
            AffectedChildren.Add(MakeShared<FJsonValueObject>(ChildInfo));
        }
        Result->SetArrayField(TEXT("affectedChildren"), AffectedChildren);
        Result->SetBoolField(TEXT("childrenDeleted"), bDeleteChildren);

        TArray<TSharedPtr<FJsonValue>> RemovedNodes;
        for (const FNodeRecord& Record : VariableNodes)
        {
            if (!IsStillInGraph(Record))
            {
                RemovedNodes.Add(MakeShared<FJsonValueObject>(NodeRecordToJson(Record)));
            }
        }
        Result->SetArrayField(TEXT("removedVariableNodes"), RemovedNodes);
        TArray<TSharedPtr<FJsonValue>> EventsJson;
        for (const FNodeRecord& Record : BoundEvents)
        {
            EventsJson.Add(MakeShared<FJsonValueObject>(NodeRecordToJson(Record)));
        }
        Result->SetArrayField(TEXT("boundEvents"), EventsJson);

        TSharedPtr<FJsonObject> CompilationStatus = MakeShared<FJsonObject>();
        CompilationStatus->SetBoolField(TEXT("success"), Diagnostics.bSuccess);
        CompilationStatus->SetNumberField(TEXT("errorCount"), Diagnostics.ErrorCount);
        CompilationStatus->SetNumberField(TEXT("warningCount"), Diagnostics.WarningCount);
        CompilationStatus->SetNumberField(TEXT("compilationTime"), Diagnostics.CompileSeconds);
        Result->SetObjectField(TEXT("compilationStatus"), CompilationStatus);

        TArray<TSharedPtr<FJsonValue>> NextSteps;
        NextSteps.Add(MakeShared<FJsonValueString>(TEXT("Use 'list-components-in-actor' to see the updated component hierarchy")));
        if (BoundEvents.Num() > 0)
        {
            NextSteps.Add(MakeShared<FJsonValueString>(TEXT("The events in boundEvents were bound to the deleted component and now never fire: delete them with delete-blueprint-node, or undo")));
        }
        if (!bDeleteChildren && ChildNames.Num() > 0)
        {
            NextSteps.Add(MakeShared<FJsonValueString>(TEXT("Reparented children keep their relative transforms, now relative to their new parent")));
        }
        NextSteps.Add(MakeShared<FJsonValueString>(TEXT("Edit > Undo (Ctrl+Z) in the editor restores the component, its children and its variable nodes")));
        Result->SetArrayField(TEXT("nextSteps"), NextSteps);

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        FN2CLogger::Get().Log(FString::Printf(TEXT("Deleted component '%s' from Blueprint '%s'%s"), *Entry.Name.ToString(),
            *Blueprint->GetName(), bDeleteChildren ? TEXT(" (including children)") : TEXT("")), EN2CLogSeverity::Info);

        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}
