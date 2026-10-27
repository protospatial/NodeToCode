// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestContentFolder.h"
#include "MCP/Utils/N2CMcpComponentUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Character.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"

/**
 * The Blueprint structure tools end to end (card 0163): create-blueprint-class, add-component-class-to-actor,
 * delete-component-in-actor, list-components-in-actor and search-blueprint-classes, run the way an agent runs them
 * through tools/call, on Blueprints saved under /Game/__N2CTestStructure (deleted before and after every case, also
 * when one fails) or a transient Blueprint. They keep the E12 fixes (cards 0166, 0167) correct on 5.6 and 5.7 after
 * the freeze (dec-5). The tools' own refusals, undo and edge cases are NodeToCode.MCP.CreateBlueprintClass and
 * NodeToCode.MCP.Components. Review ids: imp-mcp-tools-bp-structure-9, mcp-tools-bp-structure-1, -2, -3, -4, -7, -12.
 */
namespace N2CStructureToolsSpec
{
	/** Every SCS node reachable from the root nodes through ChildNodes */
	void Reachable(USCS_Node* Node, TSet<USCS_Node*>& Out)
	{
		if (!Node || Out.Contains(Node))
		{
			return;
		}
		Out.Add(Node);
		for (USCS_Node* Child : Node->GetChildNodes())
		{
			Reachable(Child, Out);
		}
	}

	FString NodeNames(const TSet<USCS_Node*>& Nodes)
	{
		TArray<FString> Names;
		for (const USCS_Node* Node : Nodes)
		{
			Names.Add(Node ? Node->GetVariableName().ToString() : TEXT("<null>"));
		}
		Names.Sort();
		return FString::Join(Names, TEXT(", "));
	}

	/** The SCS node with this variable name, or null */
	USCS_Node* FindNode(UBlueprint* Blueprint, const FString& Name)
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

	/** The variable name of the node whose ChildNodes hold Name's node ("" for a root node, "<missing>" when absent) */
	FString ParentNodeName(UBlueprint* Blueprint, const FString& Name)
	{
		const USCS_Node* Node = FindNode(Blueprint, Name);
		if (!Node)
		{
			return TEXT("<missing>");
		}
		for (const USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Candidate && Candidate->GetChildNodes().Contains(Node))
			{
				return Candidate->GetVariableName().ToString();
			}
		}
		return FString();
	}

	/** Every listed component of a list-components-in-actor tree, depth first, as {name, parent name} */
	void Flatten(const TArray<TSharedPtr<FJsonValue>>& Components, const FString& Parent, TArray<TPair<FString, FString>>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Value : Components)
		{
			const TSharedPtr<FJsonObject> Component = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Component.IsValid())
			{
				continue;
			}
			const FString Name = Component->GetStringField(TEXT("name"));
			Out.Emplace(Name, Parent);
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Component->TryGetArrayField(TEXT("children"), Children))
			{
				Flatten(*Children, Name, Out);
			}
		}
	}
}

BEGIN_DEFINE_SPEC(FN2CStructureToolsSpec, "NodeToCode.MCP.Structure", N2C_TEST_FLAGS)

	/** The saved scratch folder, created as an existing folder before a case creates into it */
	const FString Folder = TEXT("/Game/__N2CTestStructure");

	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;

	/**
	 * A case that fails until its finding is fixed: an xIt, or an It when N2C_RUN_KNOWN_DEFECTS=1 is in the editor's
	 * environment (to see which still fail). The fixing card turns it into an It.
	 */
	void KnownDefectIt(const FString& CaseText, TFunction<void()> Body)
	{
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_RUN_KNOWN_DEFECTS")) == TEXT("1"))
		{
			It(CaseText, MoveTemp(Body));
		}
		else
		{
			xIt(CaseText, MoveTemp(Body));
		}
	}

	FMcpToolCallResult Run(const TCHAR* Tool, const TSharedPtr<FJsonObject>& Args)
	{
		return N2CTest::RunTool(Tool, Args);
	}

	/** Runs a tool and checks it succeeded; its JSON, or null */
	TSharedPtr<FJsonObject> ExpectOk(const FString& What, const TCHAR* Tool, const TSharedPtr<FJsonObject>& Args)
	{
		const FMcpToolCallResult Result = Run(Tool, Args);
		if (!TestFalse(What + TEXT(" succeeds (") + N2CTest::ResultText(Result).Left(400) + TEXT(")"), Result.bIsError))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		TestTrue(What + TEXT(": result JSON"), Json.IsValid());
		return Json;
	}

	/** create-folder of the scratch folder, so every Blueprint below is created into an existing folder */
	bool MakeFolder()
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("folderPath"), Folder);
		return ExpectOk(TEXT("create-folder"), TEXT("create-folder"), Args).IsValid()
			&& TestTrue(TEXT("the folder exists before create-blueprint-class runs"), N2CTest::ContentFolderExists(Folder));
	}

	/** create-blueprint-class into the scratch folder, not opened; the saved Blueprint, or null */
	UBlueprint* Create(const FString& Name, const FString& Parent, const FString& Type = FString())
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintName"), Name);
		Args->SetStringField(TEXT("parentClassPath"), Parent);
		Args->SetStringField(TEXT("assetPath"), Folder);
		Args->SetBoolField(TEXT("addPrefix"), false);
		Args->SetBoolField(TEXT("openInEditor"), false);
		if (!Type.IsEmpty())
		{
			Args->SetStringField(TEXT("blueprintType"), Type);
		}
		const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("create ") + Name, TEXT("create-blueprint-class"), Args);
		FString AssetPath;
		if (!Json.IsValid() || !TestTrue(Name + TEXT(": assetPath in the result"), Json->TryGetStringField(TEXT("assetPath"), AssetPath)))
		{
			return nullptr;
		}
		TestEqual(Name + TEXT(": created in the existing folder"), FPackageName::GetLongPackagePath(FPackageName::ObjectPathToPackageName(AssetPath)), Folder);
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
		TestNotNull(Name + TEXT(": the Blueprint loads"), Blueprint);
		return Blueprint;
	}

	TSharedPtr<FJsonObject> AddComponent(UBlueprint* Blueprint, const FString& Class, const FString& Name, const FString& Parent = FString())
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Args->SetStringField(TEXT("componentClass"), Class);
		Args->SetStringField(TEXT("componentName"), Name);
		if (!Parent.IsEmpty())
		{
			Args->SetStringField(TEXT("parentComponent"), Parent);
		}
		return ExpectOk(TEXT("add ") + Name, TEXT("add-component-class-to-actor"), Args);
	}

	TSharedPtr<FJsonObject> ListComponents(UBlueprint* Blueprint)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Args->SetBoolField(TEXT("includeInherited"), true);
		return ExpectOk(TEXT("list-components-in-actor"), TEXT("list-components-in-actor"), Args);
	}

	/** search-blueprint-classes with the largest page; the classes array, or null */
	const TArray<TSharedPtr<FJsonValue>>* Search(const FString& Term, TSharedPtr<FJsonObject>& OutJson)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("searchTerm"), Term);
		Args->SetNumberField(TEXT("maxResults"), 200);
		OutJson = ExpectOk(TEXT("search ") + Term, TEXT("search-blueprint-classes"), Args);
		const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
		if (!OutJson.IsValid() || !TestTrue(TEXT("search ") + Term + TEXT(": classes"), OutJson->TryGetArrayField(TEXT("classes"), Classes)))
		{
			return nullptr;
		}
		return Classes;
	}

	/** The entry of a search result whose classPath is ClassPath, or null */
	static TSharedPtr<FJsonObject> FindClass(const TArray<TSharedPtr<FJsonValue>>& Classes, const FString& ClassPath)
	{
		for (const TSharedPtr<FJsonValue>& Value : Classes)
		{
			if (Value->AsObject()->GetStringField(TEXT("classPath")) == ClassPath)
			{
				return Value->AsObject();
			}
		}
		return nullptr;
	}

	/** Every SCS node is reachable from the root nodes and every reachable node is in AllNodes */
	void ExpectAllNodesReachable(const FString& What, UBlueprint* Blueprint)
	{
		using namespace N2CStructureToolsSpec;
		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		if (!TestNotNull(What + TEXT(": SCS"), SCS))
		{
			return;
		}
		TSet<USCS_Node*> FromRoots;
		for (USCS_Node* Root : SCS->GetRootNodes())
		{
			Reachable(Root, FromRoots);
		}
		const TSet<USCS_Node*> All(SCS->GetAllNodes());
		const TSet<USCS_Node*> Orphans = All.Difference(FromRoots);
		const TSet<USCS_Node*> Ghosts = FromRoots.Difference(All);
		TestTrue(What + TEXT(": every node in AllNodes is reachable from the roots (orphans: ") + NodeNames(Orphans) + TEXT(")"), Orphans.Num() == 0);
		TestTrue(What + TEXT(": every reachable node is in AllNodes (removed nodes still linked: ") + NodeNames(Ghosts) + TEXT(")"), Ghosts.Num() == 0);
	}

END_DEFINE_SPEC(FN2CStructureToolsSpec)

void FN2CStructureToolsSpec::Define()
{
	using namespace N2CStructureToolsSpec;

	BeforeEach([this]()
	{
		N2CTest::DeleteContentFolder(Folder);
	});

	AfterEach([this]()
	{
		Test.Reset();
		N2CTest::DeleteContentFolder(Folder);
		TestFalse(TEXT("scratch folder deleted"), N2CTest::ContentFolderOnDisk(Folder));
	});

	Describe(TEXT("create-blueprint-class"), [this]()
	{
		It(TEXT("creates Actor, Widget and Interface Blueprints into one existing folder"), [this]()
		{
			if (!MakeFolder())
			{
				return;
			}
			UBlueprint* Actor = Create(TEXT("BP_N2CStructActor"), TEXT("/Script/Engine.Actor"));
			if (Actor)
			{
				TestEqual(TEXT("Actor: a plain Blueprint"), Actor->GetClass(), UBlueprint::StaticClass());
				TestEqual(TEXT("Actor: parent"), Actor->ParentClass.Get(), AActor::StaticClass());
				TestEqual(TEXT("Actor: normal type"), static_cast<int32>(Actor->BlueprintType), static_cast<int32>(BPTYPE_Normal));
				N2CTest::ExpectCompilesClean(*this, TEXT("Actor"), Actor);
			}
			UBlueprint* Widget = Create(TEXT("WBP_N2CStructMenu"), TEXT("/Script/UMG.UserWidget"));
			if (Widget)
			{
				const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Widget);
				TestNotNull(TEXT("Widget: a UWidgetBlueprint (mcp-tools-bp-structure-4)"), WidgetBlueprint);
				TestTrue(TEXT("Widget: with a widget tree"), WidgetBlueprint && WidgetBlueprint->WidgetTree);
				TestTrue(TEXT("Widget: a widget generated class"), Widget->GeneratedClass && Widget->GeneratedClass->IsChildOf(UUserWidget::StaticClass()));
				N2CTest::ExpectCompilesClean(*this, TEXT("Widget"), Widget);
			}
			UBlueprint* Interface = Create(TEXT("BPI_N2CStructUsable"), TEXT("/Script/CoreUObject.Interface"), TEXT("Interface"));
			if (Interface)
			{
				TestEqual(TEXT("Interface: interface type"), static_cast<int32>(Interface->BlueprintType), static_cast<int32>(BPTYPE_Interface));
				TestTrue(TEXT("Interface: an interface class"), Interface->GeneratedClass && Interface->GeneratedClass->HasAnyClassFlags(CLASS_Interface));
				N2CTest::ExpectCompilesClean(*this, TEXT("Interface"), Interface);
			}
			for (const TCHAR* Name : { TEXT("BP_N2CStructActor"), TEXT("WBP_N2CStructMenu"), TEXT("BPI_N2CStructUsable") })
			{
				TestTrue(FString(Name) + TEXT(": saved to disk"), FPackageName::DoesPackageExist(Folder / Name));
			}
		});
	});

	Describe(TEXT("components"), [this]()
	{
		It(TEXT("adds a SpringArm and then a Camera under it to a created Blueprint, and the Camera is in the SpringArm's ChildNodes after each compile"), [this]()
		{
			if (!MakeFolder())
			{
				return;
			}
			UBlueprint* Blueprint = Create(TEXT("BP_N2CStructRig"), TEXT("/Script/Engine.Actor"));
			if (!Blueprint
				|| !AddComponent(Blueprint, TEXT("/Script/Engine.SpringArmComponent"), TEXT("CameraBoom")).IsValid()
				|| !AddComponent(Blueprint, TEXT("/Script/Engine.CameraComponent"), TEXT("FollowCamera"), TEXT("CameraBoom")).IsValid())
			{
				return;
			}
			// The tool compiled once per call; compile twice more, as saving and the editor would
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				const FString When = FString::Printf(TEXT("compile %d"), Pass + 1);
				if (Pass > 0)
				{
					N2CTest::ExpectCompilesClean(*this, When, Blueprint);
				}
				const USCS_Node* Boom = FindNode(Blueprint, TEXT("CameraBoom"));
				const USCS_Node* Camera = FindNode(Blueprint, TEXT("FollowCamera"));
				TestTrue(When + TEXT(": FollowCamera in CameraBoom's ChildNodes (mcp-tools-bp-structure-2)"), Boom && Camera && Boom->GetChildNodes().Contains(Camera));
				TestFalse(When + TEXT(": FollowCamera is not a root node"), Camera && Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Camera));
				TestEqual(When + TEXT(": CameraBoom under DefaultSceneRoot"), ParentNodeName(Blueprint, TEXT("CameraBoom")), FString(TEXT("DefaultSceneRoot")));
				ExpectAllNodesReachable(When, Blueprint);
			}
		});

		It(TEXT("deletes a mid-hierarchy component, promotes its children with their own children, and leaves AllNodes equal to the nodes reachable from the roots"), [this]()
		{
			Test = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CStructDelete"));
			UBlueprint* Blueprint = Test->GetBlueprint();
			if (!TestNotNull(TEXT("Blueprint"), Blueprint))
			{
				return;
			}
			Test->Settle();
			// DefaultSceneRoot > Boom > (Camera > Lens), (Light)
			if (!AddComponent(Blueprint, TEXT("/Script/Engine.SpringArmComponent"), TEXT("Boom")).IsValid()
				|| !AddComponent(Blueprint, TEXT("/Script/Engine.CameraComponent"), TEXT("Camera"), TEXT("Boom")).IsValid()
				|| !AddComponent(Blueprint, TEXT("/Script/Engine.SceneComponent"), TEXT("Lens"), TEXT("Camera")).IsValid()
				|| !AddComponent(Blueprint, TEXT("/Script/Engine.PointLightComponent"), TEXT("Light"), TEXT("Boom")).IsValid())
			{
				return;
			}
			ExpectAllNodesReachable(TEXT("before the delete"), Blueprint);
			const int32 NodesBefore = Blueprint->SimpleConstructionScript->GetAllNodes().Num();

			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
			Args->SetStringField(TEXT("componentName"), TEXT("Boom"));
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("delete Boom"), TEXT("delete-component-in-actor"), Args);
			if (!Json.IsValid())
			{
				return;
			}
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const FString When = Pass == 0 ? FString(TEXT("after the delete")) : FString(TEXT("after another compile"));
				if (Pass > 0)
				{
					N2CTest::ExpectCompilesClean(*this, When, Blueprint);
				}
				TestNull(When + TEXT(": Boom gone"), FindNode(Blueprint, TEXT("Boom")));
				TestEqual(When + TEXT(": one node fewer"), Blueprint->SimpleConstructionScript->GetAllNodes().Num(), NodesBefore - 1);
				TestEqual(When + TEXT(": Camera promoted to Boom's parent (mcp-tools-bp-structure-1)"), ParentNodeName(Blueprint, TEXT("Camera")), FString(TEXT("DefaultSceneRoot")));
				TestEqual(When + TEXT(": Light promoted to Boom's parent"), ParentNodeName(Blueprint, TEXT("Light")), FString(TEXT("DefaultSceneRoot")));
				TestEqual(When + TEXT(": Lens stays under Camera"), ParentNodeName(Blueprint, TEXT("Lens")), FString(TEXT("Camera")));
				ExpectAllNodesReachable(When, Blueprint);
			}
			TArray<FString> Reported;
			for (const TSharedPtr<FJsonValue>& Child : Json->GetArrayField(TEXT("affectedChildren")))
			{
				Reported.Add(Child->AsObject()->GetStringField(TEXT("name")) + TEXT("->") + Child->AsObject()->GetStringField(TEXT("newParent")));
			}
			Reported.Sort();
			TestEqual(TEXT("affectedChildren names the two promoted children"), FString::Join(Reported, TEXT(", ")),
				FString(TEXT("Camera->DefaultSceneRoot, Light->DefaultSceneRoot")));
		});

		It(TEXT("lists a Character child Blueprint's native, inherited and own components once each, under their parents"), [this]()
		{
			if (!MakeFolder())
			{
				return;
			}
			UBlueprint* Character = Create(TEXT("BP_N2CStructHero"), TEXT("/Script/Engine.Character"));
			if (!Character
				|| !AddComponent(Character, TEXT("/Script/Engine.SpringArmComponent"), TEXT("CameraBoom")).IsValid()
				|| !AddComponent(Character, TEXT("/Script/Engine.CameraComponent"), TEXT("FollowCamera"), TEXT("CameraBoom")).IsValid())
			{
				return;
			}
			UBlueprint* Child = Create(TEXT("BP_N2CStructHeroChild"), Character->GeneratedClass->GetPathName());
			if (!Child || !TestEqual(TEXT("child's parent class"), Child->ParentClass.Get(), Character->GeneratedClass.Get())
				|| !AddComponent(Child, TEXT("/Script/Engine.StaticMeshComponent"), TEXT("Hat"), TEXT("Mesh")).IsValid())
			{
				return;
			}
			const TSharedPtr<FJsonObject> Listed = ListComponents(Child);
			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			if (!Listed.IsValid() || !TestTrue(TEXT("components array"), Listed->TryGetArrayField(TEXT("components"), Components)))
			{
				return;
			}
			TArray<TPair<FString, FString>> Flat;
			Flatten(*Components, FString(), Flat);
			TMap<FString, int32> Counts;
			TMap<FString, FString> Parents;
			for (const TPair<FString, FString>& Entry : Flat)
			{
				++Counts.FindOrAdd(Entry.Key);
				Parents.Add(Entry.Key, Entry.Value);
			}
			for (const TPair<FString, int32>& Count : Counts)
			{
				TestEqual(Count.Key + TEXT(" listed once (mcp-tools-bp-structure-7)"), Count.Value, 1);
			}
			for (const TCHAR* Native : { TEXT("CapsuleComponent"), TEXT("Mesh"), TEXT("CharacterMovement") })
			{
				TestEqual(FString(Native) + TEXT(": the native component is listed"), Counts.FindRef(Native), 1);
			}
			TestEqual(TEXT("totalCount counts every listed component"), static_cast<int32>(Listed->GetNumberField(TEXT("totalCount"))), Flat.Num());
			TestEqual(TEXT("rootComponent is the capsule"), Listed->GetStringField(TEXT("rootComponent")), FString(TEXT("CapsuleComponent")));
			TestEqual(TEXT("Mesh under the capsule"), Parents.FindRef(TEXT("Mesh")), FString(TEXT("CapsuleComponent")));
			TestEqual(TEXT("inherited CameraBoom under the capsule"), Parents.FindRef(TEXT("CameraBoom")), FString(TEXT("CapsuleComponent")));
			TestEqual(TEXT("inherited FollowCamera under CameraBoom"), Parents.FindRef(TEXT("FollowCamera")), FString(TEXT("CameraBoom")));
			TestEqual(TEXT("own Hat under the native Mesh"), Parents.FindRef(TEXT("Hat")), FString(TEXT("Mesh")));
		});
	});

	Describe(TEXT("search-blueprint-classes"), [this]()
	{
		// mcp-tools-bp-structure-12 (card 0288): the common classes were added a second time
		It(TEXT("lists each native class once"), [this]()
		{
			for (const TCHAR* Term : { TEXT("Actor"), TEXT("Pawn"), TEXT("Character") })
			{
				TSharedPtr<FJsonObject> Json;
				const TArray<TSharedPtr<FJsonValue>>* Classes = Search(Term, Json);
				if (!Classes)
				{
					continue;
				}
				TMap<FString, int32> Counts;
				for (const TSharedPtr<FJsonValue>& Value : *Classes)
				{
					++Counts.FindOrAdd(Value->AsObject()->GetStringField(TEXT("classPath")));
				}
				for (const TPair<FString, int32>& Count : Counts)
				{
					TestEqual(FString(Term) + TEXT(": ") + Count.Key + TEXT(" listed once"), Count.Value, 1);
				}
				TestEqual(FString(Term) + TEXT(": /Script/Engine.") + Term + TEXT(" found"), Counts.FindRef(FString(TEXT("/Script/Engine.")) + Term), 1);
			}
		});

		// mcp-tools-bp-structure-12 (card 0288): the native pass let loaded Blueprint classes through
		It(TEXT("lists a loaded Blueprint class once, as a Blueprint class"), [this]()
		{
			if (!MakeFolder())
			{
				return;
			}
			UBlueprint* Blueprint = Create(TEXT("BP_N2CStructSearchable"), TEXT("/Script/Engine.Actor"));
			if (!Blueprint)
			{
				return;
			}
			TSharedPtr<FJsonObject> Json;
			const TArray<TSharedPtr<FJsonValue>>* Classes = Search(TEXT("BP_N2CStructSearchable"), Json);
			if (!Classes)
			{
				return;
			}
			TArray<TSharedPtr<FJsonObject>> Found;
			for (const TSharedPtr<FJsonValue>& Value : *Classes)
			{
				if (Value->AsObject()->GetStringField(TEXT("classPath")).Contains(TEXT("BP_N2CStructSearchable")))
				{
					Found.Add(Value->AsObject());
				}
			}
			if (TestEqual(TEXT("the Blueprint class listed once"), Found.Num(), 1))
			{
				TestFalse(TEXT("not reported as native"), Found[0]->GetBoolField(TEXT("isNative")));
			}
		});
		// mcp-tools-bp-structure-20 (card 0288): a leading A or U was stripped from the class name
		It(TEXT("names classes by their display name, without dropping a first letter"), [this]()
		{
			for (const TPair<const TCHAR*, const TCHAR*>& Expected : TArray<TPair<const TCHAR*, const TCHAR*>>{
				{ TEXT("/Script/Engine.Actor"), TEXT("Actor") }, { TEXT("/Script/UMG.UserWidget"), TEXT("User Widget") },
				{ TEXT("/Script/AIModule.AIController"), TEXT("AIController") } })
			{
				const FString Path = Expected.Key;
				const FString Term = FPackageName::ObjectPathToObjectName(FString(Path));
				TSharedPtr<FJsonObject> Json;
				const TArray<TSharedPtr<FJsonValue>>* Classes = Search(Term, Json);
				const TSharedPtr<FJsonObject> Entry = Classes ? FindClass(*Classes, Path) : nullptr;
				if (TestTrue(Term + TEXT(" found"), Entry.IsValid()))
				{
					TestEqual(Term + TEXT(": displayName"), Entry->GetStringField(TEXT("displayName")),
						FindObject<UClass>(nullptr, *Path)->GetDisplayNameText().ToString());
					TestFalse(Term + TEXT(": displayName keeps its first letter"), Entry->GetStringField(TEXT("displayName")).StartsWith(Expected.Value + 1));
				}
			}
			TSharedPtr<FJsonObject> Json;
			const TArray<TSharedPtr<FJsonValue>>* Classes = Search(TEXT("Actor"), Json);
			const TSharedPtr<FJsonObject> Actor = Classes ? FindClass(*Classes, TEXT("/Script/Engine.Actor")) : nullptr;
			if (TestTrue(TEXT("Actor found"), Actor.IsValid()))
			{
				TestEqual(TEXT("Actor's displayName"), Actor->GetStringField(TEXT("displayName")), FString(TEXT("Actor")));
				TestTrue(TEXT("Actor is a common class"), Actor->GetBoolField(TEXT("commonClass")));
				TestEqual(TEXT("Actor ranks first for 'Actor'"), (*Classes)[0]->AsObject()->GetStringField(TEXT("classPath")), FString(TEXT("/Script/Engine.Actor")));
			}
		});

		// mcp-tools-bp-structure-13 (card 0288): classType matched a substring of the parent's export text, and names kept it
		It(TEXT("lists a Character Blueprint under classType actor with its object path and names"), [this]()
		{
			if (!MakeFolder())
			{
				return;
			}
			UBlueprint* Blueprint = Create(TEXT("BP_N2CStructSearchChar"), TEXT("/Script/Engine.Character"));
			if (!Blueprint)
			{
				return;
			}
			const FString ClassPath = Folder + TEXT("/BP_N2CStructSearchChar.BP_N2CStructSearchChar_C");
			for (const TCHAR* Type : { TEXT("actor"), TEXT("all") })
			{
				TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), TEXT("BP_N2CStructSearchChar"));
				Args->SetStringField(TEXT("classType"), Type);
				const TSharedPtr<FJsonObject> Json = ExpectOk(FString(TEXT("search ")) + Type, TEXT("search-blueprint-classes"), Args);
				const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
				const TSharedPtr<FJsonObject> Entry = Json.IsValid() && Json->TryGetArrayField(TEXT("classes"), Classes) ? FindClass(*Classes, ClassPath) : nullptr;
				if (TestTrue(FString(Type) + TEXT(": the Character Blueprint listed by its object path"), Entry.IsValid()))
				{
					TestEqual(FString(Type) + TEXT(": className"), Entry->GetStringField(TEXT("className")), FString(TEXT("BP_N2CStructSearchChar_C")));
					TestEqual(FString(Type) + TEXT(": parentClass"), Entry->GetStringField(TEXT("parentClass")), FString(TEXT("Character")));
					TestFalse(FString(Type) + TEXT(": not native"), Entry->GetBoolField(TEXT("isNative")));
				}
			}
			for (const TCHAR* Type : { TEXT("actorComponent"), TEXT("userWidget"), TEXT("object") })
			{
				TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), TEXT("BP_N2CStructSearchChar"));
				Args->SetStringField(TEXT("classType"), Type);
				const TSharedPtr<FJsonObject> Json = ExpectOk(FString(TEXT("search ")) + Type, TEXT("search-blueprint-classes"), Args);
				const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
				if (Json.IsValid() && Json->TryGetArrayField(TEXT("classes"), Classes))
				{
					TestFalse(FString(Type) + TEXT(": the Character Blueprint is not listed"), FindClass(*Classes, ClassPath).IsValid());
				}
			}
		});

		// mcp-tools-bp-structure-22 (card 0288): abstract classes, valid parents, were hidden by default
		It(TEXT("finds abstract parent classes by default and marks them isAbstract"), [this]()
		{
			for (const TCHAR* Path : { TEXT("/Script/Engine.PrimaryDataAsset"), TEXT("/Script/UMG.UserWidget") })
			{
				const FString Term = FPackageName::ObjectPathToObjectName(FString(Path));
				TSharedPtr<FJsonObject> Json;
				const TArray<TSharedPtr<FJsonValue>>* Classes = Search(Term, Json);
				const TSharedPtr<FJsonObject> Entry = Classes ? FindClass(*Classes, Path) : nullptr;
				if (TestTrue(Term + TEXT(" found"), Entry.IsValid()))
				{
					TestTrue(Term + TEXT(": isAbstract"), Entry->GetBoolField(TEXT("isAbstract")));
				}
			}
			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("searchTerm"), TEXT("PrimaryDataAsset"));
			Args->SetBoolField(TEXT("includeAbstract"), false);
			const TSharedPtr<FJsonObject> Json = ExpectOk(TEXT("search without abstract"), TEXT("search-blueprint-classes"), Args);
			const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
			if (Json.IsValid() && Json->TryGetArrayField(TEXT("classes"), Classes))
			{
				TestFalse(TEXT("includeAbstract false leaves PrimaryDataAsset out"), FindClass(*Classes, TEXT("/Script/Engine.PrimaryDataAsset")).IsValid());
			}
		});

		// mcp-tools-bp-structure-21 (card 0288): totalFound counted every collected class, and common classes skipped the filters
		It(TEXT("counts the matches in totalFound and applies every filter to common classes"), [this]()
		{
			TSharedPtr<FJsonObject> Json;
			const TArray<TSharedPtr<FJsonValue>>* Classes = Search(TEXT("zzqqN2CNoSuchClass"), Json);
			if (Classes)
			{
				TestEqual(TEXT("no match: no classes"), Classes->Num(), 0);
				TestEqual(TEXT("no match: totalFound 0"), static_cast<int32>(Json->GetNumberField(TEXT("totalFound"))), 0);
				TestFalse(TEXT("no match: hasMore false"), Json->GetBoolField(TEXT("hasMore")));
			}
			Classes = Search(TEXT("Character"), Json);
			if (Classes && TestTrue(TEXT("Character: a page that holds every match"), Classes->Num() < 200))
			{
				TestEqual(TEXT("Character: totalFound is the number listed"), static_cast<int32>(Json->GetNumberField(TEXT("totalFound"))), Classes->Num());
				TestFalse(TEXT("Character: hasMore false"), Json->GetBoolField(TEXT("hasMore")));
			}
			{
				TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), TEXT("Character"));
				Args->SetNumberField(TEXT("maxResults"), 1);
				const TSharedPtr<FJsonObject> Page = ExpectOk(TEXT("search Character, one result"), TEXT("search-blueprint-classes"), Args);
				if (Page.IsValid() && Classes)
				{
					TestEqual(TEXT("one result"), Page->GetArrayField(TEXT("classes")).Num(), 1);
					TestEqual(TEXT("totalFound still counts every match"), static_cast<int32>(Page->GetNumberField(TEXT("totalFound"))), Classes->Num());
					TestTrue(TEXT("hasMore"), Page->GetBoolField(TEXT("hasMore")));
				}
			}
			{
				TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), TEXT("Actor"));
				Args->SetStringField(TEXT("classType"), TEXT("userWidget"));
				Args->SetNumberField(TEXT("maxResults"), 200);
				const TSharedPtr<FJsonObject> Widgets = ExpectOk(TEXT("search Actor, userWidget"), TEXT("search-blueprint-classes"), Args);
				const TArray<TSharedPtr<FJsonValue>>* WidgetClasses = nullptr;
				if (Widgets.IsValid() && Widgets->TryGetArrayField(TEXT("classes"), WidgetClasses))
				{
					TestFalse(TEXT("classType userWidget leaves the common Actor out"), FindClass(*WidgetClasses, TEXT("/Script/Engine.Actor")).IsValid());
				}
			}
			{
				TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), TEXT("Character"));
				Args->SetBoolField(TEXT("includeEngineClasses"), false);
				Args->SetNumberField(TEXT("maxResults"), 200);
				const TSharedPtr<FJsonObject> NoEngine = ExpectOk(TEXT("search Character, no engine classes"), TEXT("search-blueprint-classes"), Args);
				const TArray<TSharedPtr<FJsonValue>>* NoEngineClasses = nullptr;
				if (NoEngine.IsValid() && NoEngine->TryGetArrayField(TEXT("classes"), NoEngineClasses))
				{
					TestFalse(TEXT("includeEngineClasses false leaves the common Character out"), FindClass(*NoEngineClasses, TEXT("/Script/Engine.Character")).IsValid());
				}
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
