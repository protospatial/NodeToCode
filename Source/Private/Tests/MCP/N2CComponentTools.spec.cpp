// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Utils/N2CMcpComponentUtils.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Character.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_VariableGet.h"
#include "Components/PrimitiveComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

/**
 * The Blueprint component tools (card 0167): add-component-class-to-actor and delete-component-in-actor go through
 * USubobjectDataSubsystem in one transaction each, a parent (own, inherited or native), socket and class are checked
 * before anything changes, delete removes Get nodes and promotes children, DefaultSceneRoot cannot be deleted, and
 * nothing creates an SCS for a Blueprint that has no construction script. Review ids: mcp-tools-bp-structure-1, -2,
 * -8, -9, -11, -24, -25, -26, -27, imp-mcp-tools-bp-structure-1.
 */
namespace N2CComponentToolsSpec
{
	/** This Blueprint's own SCS as text: each node, its class, template, parent reference, socket and children */
	void OwnTreeLines(const USCS_Node* Node, int32 Depth, TArray<FString>& Lines)
	{
		if (!Node)
		{
			Lines.Add(FString::ChrN(Depth * 2, TEXT(' ')) + TEXT("<null>"));
			return;
		}
		Lines.Add(FString::Printf(TEXT("%s%s : %s template=%s parent=%s native=%d socket=%s guid=%s"), *FString::ChrN(Depth * 2, TEXT(' ')),
			*Node->GetVariableName().ToString(), *GetNameSafe(Node->ComponentClass), *GetNameSafe(Node->ComponentTemplate),
			*Node->ParentComponentOrVariableName.ToString(), Node->bIsParentComponentNative ? 1 : 0, *Node->AttachToName.ToString(),
			*Node->VariableGuid.ToString()));
		for (const USCS_Node* Child : Node->GetChildNodes())
		{
			OwnTreeLines(Child, Depth + 1, Lines);
		}
	}

	FString OwnTree(UBlueprint* Blueprint)
	{
		TArray<FString> Lines;
		const USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript.Get() : nullptr;
		if (!SCS)
		{
			return TEXT("<no SCS>");
		}
		for (const USCS_Node* Root : SCS->GetRootNodes())
		{
			OwnTreeLines(Root, 0, Lines);
		}
		Lines.Add(FString::Printf(TEXT("all nodes %d"), SCS->GetAllNodes().Num()));
		return FString::Join(Lines, TEXT("\n"));
	}

	/** The harness snapshot (graphs, variables) and the SCS tree */
	FString State(UBlueprint* Blueprint)
	{
		return N2CTest::Snapshot(Blueprint) + TEXT("\n--- SCS ---\n") + OwnTree(Blueprint);
	}

	/** The name of the component Name is attached to, as the hierarchy reads after a compile ("" at the top level) */
	FString ParentOf(UBlueprint* Blueprint, const FString& Name)
	{
		FN2CComponentTree Tree;
		FString Error;
		if (!FN2CMcpComponentUtils::BuildComponentTree(Blueprint, Tree, Error))
		{
			return TEXT("<error ") + Error + TEXT(">");
		}
		const int32 Index = Tree.Find(Name);
		if (Index == INDEX_NONE)
		{
			return TEXT("<missing>");
		}
		const int32 Parent = Tree.Entries[Index].Parent;
		return Tree.Entries.IsValidIndex(Parent) ? Tree.Entries[Parent].Name.ToString() : FString();
	}

	USCS_Node* OwnNode(UBlueprint* Blueprint, const FString& Name)
	{
		const USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript.Get() : nullptr;
		for (USCS_Node* Node : SCS ? SCS->GetAllNodes() : TArray<USCS_Node*>())
		{
			if (Node && Node->GetVariableName() == FName(*Name))
			{
				return Node;
			}
		}
		return nullptr;
	}

	/** Every component object in a list-components-in-actor tree with this name */
	void FindListed(const TArray<TSharedPtr<FJsonValue>>& Components, const FString& Name, TArray<TSharedPtr<FJsonObject>>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Value : Components)
		{
			const TSharedPtr<FJsonObject> Component = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Component.IsValid())
			{
				continue;
			}
			if (Component->GetStringField(TEXT("name")) == Name)
			{
				Out.Add(Component);
			}
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Component->TryGetArrayField(TEXT("children"), Children))
			{
				FindListed(*Children, Name, Out);
			}
		}
	}

	/** The error code of an error result ("" when it is not an error) */
	FString ErrorCode(const FMcpToolCallResult& Result)
	{
		if (!Result.bIsError)
		{
			return FString();
		}
		FString Code;
		N2CTest::ResultText(Result).Split(TEXT("|"), &Code, nullptr);
		return Code;
	}

	UBlueprint* MakeBlueprint(UPackage* Package, UClass* Parent, const TCHAR* Name, EBlueprintType Type = BPTYPE_Normal)
	{
		return FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(Name), Type, UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass());
	}
}

BEGIN_DEFINE_SPEC(FN2CComponentToolsSpec, "NodeToCode.MCP.Components", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;

	bool Build(const TCHAR* Name)
	{
		Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
		Blueprint = Test->GetBlueprint();
		if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
		{
			return false;
		}
		Test->Settle();
		return true;
	}

	TSharedPtr<FJsonObject> AddArgs(UBlueprint* Target, const FString& Class, const FString& Name, const FString& Parent = FString(),
		const FString& Socket = FString()) const
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Target->GetPathName());
		Args->SetStringField(TEXT("componentClass"), Class);
		if (!Name.IsEmpty())
		{
			Args->SetStringField(TEXT("componentName"), Name);
		}
		if (!Parent.IsEmpty())
		{
			Args->SetStringField(TEXT("parentComponent"), Parent);
		}
		if (!Socket.IsEmpty())
		{
			Args->SetStringField(TEXT("attachSocketName"), Socket);
		}
		return Args;
	}

	FMcpToolCallResult Add(UBlueprint* Target, const FString& Class, const FString& Name, const FString& Parent = FString(),
		const FString& Socket = FString())
	{
		return N2CTest::RunTool(TEXT("add-component-class-to-actor"), AddArgs(Target, Class, Name, Parent, Socket));
	}

	FMcpToolCallResult Delete(UBlueprint* Target, const FString& Name, bool bDeleteChildren)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Target->GetPathName());
		Args->SetStringField(TEXT("componentName"), Name);
		Args->SetBoolField(TEXT("deleteChildren"), bDeleteChildren);
		return N2CTest::RunTool(TEXT("delete-component-in-actor"), Args);
	}

	FMcpToolCallResult List(UBlueprint* Target, bool bIncludeInherited = true)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Target->GetPathName());
		Args->SetBoolField(TEXT("includeInherited"), bIncludeInherited);
		return N2CTest::RunTool(TEXT("list-components-in-actor"), Args);
	}

	/** Adds a component and checks it compiled and became one undo step; the result's JSON, or null */
	TSharedPtr<FJsonObject> ExpectAdded(const FString& What, UBlueprint* Target, const FString& Class, const FString& Name,
		const FString& Parent = FString(), const FString& Socket = FString())
	{
		const int32 QueueBefore = N2CTest::UndoQueueLength();
		const FMcpToolCallResult Result = Add(Target, Class, Name, Parent, Socket);
		if (!N2CTest::ExpectToolCompiled(*this, What, Result))
		{
			AddError(What + TEXT(": ") + N2CTest::ResultText(Result).Left(400));
			return nullptr;
		}
		TestEqual(What + TEXT(": one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
		return N2CTest::ResultJson(Result);
	}

	/** Undo then Redo of the last transaction: Undo gives Before exactly (graphs and SCS), Redo the state it was called in */
	bool ExpectUndoRedoState(const FString& What, UBlueprint* Target, const FString& Before)
	{
		using namespace N2CComponentToolsSpec;
		if (!TestTrue(What + TEXT(": an editor transaction buffer"), GEditor && GEditor->Trans))
		{
			return false;
		}
		const FString After = State(Target);
		if (!TestTrue(What + TEXT(": undo runs"), GEditor->UndoTransaction()))
		{
			return false;
		}
		const FString Undone = N2CTest::FirstSnapshotDifference(Before, State(Target));
		bool bOk = TestTrue(What + TEXT(": undo restores graphs and components exactly") + (Undone.IsEmpty() ? FString() : TEXT(" (") + Undone + TEXT(")")),
			Undone.IsEmpty());
		if (!TestTrue(What + TEXT(": redo runs"), GEditor->RedoTransaction()))
		{
			return false;
		}
		const FString Redone = N2CTest::FirstSnapshotDifference(After, State(Target));
		bOk &= TestTrue(What + TEXT(": redo restores the edit exactly") + (Redone.IsEmpty() ? FString() : TEXT(" (") + Redone + TEXT(")")),
			Redone.IsEmpty());
		return bOk;
	}

	/** Builds DefaultSceneRoot > CameraBoom > FollowCamera through the tool, compiled, package clean */
	bool BuildBoomAndCamera()
	{
		if (!ExpectAdded(TEXT("add CameraBoom"), Blueprint, TEXT("/Script/Engine.SpringArmComponent"), TEXT("CameraBoom")).IsValid()
			|| !ExpectAdded(TEXT("add FollowCamera"), Blueprint, TEXT("/Script/Engine.CameraComponent"), TEXT("FollowCamera"), TEXT("CameraBoom")).IsValid())
		{
			return false;
		}
		Test->Settle();
		return true;
	}
END_DEFINE_SPEC(FN2CComponentToolsSpec)

void FN2CComponentToolsSpec::Define()
{
	using namespace N2CComponentToolsSpec;

	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
	});

	Describe(TEXT("add-component-class-to-actor"), [this]()
	{
		It(TEXT("attaches CameraBoom to the root, FollowCamera to CameraBoom and a third level below that, each kept after compiles and each one undo step that Undo then Redo restore exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsAdd")))
			{
				return;
			}
			FString Before = State(Blueprint);
			TSharedPtr<FJsonObject> Boom = ExpectAdded(TEXT("CameraBoom"), Blueprint, TEXT("/Script/Engine.SpringArmComponent"), TEXT("CameraBoom"));
			if (!Boom.IsValid())
			{
				return;
			}
			TestEqual(TEXT("CameraBoom attachedTo"), Boom->GetStringField(TEXT("attachedTo")), FString(TEXT("DefaultSceneRoot")));
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			ExpectUndoRedoState(TEXT("add CameraBoom"), Blueprint, Before);

			Before = State(Blueprint);
			TSharedPtr<FJsonObject> Camera = ExpectAdded(TEXT("FollowCamera"), Blueprint, TEXT("/Script/Engine.CameraComponent"), TEXT("FollowCamera"),
				TEXT("CameraBoom"), TEXT("SpringEndpoint"));
			if (!Camera.IsValid())
			{
				return;
			}
			TestEqual(TEXT("FollowCamera attachedTo"), Camera->GetStringField(TEXT("attachedTo")), FString(TEXT("CameraBoom")));
			TestEqual(TEXT("FollowCamera socket"), Camera->GetStringField(TEXT("attachSocketName")), FString(TEXT("SpringEndpoint")));
			USCS_Node* BoomNode = OwnNode(Blueprint, TEXT("CameraBoom"));
			USCS_Node* CameraNode = OwnNode(Blueprint, TEXT("FollowCamera"));
			TestTrue(TEXT("FollowCamera is in CameraBoom's child nodes"), BoomNode && CameraNode && BoomNode->GetChildNodes().Contains(CameraNode));
			TestEqual(TEXT("FollowCamera's parent after the compile"), ParentOf(Blueprint, TEXT("FollowCamera")), FString(TEXT("CameraBoom")));
			ExpectUndoRedoState(TEXT("add FollowCamera"), Blueprint, Before);

			// A parent that is not the root (FollowCamera sits under CameraBoom under DefaultSceneRoot)
			TSharedPtr<FJsonObject> Lens = ExpectAdded(TEXT("Lens"), Blueprint, TEXT("/Script/Engine.SceneComponent"), TEXT("Lens"), TEXT("FollowCamera"));
			if (Lens.IsValid())
			{
				TestEqual(TEXT("Lens attachedTo"), Lens->GetStringField(TEXT("attachedTo")), FString(TEXT("FollowCamera")));
			}
			N2CTest::ExpectCompilesClean(*this, TEXT("compile again"), Blueprint);
			TestEqual(TEXT("FollowCamera's parent after another compile"), ParentOf(Blueprint, TEXT("FollowCamera")), FString(TEXT("CameraBoom")));
			TestEqual(TEXT("Lens's parent after another compile"), ParentOf(Blueprint, TEXT("Lens")), FString(TEXT("FollowCamera")));
			TestEqual(TEXT("CameraBoom's parent"), ParentOf(Blueprint, TEXT("CameraBoom")), FString(TEXT("DefaultSceneRoot")));

			// list-components-in-actor shows the same tree, every component once
			const TSharedPtr<FJsonObject> Listed = N2CTest::ResultJson(List(Blueprint));
			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			if (TestTrue(TEXT("list result"), Listed.IsValid() && Listed->TryGetArrayField(TEXT("components"), Components)))
			{
				TestEqual(TEXT("totalCount counts every component"), static_cast<int32>(Listed->GetNumberField(TEXT("totalCount"))), 4);
				TestEqual(TEXT("rootComponent"), Listed->GetStringField(TEXT("rootComponent")), FString(TEXT("DefaultSceneRoot")));
				TArray<TSharedPtr<FJsonObject>> Found;
				FindListed(*Components, TEXT("Lens"), Found);
				if (TestEqual(TEXT("Lens listed once"), Found.Num(), 1))
				{
					TestEqual(TEXT("Lens parentComponent"), Found[0]->GetStringField(TEXT("parentComponent")), FString(TEXT("FollowCamera")));
					TestFalse(TEXT("Lens isRootComponent"), Found[0]->GetBoolField(TEXT("isRootComponent")));
				}
			}
		});

		It(TEXT("refuses a misspelled parent, the component's own name, a missing socket, a taken name and a parent for a non-scene component, changing nothing"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsRefuse")) || !BuildBoomAndCamera())
			{
				return;
			}
			const FString Before = State(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			struct FCase { const TCHAR* What; const TCHAR* Class; const TCHAR* Name; const TCHAR* Parent; const TCHAR* Socket; const TCHAR* Code; };
			const FCase Cases[] = {
				{ TEXT("misspelled parent"), TEXT("/Script/Engine.CameraComponent"), TEXT("Camera2"), TEXT("CamerBoom"), TEXT(""), TEXT("PARENT_NOT_FOUND") },
				{ TEXT("parent is the new component"), TEXT("/Script/Engine.CameraComponent"), TEXT("Camera2"), TEXT("Camera2"), TEXT(""), TEXT("PARENT_IS_SELF") },
				{ TEXT("missing socket"), TEXT("/Script/Engine.CameraComponent"), TEXT("Camera2"), TEXT("CameraBoom"), TEXT("NoSuchSocket"), TEXT("SOCKET_NOT_FOUND") },
				{ TEXT("taken name"), TEXT("/Script/Engine.CameraComponent"), TEXT("CameraBoom"), TEXT(""), TEXT(""), TEXT("NAME_EXISTS") },
				{ TEXT("non-scene with a parent"), TEXT("/Script/Engine.RotatingMovementComponent"), TEXT("Spin"), TEXT("CameraBoom"), TEXT(""), TEXT("NOT_SCENE_COMPONENT") },
				{ TEXT("unknown class"), TEXT("/Script/Engine.NoSuchComponent"), TEXT("Camera2"), TEXT(""), TEXT(""), TEXT("CLASS_NOT_FOUND") },
				{ TEXT("not a component"), TEXT("/Script/Engine.Actor"), TEXT("Camera2"), TEXT(""), TEXT(""), TEXT("NOT_COMPONENT") },
			};
			for (const FCase& Case : Cases)
			{
				const FMcpToolCallResult Result = Add(Blueprint, Case.Class, Case.Name, Case.Parent, Case.Socket);
				TestEqual(FString(Case.What) + TEXT(": error code (") + N2CTest::ResultText(Result).Left(200) + TEXT(")"), ErrorCode(Result), FString(Case.Code));
			}
			const FString Difference = N2CTest::FirstSnapshotDifference(Before, State(Blueprint));
			TestTrue(TEXT("nothing added") + (Difference.IsEmpty() ? FString() : TEXT(" (") + Difference + TEXT(")")), Difference.IsEmpty());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), QueueBefore);
			TestFalse(TEXT("package clean"), Test->IsDirty());
		});

		It(TEXT("adds a non-scene component to the actor, and finds classes by short name, without the U prefix, past a module typo and by a component Blueprint's path"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsClasses")))
			{
				return;
			}
			TSharedPtr<FJsonObject> Spin = ExpectAdded(TEXT("U-less short name"), Blueprint, TEXT("URotatingMovementComponent"), TEXT("Spin"));
			if (Spin.IsValid())
			{
				TestEqual(TEXT("class"), Spin->GetStringField(TEXT("componentClassPath")), FString(TEXT("/Script/Engine.RotatingMovementComponent")));
				TestFalse(TEXT("a non-scene component is attached to nothing"), Spin->HasField(TEXT("attachedTo")));
				TestEqual(TEXT("Spin at the top level"), ParentOf(Blueprint, TEXT("Spin")), FString());
			}
			TSharedPtr<FJsonObject> Mesh = ExpectAdded(TEXT("short name"), Blueprint, TEXT("StaticMeshComponent"), FString());
			if (Mesh.IsValid())
			{
				TestEqual(TEXT("generated name"), Mesh->GetStringField(TEXT("componentName")), FString(TEXT("StaticMesh")));
			}
			TSharedPtr<FJsonObject> Typo = ExpectAdded(TEXT("module typo"), Blueprint, TEXT("/Script/Engin.StaticMeshComponent"), TEXT("Mesh2"));
			if (Typo.IsValid())
			{
				TestEqual(TEXT("typo class"), Typo->GetStringField(TEXT("componentClassPath")), FString(TEXT("/Script/Engine.StaticMeshComponent")));
			}

			UBlueprint* ComponentBlueprint = MakeBlueprint(Test->GetPackage(), UActorComponent::StaticClass(), TEXT("BP_N2CSpecComponent"));
			if (!TestNotNull(TEXT("component Blueprint"), ComponentBlueprint))
			{
				return;
			}
			FKismetEditorUtilities::CompileBlueprint(ComponentBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			TSharedPtr<FJsonObject> ByPath = ExpectAdded(TEXT("component Blueprint path"), Blueprint, ComponentBlueprint->GetPathName(), TEXT("Health"));
			if (ByPath.IsValid())
			{
				TestEqual(TEXT("generated class"), ByPath->GetStringField(TEXT("componentClassPath")), ComponentBlueprint->GeneratedClass->GetPathName());
			}
			TSharedPtr<FJsonObject> ByName = ExpectAdded(TEXT("component Blueprint name"), Blueprint, TEXT("BP_N2CSpecComponent"), FString());
			if (ByName.IsValid())
			{
				TestEqual(TEXT("generated class by name"), ByName->GetStringField(TEXT("componentClassPath")), ComponentBlueprint->GeneratedClass->GetPathName());
				TestEqual(TEXT("generated name without _C"), ByName->GetStringField(TEXT("componentName")), FString(TEXT("BP_N2CSpecComponent")));
			}
		});

		It(TEXT("attaches to a Character's inherited native Mesh, and to a component a parent Blueprint adds, listing inherited components once"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsNative")) || !BuildBoomAndCamera())
			{
				return;
			}
			UBlueprint* Character = MakeBlueprint(Test->GetPackage(), ACharacter::StaticClass(), TEXT("BP_N2CSpecCharacter"));
			if (!TestNotNull(TEXT("Character Blueprint"), Character))
			{
				return;
			}
			FKismetEditorUtilities::CompileBlueprint(Character, EBlueprintCompileOptions::SkipGarbageCollection);
			Test->GetPackage()->SetDirtyFlag(false);
			FString Before = State(Character);
			TSharedPtr<FJsonObject> Head = ExpectAdded(TEXT("HeadCamera on Mesh"), Character, TEXT("/Script/Engine.CameraComponent"), TEXT("HeadCamera"), TEXT("Mesh"));
			if (Head.IsValid())
			{
				TestEqual(TEXT("HeadCamera attachedTo"), Head->GetStringField(TEXT("attachedTo")), FString(TEXT("Mesh")));
				const USCS_Node* HeadNode = OwnNode(Character, TEXT("HeadCamera"));
				TestTrue(TEXT("HeadCamera's node names the native mesh"), HeadNode && HeadNode->bIsParentComponentNative
					&& HeadNode->ParentComponentOrVariableName == ACharacter::MeshComponentName);
				TestEqual(TEXT("HeadCamera's parent after the compile"), ParentOf(Character, TEXT("HeadCamera")), FString(TEXT("Mesh")));
				ExpectUndoRedoState(TEXT("add on Mesh"), Character, Before);
			}
			TSharedPtr<FJsonObject> Default = ExpectAdded(TEXT("default parent on a Character"), Character, TEXT("/Script/Engine.SceneComponent"), TEXT("Pivot"));
			if (Default.IsValid())
			{
				TestEqual(TEXT("Pivot attachedTo the capsule"), Default->GetStringField(TEXT("attachedTo")), FString(TEXT("CapsuleComponent")));
			}
			const TSharedPtr<FJsonObject> CharacterList = N2CTest::ResultJson(List(Character));
			const TArray<TSharedPtr<FJsonValue>>* CharacterComponents = nullptr;
			if (TestTrue(TEXT("Character list"), CharacterList.IsValid() && CharacterList->TryGetArrayField(TEXT("components"), CharacterComponents)))
			{
				TestEqual(TEXT("Character root"), CharacterList->GetStringField(TEXT("rootComponent")), FString(TEXT("CapsuleComponent")));
				TArray<TSharedPtr<FJsonObject>> Mesh;
				FindListed(*CharacterComponents, TEXT("Mesh"), Mesh);
				if (TestEqual(TEXT("Mesh listed once"), Mesh.Num(), 1))
				{
					TestTrue(TEXT("Mesh isNative"), Mesh[0]->GetBoolField(TEXT("isNative")));
					TArray<TSharedPtr<FJsonObject>> HeadListed;
					FindListed(Mesh[0]->GetArrayField(TEXT("children")), TEXT("HeadCamera"), HeadListed);
					TestEqual(TEXT("HeadCamera listed under Mesh"), HeadListed.Num(), 1);
				}
			}
			TestEqual(TEXT("a native component cannot be deleted"), ErrorCode(Delete(Character, TEXT("Mesh"), false)), FString(TEXT("NATIVE_COMPONENT")));

			// A child of the Blueprint that adds CameraBoom
			UBlueprint* Child = MakeBlueprint(Test->GetPackage(), Blueprint->GeneratedClass, TEXT("BP_N2CSpecChild"));
			if (!TestNotNull(TEXT("child Blueprint"), Child))
			{
				return;
			}
			FKismetEditorUtilities::CompileBlueprint(Child, EBlueprintCompileOptions::SkipGarbageCollection);
			Before = State(Child);
			TSharedPtr<FJsonObject> ChildCamera = ExpectAdded(TEXT("ChildCamera on inherited CameraBoom"), Child, TEXT("/Script/Engine.CameraComponent"), TEXT("ChildCamera"), TEXT("CameraBoom"));
			if (ChildCamera.IsValid())
			{
				TestEqual(TEXT("ChildCamera attachedTo"), ChildCamera->GetStringField(TEXT("attachedTo")), FString(TEXT("CameraBoom")));
				TestEqual(TEXT("ChildCamera's parent after the compile"), ParentOf(Child, TEXT("ChildCamera")), FString(TEXT("CameraBoom")));
				ExpectUndoRedoState(TEXT("add on an inherited component"), Child, Before);
			}
			TestEqual(TEXT("an inherited component cannot be deleted"), ErrorCode(Delete(Child, TEXT("CameraBoom"), false)), FString(TEXT("INHERITED_COMPONENT")));
			const TSharedPtr<FJsonObject> ChildList = N2CTest::ResultJson(List(Child));
			const TArray<TSharedPtr<FJsonValue>>* ChildComponents = nullptr;
			if (TestTrue(TEXT("child list"), ChildList.IsValid() && ChildList->TryGetArrayField(TEXT("components"), ChildComponents)))
			{
				TArray<TSharedPtr<FJsonObject>> Found;
				FindListed(*ChildComponents, TEXT("FollowCamera"), Found);
				if (TestEqual(TEXT("an inherited child is listed once"), Found.Num(), 1))
				{
					TestTrue(TEXT("FollowCamera isInherited"), Found[0]->GetBoolField(TEXT("isInherited")));
				}
				TestEqual(TEXT("totalCount"), static_cast<int32>(ChildList->GetNumberField(TEXT("totalCount"))), 4);
			}
			const TSharedPtr<FJsonObject> OwnList = N2CTest::ResultJson(List(Child, /*bIncludeInherited*/ false));
			const TArray<TSharedPtr<FJsonValue>>* OwnComponents = nullptr;
			if (TestTrue(TEXT("own list"), OwnList.IsValid() && OwnList->TryGetArrayField(TEXT("components"), OwnComponents)))
			{
				TestEqual(TEXT("only the child's own component"), OwnComponents->Num(), 1);
				if (OwnComponents->Num() == 1)
				{
					TestEqual(TEXT("own component"), (*OwnComponents)[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("ChildCamera")));
					TestEqual(TEXT("its inherited parent named"), (*OwnComponents)[0]->AsObject()->GetStringField(TEXT("parentComponent")), FString(TEXT("CameraBoom")));
				}
			}
		});
	});

	Describe(TEXT("delete-component-in-actor"), [this]()
	{
		It(TEXT("deletes CameraBoom, promotes FollowCamera to its parent and removes Get CameraBoom, in one undo step that restores CameraBoom with FollowCamera under it"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsDelete")) || !BuildBoomAndCamera())
			{
				return;
			}
			const USCS_Node* BoomNode = OwnNode(Blueprint, TEXT("CameraBoom"));
			UEdGraph* Graph = Test->EventGraph();
			if (!TestTrue(TEXT("CameraBoom node and event graph"), BoomNode && Graph))
			{
				return;
			}
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* GetBoom = Creator.CreateNode();
			GetBoom->VariableReference.SetSelfMember(TEXT("CameraBoom"), BoomNode->VariableGuid);
			Creator.Finalize();
			const FGuid GetBoomGuid = GetBoom->NodeGuid;
			if (!N2CTest::ExpectCompilesClean(*this, TEXT("with Get CameraBoom"), Blueprint))
			{
				return;
			}
			Test->Settle();

			const FString Before = State(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const FMcpToolCallResult Result = Delete(Blueprint, TEXT("CameraBoom"), false);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("delete CameraBoom"), Result))
			{
				AddError(N2CTest::ResultText(Result).Left(400));
				return;
			}
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			TestNull(TEXT("CameraBoom gone"), OwnNode(Blueprint, TEXT("CameraBoom")));
			TestEqual(TEXT("FollowCamera promoted to DefaultSceneRoot"), ParentOf(Blueprint, TEXT("FollowCamera")), FString(TEXT("DefaultSceneRoot")));
			TestNull(TEXT("Get CameraBoom removed"), N2CTest::FindNodeByGuid(Blueprint, GetBoomGuid.ToString()));
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (TestTrue(TEXT("result JSON"), Json.IsValid()))
			{
				const TArray<TSharedPtr<FJsonValue>>& Children = Json->GetArrayField(TEXT("affectedChildren"));
				if (TestEqual(TEXT("one affected child"), Children.Num(), 1))
				{
					const TSharedPtr<FJsonObject> Child = Children[0]->AsObject();
					TestEqual(TEXT("child"), Child->GetStringField(TEXT("name")), FString(TEXT("FollowCamera")));
					TestEqual(TEXT("action"), Child->GetStringField(TEXT("action")), FString(TEXT("reparented")));
					TestEqual(TEXT("newParent"), Child->GetStringField(TEXT("newParent")), FString(TEXT("DefaultSceneRoot")));
				}
				const TArray<TSharedPtr<FJsonValue>>& Removed = Json->GetArrayField(TEXT("removedVariableNodes"));
				if (TestEqual(TEXT("one removed variable node"), Removed.Num(), 1))
				{
					FGuid RemovedGuid;
					FGuid::Parse(Removed[0]->AsObject()->GetStringField(TEXT("nodeGuid")), RemovedGuid);
					TestEqual(TEXT("removed node GUID"), RemovedGuid, GetBoomGuid);
				}
			}
			ExpectUndoRedoState(TEXT("delete CameraBoom"), Blueprint, Before);
		});

		It(TEXT("reports an event bound to the deleted component, which stays, without opening the Blueprint editor, in one undo step that Undo then Redo restore exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsBoundEvent"))
				|| !ExpectAdded(TEXT("add Box"), Blueprint, TEXT("/Script/Engine.StaticMeshComponent"), TEXT("Box")).IsValid())
			{
				return;
			}
			UEdGraph* Graph = Test->EventGraph();
			const FObjectProperty* BoxProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, TEXT("Box"));
			const FMulticastDelegateProperty* HitDelegate = FindFProperty<FMulticastDelegateProperty>(UPrimitiveComponent::StaticClass(),
				GET_MEMBER_NAME_CHECKED(UPrimitiveComponent, OnComponentHit));
			if (!TestTrue(TEXT("Box property and OnComponentHit"), Graph && BoxProperty && HitDelegate))
			{
				return;
			}
			FGraphNodeCreator<UK2Node_ComponentBoundEvent> Creator(*Graph);
			UK2Node_ComponentBoundEvent* OnHit = Creator.CreateNode();
			OnHit->InitializeComponentBoundEventParams(BoxProperty, HitDelegate);
			Creator.Finalize();
			const FGuid OnHitGuid = OnHit->NodeGuid;
			if (!N2CTest::ExpectCompilesClean(*this, TEXT("with On Component Hit (Box)"), Blueprint))
			{
				return;
			}
			Test->Settle();

			const FString Before = State(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const FMcpToolCallResult Result = Delete(Blueprint, TEXT("Box"), false);
			if (!TestFalse(TEXT("delete Box: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError))
			{
				return;
			}
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
			TestNull(TEXT("Box gone"), OwnNode(Blueprint, TEXT("Box")));
			TestNotNull(TEXT("the bound event stays"), N2CTest::FindNodeByGuid(Blueprint, OnHitGuid.ToString()));
			UAssetEditorSubsystem* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			TestTrue(TEXT("no Blueprint editor opened"), Editors && !Editors->FindEditorForAsset(Blueprint, /*bFocusIfOpen*/ false));
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (TestTrue(TEXT("result JSON"), Json.IsValid()))
			{
				const TArray<TSharedPtr<FJsonValue>>& Events = Json->GetArrayField(TEXT("boundEvents"));
				if (TestEqual(TEXT("one bound event reported"), Events.Num(), 1))
				{
					FGuid Reported;
					FGuid::Parse(Events[0]->AsObject()->GetStringField(TEXT("nodeGuid")), Reported);
					TestEqual(TEXT("bound event GUID"), Reported, OnHitGuid);
					TestEqual(TEXT("bound event component"), Events[0]->AsObject()->GetStringField(TEXT("component")), FString(TEXT("Box")));
				}
			}
			const TSharedPtr<FJsonObject> Compile = N2CTest::ResultCompile(Result);
			TestTrue(TEXT("the call compiled, without errors (the engine leaves the event in place, unbound)"), Compile.IsValid() && Compile->GetNumberField(TEXT("errorCount")) == 0);
			ExpectUndoRedoState(TEXT("delete Box"), Blueprint, Before);
		});

		It(TEXT("with deleteChildren deletes the subtree, and refuses DefaultSceneRoot and unknown names, changing nothing"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsDeleteAll")) || !BuildBoomAndCamera())
			{
				return;
			}
			const FString Before = State(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			TestEqual(TEXT("DefaultSceneRoot"), ErrorCode(Delete(Blueprint, TEXT("DefaultSceneRoot"), false)), FString(TEXT("DEFAULT_SCENE_ROOT")));
			TestEqual(TEXT("unknown"), ErrorCode(Delete(Blueprint, TEXT("NoSuchComponent"), false)), FString(TEXT("COMPONENT_NOT_FOUND")));
			const FString Difference = N2CTest::FirstSnapshotDifference(Before, State(Blueprint));
			TestTrue(TEXT("refusals change nothing") + (Difference.IsEmpty() ? FString() : TEXT(" (") + Difference + TEXT(")")), Difference.IsEmpty());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), QueueBefore);
			TestFalse(TEXT("package clean"), Test->IsDirty());

			const FMcpToolCallResult Result = Delete(Blueprint, TEXT("CameraBoom"), true);
			if (N2CTest::ExpectToolCompiled(*this, TEXT("delete subtree"), Result))
			{
				TestNull(TEXT("CameraBoom gone"), OwnNode(Blueprint, TEXT("CameraBoom")));
				TestNull(TEXT("FollowCamera gone"), OwnNode(Blueprint, TEXT("FollowCamera")));
				const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
				const TArray<TSharedPtr<FJsonValue>>& Children = Json->GetArrayField(TEXT("affectedChildren"));
				TestTrue(TEXT("FollowCamera reported deleted"), Children.Num() == 1
					&& Children[0]->AsObject()->GetStringField(TEXT("action")) == TEXT("deleted"));
				ExpectUndoRedoState(TEXT("delete subtree"), Blueprint, Before);
			}
		});
	});

	Describe(TEXT("construction script support"), [this]()
	{
		It(TEXT("refuses a Blueprint without a construction script and never gives it an SCS, and list changes nothing"), [this]()
		{
			if (!Build(TEXT("BP_N2CComponentsNoSCS")))
			{
				return;
			}
			UBlueprint* Macros = MakeBlueprint(Test->GetPackage(), AActor::StaticClass(), TEXT("BP_N2CSpecMacros"), BPTYPE_MacroLibrary);
			if (!TestNotNull(TEXT("macro library"), Macros))
			{
				return;
			}
			TestNull(TEXT("a macro library has no SCS"), Macros->SimpleConstructionScript.Get());
			TestEqual(TEXT("list"), ErrorCode(List(Macros)), FString(TEXT("NO_CONSTRUCTION_SCRIPT")));
			TestEqual(TEXT("add"), ErrorCode(Add(Macros, TEXT("/Script/Engine.SceneComponent"), TEXT("Pivot"))), FString(TEXT("NO_CONSTRUCTION_SCRIPT")));
			TestEqual(TEXT("delete"), ErrorCode(Delete(Macros, TEXT("Pivot"), false)), FString(TEXT("NO_CONSTRUCTION_SCRIPT")));
			TestNull(TEXT("still no SCS"), Macros->SimpleConstructionScript.Get());

			const FString Before = State(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const FMcpToolCallResult Listed = List(Blueprint);
			TestFalse(TEXT("list succeeds"), Listed.bIsError);
			TestEqual(TEXT("list adds no undo step"), N2CTest::UndoQueueLength(), QueueBefore);
			TestTrue(TEXT("list changes nothing"), N2CTest::FirstSnapshotDifference(Before, State(Blueprint)).IsEmpty());
			TestFalse(TEXT("list leaves the package clean"), Test->IsDirty());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
