// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "Audio/SoundEffectPresetWidgetInterface.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Character.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

/**
 * list-blueprint-functions and list-overridable-functions (card 0289; review mcp-tools-bp-structure-16, -17, -18, -31,
 * imp-mcp-tools-bp-structure-8), run through tools/call on transient Blueprints by blueprintPath, so every case runs on
 * every engine. The interface is the engine's USoundEffectPresetWidgetInterface (UE 5.6-5.8): Blueprintable, with
 * GetClass (a return value, so a function graph in ImplementedInterfaces[].Graphs) and OnConstructed (an event), and
 * it extends UAudioPanelWidgetInterface.
 */
namespace N2CFunctionListsSpec
{
	/** The entries of a list result's "functions" array whose "name" is Name */
	TArray<TSharedPtr<FJsonObject>> Entries(const TSharedPtr<FJsonObject>& Json, const FString& Name)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Functions = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(TEXT("functions"), Functions))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Functions)
			{
				const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Entry.IsValid() && Entry->GetStringField(TEXT("name")) == Name)
				{
					Out.Add(Entry);
				}
			}
		}
		return Out;
	}

	FString Field(const TSharedPtr<FJsonObject>& Entry, const TCHAR* Name)
	{
		FString Value;
		if (Entry.IsValid())
		{
			Entry->TryGetStringField(Name, Value);
		}
		return Value;
	}

	bool Flag(const TSharedPtr<FJsonObject>& Entry, const TCHAR* Name)
	{
		bool bValue = false;
		return Entry.IsValid() && Entry->TryGetBoolField(Name, bValue) && bValue;
	}

	/** A Blueprint of ParentClass in Package (released with the FTestBlueprint that owns the package) */
	UBlueprint* MakeBlueprint(UClass* ParentClass, UPackage* Package, const FString& Name)
	{
		return ParentClass && Package ? FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()) : nullptr;
	}

	/** The Blueprint's ReceiveBeginPlay event node in its event graph, made (as the editor's default) when missing */
	UK2Node_Event* BeginPlay(UBlueprint* Blueprint)
	{
		UEdGraph* EventGraph = Blueprint && Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
		if (!EventGraph)
		{
			return nullptr;
		}
		const FName Name(TEXT("ReceiveBeginPlay")); // protected in AActor, so not GET_FUNCTION_NAME_CHECKED
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
			if (Event && Event->bOverrideFunction && Event->EventReference.GetMemberName() == Name)
			{
				return Event;
			}
		}
		int32 PosY = 0;
		return FKismetEditorUtilities::AddDefaultEventNode(Blueprint, EventGraph, Name, AActor::StaticClass(), PosY);
	}

	TSharedPtr<FJsonObject> Call(const FString& Tool, UBlueprint* Blueprint, bool bIncludeImplemented = true)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Blueprint ? Blueprint->GetPathName() : FString());
		if (Tool == TEXT("list-overridable-functions"))
		{
			Args->SetBoolField(TEXT("includeImplemented"), bIncludeImplemented);
		}
		return N2CTest::ResultJson(N2CTest::RunTool(Tool, Args));
	}
}

BEGIN_DEFINE_SPEC(FN2CMcpFunctionListsSpec, "NodeToCode.MCP.FunctionLists", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	/** Test's Actor Blueprint: a function, a custom event, BeginPlay in use, and the interface */
	UBlueprint* Base = nullptr;
	/** A child of Base that overrides BeginPlay too */
	UBlueprint* Child = nullptr;
END_DEFINE_SPEC(FN2CMcpFunctionListsSpec)

void FN2CMcpFunctionListsSpec::Define()
{
	using namespace N2CFunctionListsSpec;

	BeforeEach([this]()
	{
		Test = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CFnListBase"));
		Base = Test->GetBlueprint();
		if (!TestNotNull(TEXT("base Blueprint"), Base))
		{
			return;
		}
		Test->AddFunction(TEXT("MyFunc"));
		Test->AddCustomEvent(Test->EventGraph(), TEXT("OnOpened"));
		// BeginPlay in use (not the greyed-out default), so compiling Base gives its class a ReceiveBeginPlay stub
		UK2Node_Event* BaseBeginPlay = BeginPlay(Base);
		UK2Node_CallFunction* Print = Test->AddCall(Test->EventGraph(), UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		if (BaseBeginPlay)
		{
			BaseBeginPlay->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction=*/true);
			Test->Link(BaseBeginPlay, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
		}
		TestTrue(TEXT("Base implements the interface"), FBlueprintEditorUtils::ImplementNewInterface(Base,
			FTopLevelAssetPath(USoundEffectPresetWidgetInterface::StaticClass())));
		N2CTest::ExpectCompilesClean(*this, TEXT("Base"), Base);

		Child = MakeBlueprint(Base->GeneratedClass, Test->GetPackage(), TEXT("BP_N2CFnListChild"));
		if (TestNotNull(TEXT("child Blueprint"), Child))
		{
			TestNotNull(TEXT("child BeginPlay event"), BeginPlay(Child));
			N2CTest::ExpectCompilesClean(*this, TEXT("Child"), Child);
		}
		TestEqual(FString::Printf(TEXT("test Blueprint errors: %s"), *FString::Join(Test->Errors(), TEXT("; "))), Test->Errors().Num(), 0);
	});

	AfterEach([this]()
	{
		Base = nullptr;
		Child = nullptr;
		Test.Reset();
	});

	Describe(TEXT("list-blueprint-functions"), [this]()
	{
		It(TEXT("lists function graphs, interface function graphs, custom events and override events"), [this]()
		{
			const TSharedPtr<FJsonObject> Json = Call(TEXT("list-blueprint-functions"), Base);
			if (!TestNotNull(TEXT("result JSON"), Json.Get()))
			{
				return;
			}
			const TArray<TSharedPtr<FJsonObject>> MyFunc = Entries(Json, TEXT("MyFunc"));
			TestEqual(TEXT("MyFunc listed once"), MyFunc.Num(), 1);
			TestEqual(TEXT("MyFunc type"), Field(MyFunc.Num() ? MyFunc[0] : nullptr, TEXT("type")), FString(TEXT("Function")));

			const TArray<TSharedPtr<FJsonObject>> OnOpened = Entries(Json, TEXT("OnOpened"));
			if (TestEqual(TEXT("custom event OnOpened listed once"), OnOpened.Num(), 1))
			{
				TestEqual(TEXT("OnOpened type"), Field(OnOpened[0], TEXT("type")), FString(TEXT("CustomEvent")));
				TestTrue(TEXT("OnOpened enabled"), Flag(OnOpened[0], TEXT("isEnabled")));
				// The node's GUID, the same on every call (it used to be a new random GUID)
				const TArray<TSharedPtr<FJsonObject>> Again = Entries(Call(TEXT("list-blueprint-functions"), Base), TEXT("OnOpened"));
				TestEqual(TEXT("OnOpened guid stable"), Field(Again.Num() ? Again[0] : nullptr, TEXT("guid")), Field(OnOpened[0], TEXT("guid")));
			}

			const TArray<TSharedPtr<FJsonObject>> BeginPlayEntries = Entries(Json, TEXT("ReceiveBeginPlay"));
			if (TestEqual(TEXT("override event ReceiveBeginPlay listed once"), BeginPlayEntries.Num(), 1))
			{
				TestEqual(TEXT("ReceiveBeginPlay type"), Field(BeginPlayEntries[0], TEXT("type")), FString(TEXT("Event")));
				TestEqual(TEXT("ReceiveBeginPlay declaredIn"), Field(BeginPlayEntries[0], TEXT("declaredIn")), FString(TEXT("Actor")));
				TestTrue(TEXT("ReceiveBeginPlay enabled"), Flag(BeginPlayEntries[0], TEXT("isEnabled")));
			}

			const TArray<TSharedPtr<FJsonObject>> GetClassEntries = Entries(Json, TEXT("GetClass"));
			if (TestEqual(TEXT("interface function graph GetClass listed once"), GetClassEntries.Num(), 1))
			{
				TestEqual(TEXT("GetClass type"), Field(GetClassEntries[0], TEXT("type")), FString(TEXT("InterfaceFunction")));
				TestEqual(TEXT("GetClass interface"), Field(GetClassEntries[0], TEXT("interface")), FString(TEXT("SoundEffectPresetWidgetInterface")));
			}
		});
	});

	Describe(TEXT("list-overridable-functions"), [this]()
	{
		It(TEXT("reports ReceiveBeginPlay implemented in a child of a Blueprint that also overrides it"), [this]()
		{
			const TArray<TSharedPtr<FJsonObject>> Found = Entries(Call(TEXT("list-overridable-functions"), Child), TEXT("ReceiveBeginPlay"));
			if (TestEqual(TEXT("listed once with includeImplemented"), Found.Num(), 1))
			{
				TestTrue(TEXT("isImplemented"), Flag(Found[0], TEXT("isImplemented")));
				TestEqual(TEXT("sourceName is the declaring class"), Field(Found[0], TEXT("sourceName")), FString(TEXT("Actor")));
				TestEqual(TEXT("functionType"), Field(Found[0], TEXT("functionType")), FString(TEXT("Event")));
			}
			TestEqual(TEXT("not offered without includeImplemented"),
				Entries(Call(TEXT("list-overridable-functions"), Child, false), TEXT("ReceiveBeginPlay")).Num(), 0);
		});

		It(TEXT("counts an interface function graph as implemented and types it as a function"), [this]()
		{
			const TSharedPtr<FJsonObject> Json = Call(TEXT("list-overridable-functions"), Base);
			const TArray<TSharedPtr<FJsonObject>> GetClassEntries = Entries(Json, TEXT("GetClass"));
			if (TestEqual(TEXT("GetClass listed once"), GetClassEntries.Num(), 1))
			{
				TestTrue(TEXT("GetClass isImplemented"), Flag(GetClassEntries[0], TEXT("isImplemented")));
				TestEqual(TEXT("GetClass sourceType"), Field(GetClassEntries[0], TEXT("sourceType")), FString(TEXT("Interface")));
				TestEqual(TEXT("GetClass functionType (has a return value)"), Field(GetClassEntries[0], TEXT("functionType")), FString(TEXT("Function")));
			}
			const TArray<TSharedPtr<FJsonObject>> OnConstructed = Entries(Json, TEXT("OnConstructed"));
			if (TestEqual(TEXT("OnConstructed listed once"), OnConstructed.Num(), 1))
			{
				TestFalse(TEXT("OnConstructed isImplemented"), Flag(OnConstructed[0], TEXT("isImplemented")));
				TestEqual(TEXT("OnConstructed functionType"), Field(OnConstructed[0], TEXT("functionType")), FString(TEXT("Event")));
			}
		});

		It(TEXT("lists the interfaces a parent class implements"), [this]()
		{
			const TArray<TSharedPtr<FJsonObject>> OnConstructed = Entries(Call(TEXT("list-overridable-functions"), Child), TEXT("OnConstructed"));
			if (TestEqual(TEXT("the parent's interface event OnConstructed listed once"), OnConstructed.Num(), 1))
			{
				TestEqual(TEXT("sourceType"), Field(OnConstructed[0], TEXT("sourceType")), FString(TEXT("Interface")));
				TestEqual(TEXT("sourceName"), Field(OnConstructed[0], TEXT("sourceName")), FString(TEXT("SoundEffectPresetWidgetInterface")));
			}
		});

		It(TEXT("types functionType by placement, not BlueprintCallable, and reports isCallable apart"), [this]()
		{
			// ACharacter::CanJumpInternal: BlueprintNativeEvent, const, returns bool, not BlueprintCallable
			UBlueprint* CharacterBlueprint = MakeBlueprint(ACharacter::StaticClass(), Test->GetPackage(), TEXT("BP_N2CFnListCharacter"));
			if (!TestNotNull(TEXT("character Blueprint"), CharacterBlueprint))
			{
				return;
			}
			N2CTest::ExpectCompilesClean(*this, TEXT("Character Blueprint"), CharacterBlueprint);
			const TArray<TSharedPtr<FJsonObject>> CanJump = Entries(Call(TEXT("list-overridable-functions"), CharacterBlueprint), TEXT("CanJumpInternal"));
			if (TestEqual(TEXT("CanJumpInternal listed once"), CanJump.Num(), 1))
			{
				TestEqual(TEXT("functionType"), Field(CanJump[0], TEXT("functionType")), FString(TEXT("Function")));
				TestFalse(TEXT("isCallable"), Flag(CanJump[0], TEXT("isCallable")));
			}
		});

		It(TEXT("leaves out BlueprintInternalUseOnly events, as the editor does"), [this]()
		{
			// AActor::UserConstructionScript is BlueprintImplementableEvent with BlueprintInternalUseOnly
			TestEqual(TEXT("UserConstructionScript not offered"),
				Entries(Call(TEXT("list-overridable-functions"), Base), TEXT("UserConstructionScript")).Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
