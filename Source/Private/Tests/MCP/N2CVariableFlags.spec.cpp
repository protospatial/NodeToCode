// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateVariableTool.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateLocalVariableTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CVariableFlagsSpec
{
	using FSettings = FN2CMcpCreateVariableTool::FVariableCreationSettings;

	const FBPVariableDescription* FindMember(const UBlueprint* Blueprint, const FName Name)
	{
		const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
		return Index == INDEX_NONE ? nullptr : &Blueprint->NewVariables[Index];
	}

	const FProperty* CompiledProperty(const UBlueprint* Blueprint, const FName Name)
	{
		return Blueprint->GeneratedClass ? FindFProperty<FProperty>(Blueprint->GeneratedClass, Name) : nullptr;
	}

	const FBPVariableDescription* FindLocal(const UK2Node_FunctionEntry* Entry, const FName Name)
	{
		return Entry ? Entry->LocalVariables.FindByPredicate([Name](const FBPVariableDescription& Var) { return Var.VarName == Name; }) : nullptr;
	}

	bool HasFunctionGraph(const UBlueprint* Blueprint, const FName Name)
	{
		return Blueprint->FunctionGraphs.ContainsByPredicate([Name](const UEdGraph* Graph) { return Graph && Graph->GetFName() == Name; });
	}

	void Recompile(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

	/** One tool call: the edit inside an FN2CMcpToolCallScope, compiled once when it ends (as ExecuteOnGameThread does) */
	template <typename FEdit>
	TArray<FN2CCompileDiagnostics> InToolCall(FEdit&& Edit)
	{
		FN2CMcpToolCallScope CallScope;
		Edit();
		return CallScope.Flush();
	}
}

/**
 * create-variable and create-local-variable set every flag on the variable description, in one undo step (card 0154).
 * The member cases call FN2CMcpCreateVariableTool::CreateMemberVariable and the local ones
 * FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction (what the tools run) inside a tool-call scope, so they run
 * on every engine; the last cases call the MCP tools on a focused Blueprint editor and run on UE 5.8 only (a headless
 * Blueprint editor cannot be closed below 5.8, see NodeToCode.MCP.CompileOnce).
 */
BEGIN_DEFINE_SPEC(FN2CVariableFlagsSpec, "NodeToCode.MCP.VariableFlags", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	bool bEditorOpen = false;

	/** A /Game Actor Blueprint, compiled, package not transient (so MarkPackageDirty applies) and clean */
	UBlueprint* MakeBlueprint(const TCHAR* Name);
END_DEFINE_SPEC(FN2CVariableFlagsSpec)

UBlueprint* FN2CVariableFlagsSpec::MakeBlueprint(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FTestBlueprint>(Name, TEXT("/Game"));
	UBlueprint* Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return nullptr;
	}
	// FTestBlueprint packages are transient, and MarkPackageDirty skips transient objects: clear the flag so the
	// dirty checks see what a saved Blueprint would (AfterEach puts it back)
	Test->GetPackage()->ClearFlags(RF_Transient);
	N2CVariableFlagsSpec::Recompile(Blueprint);
	Test->GetPackage()->SetDirtyFlag(false);
	return Blueprint;
}

void FN2CVariableFlagsSpec::Define()
{
	using namespace N2CVariableFlagsSpec;

	AfterEach([this]()
	{
		if (Test.IsValid())
		{
			if (bEditorOpen && GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Test->GetBlueprint());
			}
			Test->GetPackage()->SetDirtyFlag(false);
			Test->GetPackage()->SetFlags(RF_Transient);
		}
		// The undo buffer holds the test Blueprint's objects: drop them so the Blueprint can be collected
		if (GEditor && GEditor->Trans)
		{
			GEditor->ResetTransaction(NSLOCTEXT("NodeToCode", "N2CVariableFlagsReset", "NodeToCode variable flag tests"));
		}
		bEditorOpen = false;
		Test.Reset();
	});

	Describe(TEXT("create-variable"), [this]()
	{
		It(TEXT("keeps a replicated, read-only, instance-editable float's flags on the description and after two compiles"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarFlags"));
			if (!Blueprint)
			{
				return;
			}
			FSettings Settings;
			Settings.VariableName = TEXT("Health");
			Settings.bInstanceEditable = true;
			Settings.bBlueprintReadOnly = true;
			Settings.ReplicationCondition = TEXT("replicated");
			Settings.Category = TEXT("Stats");
			Settings.Tooltip = TEXT("Hit points left");
			Settings.DefaultValue = TEXT("100.0");

			FName Name;
			FString Error;
			const TArray<FN2CCompileDiagnostics> Compiles = InToolCall([&]()
			{
				Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, N2CTest::FTestBlueprint::DoublePinType(), Error);
			});
			if (!TestEqual(TEXT("created as Health: ") + Error, Name, FName(TEXT("Health"))))
			{
				return;
			}
			TestEqual(TEXT("one compile for the call"), Compiles.Num(), 1);
			if (Compiles.Num() == 1)
			{
				TestTrue(TEXT("the Blueprint compiles"), Compiles[0].bSuccess);
			}
			TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());

			const FBPVariableDescription* Var = FindMember(Blueprint, Name);
			if (!TestNotNull(TEXT("variable description"), Var))
			{
				return;
			}
			TestTrue(TEXT("description: CPF_Net"), (Var->PropertyFlags & CPF_Net) != 0);
			TestTrue(TEXT("description: CPF_BlueprintReadOnly"), (Var->PropertyFlags & CPF_BlueprintReadOnly) != 0);
			TestTrue(TEXT("description: CPF_Edit"), (Var->PropertyFlags & CPF_Edit) != 0);
			TestFalse(TEXT("description: no CPF_DisableEditOnInstance (instance editable)"), (Var->PropertyFlags & CPF_DisableEditOnInstance) != 0);
			TestFalse(TEXT("description: no CPF_Interp (not exposed to cinematics)"), (Var->PropertyFlags & CPF_Interp) != 0);
			TestFalse(TEXT("description: no CPF_RepNotify"), (Var->PropertyFlags & CPF_RepNotify) != 0);
			TestEqual(TEXT("description: category"), Var->Category.ToString(), FString(TEXT("Stats")));
			TestEqual(TEXT("description: tooltip"), Var->GetMetaData(FBlueprintMetadata::MD_Tooltip), FString(TEXT("Hit points left")));
			// The compile moves the default value to the class default object (and clears it on the description)
			if (const FDoubleProperty* Double = CastField<FDoubleProperty>(CompiledProperty(Blueprint, Name)))
			{
				TestEqual(TEXT("default value on the class defaults"),
					Double->GetPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject()), 100.0);
			}
			else
			{
				AddError(TEXT("Health did not compile to a double property"));
			}

			for (int32 Pass = 1; Pass <= 2; ++Pass)
			{
				if (Pass == 2)
				{
					Recompile(Blueprint);
				}
				const FProperty* Property = CompiledProperty(Blueprint, Name);
				const FString When = Pass == 1 ? TEXT("after the call's compile") : TEXT("after a second compile");
				if (!TestNotNull(TEXT("compiled property ") + When, Property))
				{
					return;
				}
				TestTrue(TEXT("CPF_Net ") + When, Property->HasAnyPropertyFlags(CPF_Net));
				TestTrue(TEXT("CPF_BlueprintReadOnly ") + When, Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
				TestTrue(TEXT("CPF_Edit ") + When, Property->HasAnyPropertyFlags(CPF_Edit));
				TestFalse(TEXT("no CPF_DisableEditOnInstance ") + When, Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
				TestFalse(TEXT("no CPF_Interp ") + When, Property->HasAnyPropertyFlags(CPF_Interp));
			}
		});

		It(TEXT("leaves a variable with no flags editable on the class defaults only, unreplicated and not exposed to cinematics"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarDefaults"));
			if (!Blueprint)
			{
				return;
			}
			FSettings Settings;
			Settings.VariableName = TEXT("Plain");
			FName Name;
			FString Error;
			InToolCall([&]() { Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, N2CTest::FTestBlueprint::IntPinType(), Error); });
			const FBPVariableDescription* Var = FindMember(Blueprint, Name);
			if (!TestNotNull(TEXT("created: ") + Error, Var))
			{
				return;
			}
			TestTrue(TEXT("CPF_DisableEditOnInstance (the editor's default)"), (Var->PropertyFlags & CPF_DisableEditOnInstance) != 0);
			TestFalse(TEXT("no CPF_Interp"), (Var->PropertyFlags & CPF_Interp) != 0);
			TestFalse(TEXT("no CPF_Net"), (Var->PropertyFlags & CPF_Net) != 0);
			TestFalse(TEXT("no CPF_BlueprintReadOnly"), (Var->PropertyFlags & CPF_BlueprintReadOnly) != 0);
			TestEqual(TEXT("default category"), Var->Category.ToString(), UEdGraphSchema_K2::VR_DefaultCategory.ToString());
			const FProperty* Property = CompiledProperty(Blueprint, Name);
			if (TestNotNull(TEXT("compiled property"), Property))
			{
				TestTrue(TEXT("compiled: CPF_DisableEditOnInstance"), Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
				TestFalse(TEXT("compiled: no CPF_Interp"), Property->HasAnyPropertyFlags(CPF_Interp));
			}
		});

		It(TEXT("gives a repnotify variable CPF_RepNotify, its OnRep function and the compiled RepNotifyFunc"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarRepNotify"));
			if (!Blueprint)
			{
				return;
			}
			FSettings Settings;
			Settings.VariableName = TEXT("Ammo");
			Settings.ReplicationCondition = TEXT("repnotify");
			FName Name;
			FString Error;
			const TArray<FN2CCompileDiagnostics> Compiles = InToolCall([&]()
			{
				Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, N2CTest::FTestBlueprint::IntPinType(), Error);
			});
			const FBPVariableDescription* Var = FindMember(Blueprint, Name);
			if (!TestNotNull(TEXT("created: ") + Error, Var))
			{
				return;
			}
			if (Compiles.Num() == 1)
			{
				TestTrue(TEXT("the Blueprint compiles"), Compiles[0].bSuccess);
			}
			const FName OnRep(TEXT("OnRep_Ammo"));
			TestTrue(TEXT("description: CPF_Net"), (Var->PropertyFlags & CPF_Net) != 0);
			TestTrue(TEXT("description: CPF_RepNotify"), (Var->PropertyFlags & CPF_RepNotify) != 0);
			TestEqual(TEXT("description: RepNotifyFunc"), Var->RepNotifyFunc, OnRep);
			TestTrue(TEXT("OnRep_Ammo function graph exists"), HasFunctionGraph(Blueprint, OnRep));
			Recompile(Blueprint);
			const FProperty* Property = CompiledProperty(Blueprint, Name);
			if (TestNotNull(TEXT("compiled property"), Property))
			{
				TestTrue(TEXT("compiled: CPF_Net"), Property->HasAnyPropertyFlags(CPF_Net));
				TestTrue(TEXT("compiled: CPF_RepNotify"), Property->HasAnyPropertyFlags(CPF_RepNotify));
				TestEqual(TEXT("compiled: RepNotifyFunc"), Property->RepNotifyFunc, OnRep);
			}
			TestNotNull(TEXT("compiled OnRep_Ammo function"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->FindFunctionByName(OnRep) : nullptr);
		});

		It(TEXT("is undone in one step, flags and OnRep function included, leaving the previous transaction alone"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarUndo"));
			if (!Blueprint || !TestTrue(TEXT("an editor transaction buffer"), GEditor && GEditor->Trans))
			{
				return;
			}
			// An earlier, unrelated user edit
			{
				const FScopedTransaction Earlier(NSLOCTEXT("NodeToCode", "N2CVariableFlagsEarlier", "Earlier edit (test)"));
				Blueprint->Modify();
				Blueprint->BlueprintDescription = TEXT("Edited before the variable");
			}
			const int32 QueueBefore = GEditor->Trans->GetQueueLength();

			FSettings Settings;
			Settings.VariableName = TEXT("Shield");
			Settings.bInstanceEditable = true;
			Settings.bBlueprintReadOnly = true;
			Settings.ReplicationCondition = TEXT("repnotify");
			Settings.Category = TEXT("Stats");
			FName Name;
			FString Error;
			InToolCall([&]() { Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, N2CTest::FTestBlueprint::DoublePinType(), Error); });
			if (!TestNotNull(TEXT("created: ") + Error, FindMember(Blueprint, Name)))
			{
				return;
			}
			TestEqual(TEXT("one transaction recorded for the call"), GEditor->Trans->GetQueueLength(), QueueBefore + 1);
			TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());

			TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
			TestNull(TEXT("one undo removes the variable"), FindMember(Blueprint, Name));
			TestFalse(TEXT("one undo removes the OnRep function"), HasFunctionGraph(Blueprint, FName(TEXT("OnRep_Shield"))));
			TestEqual(TEXT("the earlier edit is still there"), Blueprint->BlueprintDescription, FString(TEXT("Edited before the variable")));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			Recompile(Blueprint);
			TestNull(TEXT("no compiled property after undo and compile"), CompiledProperty(Blueprint, Name));

			TestTrue(TEXT("redo runs"), GEditor->RedoTransaction());
			const FBPVariableDescription* Redone = FindMember(Blueprint, Name);
			if (TestNotNull(TEXT("redo brings the variable back"), Redone))
			{
				TestTrue(TEXT("redo keeps CPF_Net and CPF_BlueprintReadOnly"),
					(Redone->PropertyFlags & (CPF_Net | CPF_BlueprintReadOnly)) == (CPF_Net | CPF_BlueprintReadOnly));
				TestFalse(TEXT("redo keeps it instance editable"), (Redone->PropertyFlags & CPF_DisableEditOnInstance) != 0);
			}
		});

		It(TEXT("refuses Blueprint Interfaces, Function Libraries and Macro Libraries and changes nothing"), [this]()
		{
			UBlueprint* Owner = MakeBlueprint(TEXT("BP_N2CVarOwner"));
			if (!Owner)
			{
				return;
			}
			struct FCase { const TCHAR* Name; UClass* Parent; EBlueprintType Type; const TCHAR* Kind; };
			const FCase Cases[] = {
				{ TEXT("BPI_N2CVarRefused"), UInterface::StaticClass(), BPTYPE_Interface, TEXT("Blueprint Interface") },
				{ TEXT("BPFL_N2CVarRefused"), UBlueprintFunctionLibrary::StaticClass(), BPTYPE_FunctionLibrary, TEXT("Function Library") },
				{ TEXT("BPML_N2CVarRefused"), AActor::StaticClass(), BPTYPE_MacroLibrary, TEXT("Macro Library") },
			};
			for (const FCase& Case : Cases)
			{
				UBlueprint* Refused = FKismetEditorUtilities::CreateBlueprint(Case.Parent, Test->GetPackage(), FName(Case.Name), Case.Type,
					UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
				if (!TestNotNull(FString::Printf(TEXT("%s created"), Case.Name), Refused))
				{
					continue;
				}
				FSettings Settings;
				Settings.VariableName = TEXT("State");
				FString Error;
				FName Name;
				InToolCall([&]() { Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Refused, Settings, N2CTest::FTestBlueprint::IntPinType(), Error); });
				TestTrue(FString::Printf(TEXT("%s refused"), Case.Name), Name.IsNone());
				TestTrue(FString::Printf(TEXT("%s: error names the code and kind: %s"), Case.Name, *Error),
					Error.StartsWith(TEXT("UNSUPPORTED_BLUEPRINT_TYPE:")) && Error.Contains(Case.Kind));
				TestEqual(FString::Printf(TEXT("%s: no variable added"), Case.Name), Refused->NewVariables.Num(), 0);
			}
		});

		It(TEXT("refuses an unknown replicationCondition before changing anything"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarBadRep"));
			if (!Blueprint)
			{
				return;
			}
			FSettings Settings;
			Settings.VariableName = TEXT("Bad");
			Settings.ReplicationCondition = TEXT("always");
			FString Error;
			FName Name;
			InToolCall([&]() { Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, N2CTest::FTestBlueprint::IntPinType(), Error); });
			TestTrue(TEXT("refused: ") + Error, Name.IsNone() && Error.StartsWith(TEXT("INVALID_ARGUMENT:")));
			TestEqual(TEXT("no variable added"), Blueprint->NewVariables.Num(), 0);
		});
	});

	Describe(TEXT("create-local-variable"), [this]()
	{
		It(TEXT("renames a local named like a parameter, and the function compiles"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CLocalParam"));
			if (!Blueprint)
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CTakesTarget")));
			Test->AddFunctionInput(Function, FName(TEXT("Target")), N2CTest::FTestBlueprint::IntPinType());
			UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
			if (!TestTrue(TEXT("function built: ") + FString::Join(Test->Errors(), TEXT("; ")), Entry != nullptr && Test->Errors().Num() == 0))
			{
				return;
			}
			Recompile(Blueprint);
			Test->GetPackage()->SetDirtyFlag(false);

			FName Name;
			FString Error;
			const TArray<FN2CCompileDiagnostics> Compiles = InToolCall([&]()
			{
				Name = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Target"),
					N2CTest::FTestBlueprint::IntPinType(), TEXT("3"), TEXT("A local tooltip"), Error);
			});
			if (!TestFalse(TEXT("created: ") + Error, Name.IsNone()))
			{
				return;
			}
			TestNotEqual(TEXT("renamed away from the parameter"), Name, FName(TEXT("Target")));
			AddInfo(TEXT("local named 'Target' was created as ") + Name.ToString());
			if (TestEqual(TEXT("one compile"), Compiles.Num(), 1))
			{
				TestTrue(TEXT("the function compiles"), Compiles[0].bSuccess);
				TestEqual(TEXT("no compile errors"), Compiles[0].ErrorCount, 0);
			}
			TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());
			const FBPVariableDescription* Local = FindLocal(Entry, Name);
			if (TestNotNull(TEXT("local description"), Local))
			{
				TestTrue(TEXT("CPF_BlueprintVisible, as AddLocalVariable sets"), (Local->PropertyFlags & CPF_BlueprintVisible) != 0);
				TestEqual(TEXT("default value"), Local->DefaultValue, FString(TEXT("3")));
				TestEqual(TEXT("tooltip"), Local->GetMetaData(FBlueprintMetadata::MD_Tooltip), FString(TEXT("A local tooltip")));
				TestEqual(TEXT("category"), Local->Category.ToString(), UEdGraphSchema_K2::VR_DefaultCategory.ToString());
			}
		});

		It(TEXT("is undone in one step, leaving the previous transaction alone"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CLocalUndo"));
			if (!Blueprint || !TestTrue(TEXT("an editor transaction buffer"), GEditor && GEditor->Trans))
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CLocalUndoFn")));
			UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
			if (!TestNotNull(TEXT("function entry"), Entry))
			{
				return;
			}
			Recompile(Blueprint);
			{
				const FScopedTransaction Earlier(NSLOCTEXT("NodeToCode", "N2CVariableFlagsEarlierLocal", "Earlier edit (test)"));
				Blueprint->Modify();
				Blueprint->BlueprintDescription = TEXT("Edited before the local");
			}
			const int32 QueueBefore = GEditor->Trans->GetQueueLength();
			FName Name;
			FString Error;
			InToolCall([&]()
			{
				Name = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Counter"),
					N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
			});
			if (!TestNotNull(TEXT("created: ") + Error, FindLocal(Entry, Name)))
			{
				return;
			}
			TestEqual(TEXT("one transaction recorded for the call"), GEditor->Trans->GetQueueLength(), QueueBefore + 1);
			TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
			TestNull(TEXT("one undo removes the local"), FindLocal(Entry, Name));
			TestEqual(TEXT("the earlier edit is still there"), Blueprint->BlueprintDescription, FString(TEXT("Edited before the local")));
		});

		It(TEXT("refuses an event graph"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CLocalEventGraph"));
			if (!Blueprint)
			{
				return;
			}
			FString Error;
			FName Name;
			InToolCall([&]()
			{
				Name = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Test->EventGraph(), TEXT("Nope"),
					N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
			});
			TestTrue(TEXT("refused: ") + Error, Name.IsNone() && Error.StartsWith(TEXT("NOT_A_FUNCTION_GRAPH:")));
		});
	});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	Describe(TEXT("through the MCP tools on a focused Blueprint (UE 5.8)"), [this]()
	{
		It(TEXT("create-variable defaults to not instance editable, and create-local-variable targets the focused function"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarTools"));
			if (!Blueprint)
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CToolFn")));
			Test->AddFunctionInput(Function, FName(TEXT("Amount")), N2CTest::FTestBlueprint::IntPinType());
			Recompile(Blueprint);
			TSharedPtr<IBlueprintEditor> Opened;
			FString OpenError;
			if (!TestTrue(TEXT("Blueprint editor opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, Opened, OpenError)))
			{
				return;
			}
			bEditorOpen = true;
			StaticCastSharedPtr<FBlueprintEditor>(Opened)->JumpToHyperlink(Function, false);

			const TSharedPtr<IN2CMcpTool> CreateVariable = N2CTest::FindRegisteredTool(TEXT("create-variable"));
			const TSharedPtr<IN2CMcpTool> CreateLocal = N2CTest::FindRegisteredTool(TEXT("create-local-variable"));
			if (!TestTrue(TEXT("both tools registered"), CreateVariable.IsValid() && CreateLocal.IsValid()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> InstanceDefault = CreateVariable->GetDefinition().InputSchema->GetObjectField(TEXT("properties"))
				->GetObjectField(TEXT("isInstanceEditable"));
			TestFalse(TEXT("schema default of isInstanceEditable is false"), InstanceDefault->GetBoolField(TEXT("default")));

			const TSharedPtr<FJsonObject> VarArgs = MakeShared<FJsonObject>();
			VarArgs->SetStringField(TEXT("variableName"), TEXT("Speed"));
			VarArgs->SetStringField(TEXT("typeIdentifier"), TEXT("float"));
			VarArgs->SetStringField(TEXT("replicationCondition"), TEXT("replicated"));
			const FMcpToolCallResult VarResult = CreateVariable->Execute(VarArgs);
			TestFalse(TEXT("create-variable succeeds: ") + N2CTest::ResultText(VarResult).Left(300), VarResult.bIsError);
			const FBPVariableDescription* Speed = FindMember(Blueprint, FName(TEXT("Speed")));
			if (TestNotNull(TEXT("Speed created"), Speed))
			{
				TestTrue(TEXT("not instance editable by default"), (Speed->PropertyFlags & CPF_DisableEditOnInstance) != 0);
				TestFalse(TEXT("no CPF_Interp"), (Speed->PropertyFlags & CPF_Interp) != 0);
				TestTrue(TEXT("CPF_Net"), (Speed->PropertyFlags & CPF_Net) != 0);
			}
			const FProperty* SpeedProperty = CompiledProperty(Blueprint, FName(TEXT("Speed")));
			TestTrue(TEXT("compiled Speed is replicated"), SpeedProperty && SpeedProperty->HasAnyPropertyFlags(CPF_Net));
			const TSharedPtr<FJsonObject> VarJson = N2CTest::ParseJsonObject(N2CTest::ResultText(VarResult));
			TestTrue(TEXT("the result carries the compile"), VarJson.IsValid() && VarJson->HasField(TEXT("compile")));

			const TSharedPtr<FJsonObject> LocalArgs = MakeShared<FJsonObject>();
			LocalArgs->SetStringField(TEXT("variableName"), TEXT("Amount"));
			LocalArgs->SetStringField(TEXT("typeIdentifier"), TEXT("int"));
			const FMcpToolCallResult LocalResult = CreateLocal->Execute(LocalArgs);
			TestFalse(TEXT("create-local-variable succeeds: ") + N2CTest::ResultText(LocalResult).Left(300), LocalResult.bIsError);
			const TSharedPtr<FJsonObject> LocalJson = N2CTest::ParseJsonObject(N2CTest::ResultText(LocalResult));
			if (TestTrue(TEXT("local result is JSON"), LocalJson.IsValid()))
			{
				const FString Actual = LocalJson->GetStringField(TEXT("actualName"));
				TestNotEqual(TEXT("the local is not named like the parameter"), Actual, FString(TEXT("Amount")));
				TestEqual(TEXT("functionName"), LocalJson->GetStringField(TEXT("functionName")), FString(TEXT("N2CToolFn")));
				TestNotNull(TEXT("the local is in the focused function"), FindLocal(Test->FunctionEntry(Function), FName(*Actual)));
				const TSharedPtr<FJsonObject>* Compile = nullptr;
				if (TestTrue(TEXT("the result carries the compile"), LocalJson->TryGetObjectField(TEXT("compile"), Compile)))
				{
					TestTrue(TEXT("the function compiles"), (*Compile)->GetBoolField(TEXT("success")));
				}
			}
		});
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
