// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateLocalVariableTool.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateVariableTool.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpGetBlueprintMemberVariablesTool.h"
#include "MCP/Utils/N2CMcpVariableDefaults.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"

// get-blueprint-member-variables' description of one variable (private in the tool)
N2C_TEST_PRIVATE_MEMBER(FN2CVariableDefaultsBuildVariableInfo,
	TSharedPtr<FJsonObject> (FN2CMcpGetBlueprintMemberVariablesTool::*)(const UBlueprint*, const FBPVariableDescription&) const,
	&FN2CMcpGetBlueprintMemberVariablesTool::BuildVariableInfo)

namespace N2CVariableDefaultsSpec
{
	FEdGraphPinType FloatPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Real;
		Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		return Type;
	}

	FEdGraphPinType NamePinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Name;
		return Type;
	}

	void Recompile(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

	/** The member's value on the class defaults as a number (bool, byte, int and real properties), or NaN */
	double DefaultNumber(const UBlueprint* Blueprint, const FName Name)
	{
		const FProperty* Property = FN2CMcpVariableDefaults::FindMemberProperty(Blueprint, Name);
		const UObject* Defaults = Property ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
		if (!Defaults)
		{
			return TNumericLimits<double>::Lowest();
		}
		const void* Value = Property->ContainerPtrToValuePtr<void>(Defaults);
		if (const FBoolProperty* Bool = CastField<const FBoolProperty>(Property))
		{
			return Bool->GetPropertyValue(Value) ? 1.0 : 0.0;
		}
		if (const FNumericProperty* Numeric = CastField<const FNumericProperty>(Property))
		{
			return Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value) : static_cast<double>(Numeric->GetSignedIntPropertyValue(Value));
		}
		if (const FEnumProperty* Enum = CastField<const FEnumProperty>(Property))
		{
			return static_cast<double>(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
		}
		return TNumericLimits<double>::Lowest();
	}

	FString DefaultText(const UBlueprint* Blueprint, const FName Name)
	{
		FString Text;
		FN2CMcpVariableDefaults::ReadMemberDefault(Blueprint, Name, Text);
		return Text;
	}

	const FBPVariableDescription* FindMember(const UBlueprint* Blueprint, const FName Name)
	{
		const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
		return Index == INDEX_NONE ? nullptr : &Blueprint->NewVariables[Index];
	}

	TSharedPtr<FJsonObject> DescribeVariable(const UBlueprint* Blueprint, const FName Name)
	{
		const TSharedPtr<FN2CMcpGetBlueprintMemberVariablesTool> Reader =
			StaticCastSharedPtr<FN2CMcpGetBlueprintMemberVariablesTool>(N2CTest::FindRegisteredTool(TEXT("get-blueprint-member-variables")));
		const FBPVariableDescription* Var = FindMember(Blueprint, Name);
		if (!Reader.IsValid() || !Var)
		{
			return nullptr;
		}
		return ((*Reader).*N2CTest::GetPrivateMember(N2CTest::FN2CVariableDefaultsBuildVariableInfo()))(Blueprint, *Var);
	}
}

/**
 * Member and local variable defaults read and written where the editor keeps them (card 0283; review
 * mcp-tools-variables-8, -9, -10, -25, imp-mcp-tools-variables-6). set-member-variable-default-value writes the class
 * defaults (CDO) in one undo step without compiling, parses the value as the variable's type (a refused value changes
 * nothing), takes an enum entry by name, display name or index, and resets on an empty value; the old value is read
 * from the CDO. get-blueprint-member-variables reads the default from the CDO, Instance Editable and Private as the
 * details panel shows them. set-local-function-variable-default-value parses against the compiled local. The type
 * description (float and double, a map's key and value) is the read-back table of NodeToCode.MCP.Variables.
 */
BEGIN_DEFINE_SPEC(FN2CVariableDefaultsSpec, "NodeToCode.MCP.VariableDefaults", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;

	/** A scratch Blueprint with the given member variables, compiled and clean */
	bool Build(const TCHAR* Name, const TArray<TPair<FName, FEdGraphPinType>>& Members);
	/** SetMemberDefault inside one tool call; the compiles that call ran */
	TArray<FN2CCompileDiagnostics> Set(const FName Name, const FString& Value, bool& bOutOk, FN2CMcpVariableDefaults::FMemberResult& OutResult, FString& OutError);
	/** SetMemberDefault must refuse Value, naming the reason, and change nothing */
	void ExpectRefused(const FName Name, const FString& Value, const FString& ReasonPart);
END_DEFINE_SPEC(FN2CVariableDefaultsSpec)

bool FN2CVariableDefaultsSpec::Build(const TCHAR* Name, const TArray<TPair<FName, FEdGraphPinType>>& Members)
{
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	for (const TPair<FName, FEdGraphPinType>& Member : Members)
	{
		if (!TestTrue(TEXT("added ") + Member.Key.ToString(), Test->AddMemberVariable(Member.Key, Member.Value)))
		{
			return false;
		}
	}
	Test->Settle();
	return true;
}

TArray<FN2CCompileDiagnostics> FN2CVariableDefaultsSpec::Set(const FName Name, const FString& Value, bool& bOutOk,
	FN2CMcpVariableDefaults::FMemberResult& OutResult, FString& OutError)
{
	return N2CTest::InToolCall([&]()
	{
		bOutOk = FN2CMcpVariableDefaults::SetMemberDefault(Blueprint, Name, Value, OutResult, OutError);
	});
}

void FN2CVariableDefaultsSpec::ExpectRefused(const FName Name, const FString& Value, const FString& ReasonPart)
{
	using namespace N2CVariableDefaultsSpec;
	const FString Before = DefaultText(Blueprint, Name);
	const int32 UndoBefore = N2CTest::UndoQueueLength();
	bool bOk = true;
	FN2CMcpVariableDefaults::FMemberResult Result;
	FString Error;
	const TArray<FN2CCompileDiagnostics> Compiles = Set(Name, Value, bOk, Result, Error);
	const FString What = FString::Printf(TEXT("%s = '%s'"), *Name.ToString(), *Value);
	TestFalse(What + TEXT(" is refused"), bOk);
	TestTrue(FString::Printf(TEXT("%s: the reason (%s) says '%s'"), *What, *Error, *ReasonPart), Error.Contains(ReasonPart));
	TestEqual(What + TEXT(": the class defaults are unchanged"), DefaultText(Blueprint, Name), Before);
	TestEqual(What + TEXT(": no undo step"), N2CTest::UndoQueueLength(), UndoBefore);
	TestEqual(What + TEXT(": no compile"), Compiles.Num(), 0);
	TestFalse(What + TEXT(": package clean"), Test->IsDirty());
}

void FN2CVariableDefaultsSpec::Define()
{
	using namespace N2CVariableDefaultsSpec;

	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
	});

	Describe(TEXT("set-member-variable-default-value"), [this]()
	{
		It(TEXT("writes a float default on the class defaults in one undo step, without compiling; undo restores the class defaults"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefFloat"), { { TEXT("Speed"), FloatPinType() } }))
			{
				return;
			}
			const int32 UndoBefore = N2CTest::UndoQueueLength();
			N2CTest::FCompileCounter Counter(Blueprint);
			bool bOk = false;
			FN2CMcpVariableDefaults::FMemberResult Result;
			FString Error;
			const TArray<FN2CCompileDiagnostics> Compiles = Set(TEXT("Speed"), TEXT("75"), bOk, Result, Error);
			if (!TestTrue(TEXT("set: ") + Error, bOk))
			{
				return;
			}
			TestEqual(TEXT("no compile in the call"), Compiles.Num(), 0);
			TestEqual(TEXT("no full compile"), Counter.Count, 0);
			TestFalse(TEXT("not compiled first"), Result.bCompiledFirst);
			TestEqual(TEXT("oldValue read from the class defaults"), Result.OldValue, FString(TEXT("0.000000")));
			TestEqual(TEXT("newValue read back"), Result.NewValue, FString(TEXT("75.000000")));
			TestEqual(TEXT("the class defaults hold 75"), DefaultNumber(Blueprint, TEXT("Speed")), 75.0);
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), UndoBefore + 1);
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			TestEqual(TEXT("the description keeps no default"), FindMember(Blueprint, TEXT("Speed"))->DefaultValue, FString());

			TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
			TestEqual(TEXT("undo restores the class defaults"), DefaultNumber(Blueprint, TEXT("Speed")), 0.0);
			TestTrue(TEXT("redo runs"), GEditor->RedoTransaction());
			TestEqual(TEXT("redo sets it again"), DefaultNumber(Blueprint, TEXT("Speed")), 75.0);
			Recompile(Blueprint);
			TestEqual(TEXT("a compile keeps the class default"), DefaultNumber(Blueprint, TEXT("Speed")), 75.0);
			TestEqual(TEXT("read as text"), DefaultText(Blueprint, TEXT("Speed")), FString(TEXT("75.000000")));
		});

		It(TEXT("refuses a value that does not parse as the variable's type and changes nothing"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefRefuse"), { { TEXT("Speed"), FloatPinType() }, { TEXT("Count"), N2CTest::FTestBlueprint::IntPinType() },
				{ TEXT("Flag"), N2CTest::FTestBlueprint::BoolPinType() } }))
			{
				return;
			}
			ExpectRefused(TEXT("Speed"), TEXT("abc"), TEXT("not a valid"));
			ExpectRefused(TEXT("Count"), TEXT("1.5"), TEXT("not a valid"));
			ExpectRefused(TEXT("Count"), TEXT("12abc"), TEXT("not a valid"));
			ExpectRefused(TEXT("Flag"), TEXT("maybe"), TEXT("not a Boolean"));
			ExpectRefused(TEXT("Missing"), TEXT("1"), TEXT("not found"));
		});

		It(TEXT("resets to the type's default value on an empty value"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefReset"), { { TEXT("Count"), N2CTest::FTestBlueprint::IntPinType() }, { TEXT("Label"), NamePinType() } }))
			{
				return;
			}
			bool bOk = false;
			FN2CMcpVariableDefaults::FMemberResult Result;
			FString Error;
			Set(TEXT("Count"), TEXT("5"), bOk, Result, Error);
			TestEqual(TEXT("set to 5"), DefaultNumber(Blueprint, TEXT("Count")), 5.0);
			Set(TEXT("Count"), TEXT(""), bOk, Result, Error);
			TestTrue(TEXT("the empty value is accepted: ") + Error, bOk);
			TestEqual(TEXT("oldValue 5"), Result.OldValue, FString(TEXT("5")));
			TestEqual(TEXT("reset to 0"), DefaultNumber(Blueprint, TEXT("Count")), 0.0);
			Set(TEXT("Label"), TEXT("Alpha"), bOk, Result, Error);
			TestEqual(TEXT("name set"), DefaultText(Blueprint, TEXT("Label")), FString(TEXT("Alpha")));
			Set(TEXT("Label"), TEXT(""), bOk, Result, Error);
			TestEqual(TEXT("name reset to None"), DefaultText(Blueprint, TEXT("Label")), FString(TEXT("None")));
		});

		It(TEXT("takes a user enum entry by name, display name or index, and lists the entries for one it does not know"), [this]()
		{
			Test = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CDefEnum"));
			Blueprint = Test->GetBlueprint();
			UUserDefinedEnum* Enum = Test->AddUserEnum(FName(*FString::Printf(TEXT("E_N2CDefMode_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8))),
				{ TEXT("Idle"), TEXT("Moving"), TEXT("Dead") });
			if (!TestNotNull(TEXT("Blueprint"), Blueprint) || !TestNotNull(TEXT("user enum"), Enum)
				|| !TestTrue(TEXT("added Mode"), Test->AddMemberVariable(TEXT("Mode"), N2CTest::FTestBlueprint::EnumPinType(Enum))))
			{
				return;
			}
			Test->Settle();
			struct FRow { FString Value; double Expected; };
			const TArray<FRow> Rows = {
				{ TEXT("Dead"), 2.0 },                                 // display name
				{ Enum->GetNameStringByIndex(1), 1.0 },                // the entry's full name (E_...::NewEnumerator1)
				{ TEXT("0"), 0.0 },                                    // index
				{ TEXT("moving"), 1.0 },                               // display name, any case
			};
			for (const FRow& Row : Rows)
			{
				bool bOk = false;
				FN2CMcpVariableDefaults::FMemberResult Result;
				FString Error;
				Set(TEXT("Mode"), Row.Value, bOk, Result, Error);
				if (TestTrue(FString::Printf(TEXT("'%s' is accepted: %s"), *Row.Value, *Error), bOk))
				{
					TestEqual(FString::Printf(TEXT("'%s' sets entry %d"), *Row.Value, static_cast<int32>(Row.Expected)), DefaultNumber(Blueprint, TEXT("Mode")), Row.Expected);
				}
			}
			Test->GetPackage()->SetDirtyFlag(false);
			ExpectRefused(TEXT("Mode"), TEXT("Flying"), TEXT("Idle"));
		});

		It(TEXT("applies a default the description still holds first, so a later compile keeps the new value"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefPending"), {}))
			{
				return;
			}
			// A variable added with a default and not compiled yet: the description holds 3, the class has no property
			if (!TestTrue(TEXT("added Pending"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Pending"), N2CTest::FTestBlueprint::IntPinType(), TEXT("3"))))
			{
				return;
			}
			bool bOk = false;
			FN2CMcpVariableDefaults::FMemberResult Result;
			FString Error;
			const TArray<FN2CCompileDiagnostics> Compiles = Set(TEXT("Pending"), TEXT("9"), bOk, Result, Error);
			if (!TestTrue(TEXT("set: ") + Error, bOk))
			{
				return;
			}
			TestTrue(TEXT("compiled first"), Result.bCompiledFirst);
			TestEqual(TEXT("one compile, reported"), Compiles.Num(), 1);
			TestEqual(TEXT("oldValue is the description's default, applied"), Result.OldValue, FString(TEXT("3")));
			TestEqual(TEXT("the class defaults hold 9"), DefaultNumber(Blueprint, TEXT("Pending")), 9.0);
			Recompile(Blueprint);
			TestEqual(TEXT("a later compile keeps 9"), DefaultNumber(Blueprint, TEXT("Pending")), 9.0);
		});
	});

	Describe(TEXT("get-blueprint-member-variables"), [this]()
	{
		It(TEXT("reads a create-variable default from the class defaults (card 0171: Health 100 read back empty)"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefHealth"), {}))
			{
				return;
			}
			FN2CMcpCreateVariableTool::FVariableCreationSettings Settings;
			Settings.VariableName = TEXT("Health");
			Settings.TypeIdentifier = TEXT("int");
			Settings.DefaultValue = TEXT("100");
			FName Name;
			FString Error;
			N2CTest::InToolCall([&]()
			{
				Name = FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, N2CTest::FTestBlueprint::IntPinType(), Error);
			});
			if (!TestFalse(TEXT("created: ") + Error, Name.IsNone()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Info = DescribeVariable(Blueprint, Name);
			if (TestTrue(TEXT("described"), Info.IsValid()))
			{
				TestEqual(TEXT("defaultValue"), Info->GetStringField(TEXT("defaultValue")), FString(TEXT("100")));
			}
		});

		It(TEXT("reports Instance Editable and Private as the details panel shows them"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefFlags"), { { TEXT("Plain"), N2CTest::FTestBlueprint::IntPinType() }, { TEXT("Shown"), N2CTest::FTestBlueprint::IntPinType() } }))
			{
				return;
			}
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, TEXT("Shown"), /*bNewFlag*/ false);
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, TEXT("Shown"), nullptr, FBlueprintMetadata::MD_Private, TEXT("true"));
			const TSharedPtr<FJsonObject> Plain = DescribeVariable(Blueprint, TEXT("Plain"));
			const TSharedPtr<FJsonObject> Shown = DescribeVariable(Blueprint, TEXT("Shown"));
			if (!TestTrue(TEXT("described"), Plain.IsValid() && Shown.IsValid()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> PlainFlags = Plain->GetObjectField(TEXT("flags"));
			const TSharedPtr<FJsonObject> ShownFlags = Shown->GetObjectField(TEXT("flags"));
			TestFalse(TEXT("a new variable is not Instance Editable"), PlainFlags->GetBoolField(TEXT("instanceEditable")));
			TestFalse(TEXT("a new variable is not Private"), PlainFlags->GetBoolField(TEXT("private")));
			TestTrue(TEXT("Instance Editable once ticked"), ShownFlags->GetBoolField(TEXT("instanceEditable")));
			TestTrue(TEXT("Private once ticked"), ShownFlags->GetBoolField(TEXT("private")));
		});

		It(TEXT("describes a float as float and a map by its key and value"), [this]()
		{
			FEdGraphPinType Map = NamePinType();
			Map.ContainerType = EPinContainerType::Map;
			Map.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Int;
			if (!Build(TEXT("BP_N2CDefTypes"), { { TEXT("Speed"), FloatPinType() }, { TEXT("Scores"), Map } }))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Speed = DescribeVariable(Blueprint, TEXT("Speed"));
			const TSharedPtr<FJsonObject> Scores = DescribeVariable(Blueprint, TEXT("Scores"));
			if (!TestTrue(TEXT("described"), Speed.IsValid() && Scores.IsValid()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> SpeedType = Speed->GetObjectField(TEXT("type"));
			TestEqual(TEXT("float category"), SpeedType->GetStringField(TEXT("category")), FString(TEXT("float")));
			TestEqual(TEXT("float displayString"), SpeedType->GetStringField(TEXT("displayString")), FString(TEXT("float")));
			const TSharedPtr<FJsonObject> MapType = Scores->GetObjectField(TEXT("type"));
			TestEqual(TEXT("a map's element category is its value"), MapType->GetStringField(TEXT("category")), FString(TEXT("int")));
			TestEqual(TEXT("keyType is the key"), MapType->GetObjectField(TEXT("keyType"))->GetStringField(TEXT("category")), FString(TEXT("name")));
			const TSharedPtr<FJsonObject>* ValueType = nullptr;
			if (TestTrue(TEXT("valueType"), MapType->TryGetObjectField(TEXT("valueType"), ValueType)))
			{
				TestEqual(TEXT("valueType is the value"), (*ValueType)->GetStringField(TEXT("category")), FString(TEXT("int")));
			}
			TestEqual(TEXT("TMap<key, value>"), MapType->GetStringField(TEXT("displayString")), FString(TEXT("TMap<name, int>")));
		});
	});

	Describe(TEXT("set-local-function-variable-default-value"), [this]()
	{
		It(TEXT("parses a local's default against the compiled local and stores what the editor stores"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefLocal"), {}))
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(TEXT("N2CDefFn"));
			if (!TestNotNull(TEXT("function"), Function))
			{
				return;
			}
			FName IntLocal, FloatLocal;
			FString Error;
			N2CTest::InToolCall([&]()
			{
				IntLocal = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Count"), N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
				FloatLocal = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Rate"), FloatPinType(), FString(), FString(), Error);
			});
			UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
			if (!TestFalse(TEXT("locals created: ") + Error, IntLocal.IsNone() || FloatLocal.IsNone()) || !TestNotNull(TEXT("entry"), Entry))
			{
				return;
			}
			FString Stored;
			TestTrue(TEXT("42 parses"), FN2CMcpVariableDefaults::PrepareLocalDefault(Blueprint, Function, Entry, IntLocal, TEXT("42"), Stored, Error));
			TestEqual(TEXT("stored 42"), Stored, FString(TEXT("42")));
			TestTrue(TEXT("1.5 parses"), FN2CMcpVariableDefaults::PrepareLocalDefault(Blueprint, Function, Entry, FloatLocal, TEXT("1.5"), Stored, Error));
			TestEqual(TEXT("stored as the editor stores a float"), Stored, FString(TEXT("1.500000")));
			TestTrue(TEXT("empty clears"), FN2CMcpVariableDefaults::PrepareLocalDefault(Blueprint, Function, Entry, IntLocal, TEXT(""), Stored, Error));
			TestEqual(TEXT("stored empty"), Stored, FString());
			Error.Reset();
			TestFalse(TEXT("abc is refused for an int"), FN2CMcpVariableDefaults::PrepareLocalDefault(Blueprint, Function, Entry, IntLocal, TEXT("abc"), Stored, Error));
			TestTrue(TEXT("with the reason: ") + Error, Error.Contains(TEXT("not a valid")));
		});
	});

#if N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
	Describe(TEXT("through the MCP tools on a focused Blueprint (UE 5.8)"), [this]()
	{
		It(TEXT("set-member-variable-default-value and get-blueprint-member-variables"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefTool"), { { TEXT("Speed"), FloatPinType() } }) || !Test->OpenInEditor(*this, Test->EventGraph()))
			{
				return;
			}
			const int32 UndoBefore = N2CTest::UndoQueueLength();
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("variableName"), TEXT("Speed"));
			Args->SetStringField(TEXT("defaultValue"), TEXT("75"));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("set-member-variable-default-value"), Args);
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (!TestTrue(TEXT("set: ") + N2CTest::ResultText(Result).Left(300), !Result.bIsError && Json.IsValid()))
			{
				return;
			}
			TestEqual(TEXT("oldDefaultValue"), Json->GetStringField(TEXT("oldDefaultValue")), FString(TEXT("0.000000")));
			TestEqual(TEXT("newDefaultValue"), Json->GetStringField(TEXT("newDefaultValue")), FString(TEXT("75.000000")));
			TestFalse(TEXT("no compile ran"), N2CTest::ResultCompile(Result).IsValid());
			TestEqual(TEXT("the class defaults hold 75"), DefaultNumber(Blueprint, TEXT("Speed")), 75.0);
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), UndoBefore + 1);
			TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
			TestEqual(TEXT("undo restores the class defaults"), DefaultNumber(Blueprint, TEXT("Speed")), 0.0);
			TestTrue(TEXT("redo runs"), GEditor->RedoTransaction());

			Args->SetStringField(TEXT("defaultValue"), TEXT("abc"));
			const FMcpToolCallResult Bad = N2CTest::RunTool(TEXT("set-member-variable-default-value"), Args);
			TestTrue(TEXT("abc is an error: ") + N2CTest::ResultText(Bad).Left(300), Bad.bIsError && N2CTest::ResultText(Bad).Contains(TEXT("not a valid")));
			TestEqual(TEXT("the class defaults keep 75"), DefaultNumber(Blueprint, TEXT("Speed")), 75.0);

			const FMcpToolCallResult Listed = N2CTest::RunTool(TEXT("get-blueprint-member-variables"), MakeShared<FJsonObject>());
			const TSharedPtr<FJsonObject> List = N2CTest::ResultJson(Listed);
			const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
			if (TestTrue(TEXT("listed"), List.IsValid() && List->TryGetArrayField(TEXT("variables"), Variables) && Variables->Num() == 1))
			{
				const TSharedPtr<FJsonObject> Speed = (*Variables)[0]->AsObject();
				TestEqual(TEXT("listed default"), Speed->GetStringField(TEXT("defaultValue")), FString(TEXT("75.000000")));
				TestEqual(TEXT("listed type"), Speed->GetObjectField(TEXT("type"))->GetStringField(TEXT("category")), FString(TEXT("float")));
			}

			Args->SetStringField(TEXT("defaultValue"), TEXT(""));
			const FMcpToolCallResult Reset = N2CTest::RunTool(TEXT("set-member-variable-default-value"), Args);
			TestFalse(TEXT("an empty value resets: ") + N2CTest::ResultText(Reset).Left(300), Reset.bIsError);
			TestEqual(TEXT("reset to 0"), DefaultNumber(Blueprint, TEXT("Speed")), 0.0);
		});

		It(TEXT("set-local-function-variable-default-value refuses a value the local cannot hold and stores a valid one"), [this]()
		{
			if (!Build(TEXT("BP_N2CDefLocalTool"), {}))
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(TEXT("N2CDefToolFn"));
			FName Local;
			FString Error;
			N2CTest::InToolCall([&]()
			{
				Local = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Count"), N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
			});
			Test->Settle();
			UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
			if (!TestFalse(TEXT("local created: ") + Error, Local.IsNone()) || !TestNotNull(TEXT("entry"), Entry) || !Test->OpenInEditor(*this, Function))
			{
				return;
			}
			const int32 UndoBefore = N2CTest::UndoQueueLength();
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("variableName"), Local.ToString());
			Args->SetStringField(TEXT("defaultValue"), TEXT("abc"));
			const FMcpToolCallResult Bad = N2CTest::RunTool(TEXT("set-local-function-variable-default-value"), Args);
			TestTrue(TEXT("abc is an error: ") + N2CTest::ResultText(Bad).Left(300), Bad.bIsError && N2CTest::ResultText(Bad).Contains(TEXT("not a valid")));
			TestEqual(TEXT("the description is unchanged"), FN2CMcpVariableUtils::FindLocalVariable(Entry, Local.ToString())->DefaultValue, FString());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), UndoBefore);

			Args->SetStringField(TEXT("defaultValue"), TEXT("42"));
			const FMcpToolCallResult Good = N2CTest::RunTool(TEXT("set-local-function-variable-default-value"), Args);
			TestFalse(TEXT("42 is set: ") + N2CTest::ResultText(Good).Left(300), Good.bIsError);
			TestEqual(TEXT("the description holds 42"), FN2CMcpVariableUtils::FindLocalVariable(Entry, Local.ToString())->DefaultValue, FString(TEXT("42")));
			N2CTest::ExpectToolCompiled(*this, TEXT("set-local-function-variable-default-value"), Good);
		});
	});
#else
	Describe(TEXT("through the MCP tools on a focused Blueprint (UE 5.8)"), [this]()
	{
		It(TEXT("is skipped below UE 5.8"), [this]()
		{
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there (see N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR); the cases above run the same code"));
		});
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
