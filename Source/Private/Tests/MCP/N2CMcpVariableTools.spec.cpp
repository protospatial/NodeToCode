// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateVariableTool.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateLocalVariableTool.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpGetBlueprintMemberVariablesTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "Runtime/Launch/Resources/Version.h"

// get-blueprint-member-variables' type description, the read side of the round trip (private in the tool)
N2C_TEST_PRIVATE_MEMBER(FN2CVariableToolsPinTypeToJson,
	TSharedPtr<FJsonObject> (FN2CMcpGetBlueprintMemberVariablesTool::*)(const FEdGraphPinType&) const,
	&FN2CMcpGetBlueprintMemberVariablesTool::PinTypeToJson)

namespace N2CVariableToolsSpec
{
	/** One terminal type of an expected pin type: category, sub-category and sub-type object (a path or a placeholder) */
	struct FTerminal
	{
		FName Category;
		FName SubCategory;
		const TCHAR* Object = nullptr;
	};

	/**
	 * One row of the type table: the create-variable arguments, the pin type the editor's own type picker makes for that
	 * type, an optional default value, and the open findings that keep a part of the row disabled. Placeholders in the
	 * identifiers and object paths: {S}/{SN} the test's user struct (path/name), {E}/{EN} its user enum, {E1} the user
	 * enum's second enumerator, {BP} the generated class of a sibling Blueprint. The test package is mounted under
	 * /NodeToCode, so every user type is plugin content (mcp-tools-variables-7).
	 */
	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* Type;
		const TCHAR* Container;
		const TCHAR* Key;
		FTerminal Expected;
		FTerminal ExpectedValue; // the value of a map (Expected is its key)
		const TCHAR* Default = nullptr;
		/** A sub-type the caller wants: create-variable has no argument for it (card 0292), so it is dropped like the tool drops it */
		const TCHAR* SubType = nullptr;
		/** Finding and card that keep the create round trip (and everything after it in the case) disabled */
		const TCHAR* CreateDefect = nullptr;
		/** Finding and card that keep the get-blueprint-member-variables read-back disabled */
		const TCHAR* ReadBackDefect = nullptr;
	};

	static const FName PC_Bool = UEdGraphSchema_K2::PC_Boolean;

	static const TCHAR* const Map0283 = TEXT("mcp-tools-variables-8 (card 0283): get-blueprint-member-variables writes a map's key as its value and the value as keyType");
	static const TCHAR* const Real0283 = TEXT("mcp-tools-variables-8 (card 0283): get-blueprint-member-variables reports float and double (PC_Real) as category 'unknown'");
	static const TCHAR* const Sub0292 = TEXT("mcp-tools-variables-15 (card 0292): create-variable has no sub-type argument, so a class or soft reference to a given class cannot be asked for");

	static TArray<FCase> Cases()
	{
		const FName Real = UEdGraphSchema_K2::PC_Real;
		const FName Struct = UEdGraphSchema_K2::PC_Struct;
		const FName Object = UEdGraphSchema_K2::PC_Object;
		const FName Byte = UEdGraphSchema_K2::PC_Byte;
		const FName Int = UEdGraphSchema_K2::PC_Int;
		const FName Name = UEdGraphSchema_K2::PC_Name;
		const TCHAR* Actor = TEXT("/Script/Engine.Actor");
		const TCHAR* HitResult = TEXT("/Script/Engine.HitResult");
		const TCHAR* Guid = TEXT("/Script/CoreUObject.Guid");
		const TCHAR* Vector = TEXT("/Script/CoreUObject.Vector");
		const TCHAR* Channel = TEXT("/Script/Engine.ECollisionChannel");
		const TCHAR* UObjectPath = TEXT("/Script/CoreUObject.Object");

		TArray<FCase> Out;
		auto Add = [&Out](const TCHAR* Label, const TCHAR* Type, FTerminal Expected, const TCHAR* Default = nullptr) -> FCase&
		{
			FCase& Case = Out.AddDefaulted_GetRef();
			Case.Label = Label;
			Case.Type = Type;
			Case.Container = TEXT("none");
			Case.Key = TEXT("");
			Case.Expected = Expected;
			Case.Default = Default;
			return Case;
		};
		auto AddContainer = [&Out](const TCHAR* Label, const TCHAR* Type, const TCHAR* Container, FTerminal Expected, const TCHAR* Default = nullptr) -> FCase&
		{
			FCase& Case = Out.AddDefaulted_GetRef();
			Case.Label = Label;
			Case.Type = Type;
			Case.Container = Container;
			Case.Key = TEXT("");
			Case.Expected = Expected;
			Case.Default = Default;
			return Case;
		};
		auto AddMap = [&Out](const TCHAR* Label, const TCHAR* Key, const TCHAR* Value, FTerminal ExpectedKey, FTerminal ExpectedValue) -> FCase&
		{
			FCase& Case = Out.AddDefaulted_GetRef();
			Case.Label = Label;
			Case.Type = Value;
			Case.Container = TEXT("map");
			Case.Key = Key;
			Case.Expected = ExpectedKey;
			Case.ExpectedValue = ExpectedValue;
			Case.ReadBackDefect = Map0283;
			return Case;
		};

		// Primitives
		Add(TEXT("bool"), TEXT("bool"), { PC_Bool }, TEXT("true"));
		Add(TEXT("int"), TEXT("int"), { Int }, TEXT("42"));
		Add(TEXT("int64"), TEXT("int64"), { UEdGraphSchema_K2::PC_Int64 }, TEXT("9000000000"));
		Add(TEXT("byte"), TEXT("uint8"), { Byte }, TEXT("7"));
		Add(TEXT("float"), TEXT("float"), { Real, UEdGraphSchema_K2::PC_Float }, TEXT("1.5")).ReadBackDefect = Real0283;
		Add(TEXT("double"), TEXT("double"), { Real, UEdGraphSchema_K2::PC_Double }, TEXT("2.25")).ReadBackDefect = Real0283;
		Add(TEXT("string"), TEXT("FString"), { UEdGraphSchema_K2::PC_String }, TEXT("hello"));
		Add(TEXT("name"), TEXT("FName"), { Name }, TEXT("Alpha"));
		Add(TEXT("text"), TEXT("FText"), { UEdGraphSchema_K2::PC_Text }, TEXT("Hi"));

		// Enums (byte pins with the enum as sub-type) and structs
		Add(TEXT("native enum by path"), Channel, { Byte, NAME_None, Channel }, TEXT("ECC_Camera"));
		Add(TEXT("native enum by name"), TEXT("ECollisionChannel"), { Byte, NAME_None, Channel });
		Add(TEXT("user enum by name"), TEXT("{EN}"), { Byte, NAME_None, TEXT("{E}") }, TEXT("{E1}"));
		Add(TEXT("user enum by plugin path"), TEXT("{E}"), { Byte, NAME_None, TEXT("{E}") });
		Add(TEXT("native struct"), TEXT("HitResult"), { Struct, NAME_None, HitResult });
		Add(TEXT("math struct"), TEXT("FVector"), { Struct, NAME_None, Vector }, TEXT("(X=1.0,Y=2.0,Z=3.0)"));
		Add(TEXT("user struct by name"), TEXT("{SN}"), { Struct, NAME_None, TEXT("{S}") });
		Add(TEXT("user struct by plugin path"), TEXT("{S}"), { Struct, NAME_None, TEXT("{S}") });

		// References
		Add(TEXT("object by name"), TEXT("Actor"), { Object, NAME_None, Actor });
		Add(TEXT("object by path"), Actor, { Object, NAME_None, Actor });
		Add(TEXT("Blueprint class object"), TEXT("{BP}"), { Object, NAME_None, TEXT("{BP}") });
		Add(TEXT("plugin native class"), TEXT("/Script/EnhancedInput.InputAction"), { Object, NAME_None, TEXT("/Script/EnhancedInput.InputAction") });
		Add(TEXT("interface"), TEXT("IActorSoundParameterInterface"),
			{ UEdGraphSchema_K2::PC_Interface, NAME_None, TEXT("/Script/Engine.ActorSoundParameterInterface") });
		Add(TEXT("soft object"), TEXT("softobject"), { UEdGraphSchema_K2::PC_SoftObject, NAME_None, UObjectPath });
		Add(TEXT("soft class"), TEXT("softclass"), { UEdGraphSchema_K2::PC_SoftClass, NAME_None, UObjectPath });
		{
			FCase& Case = Add(TEXT("class reference to Actor"), TEXT("class"), { UEdGraphSchema_K2::PC_Class, NAME_None, Actor });
			Case.SubType = TEXT("Actor");
			Case.CreateDefect = Sub0292;
			Case.ReadBackDefect = Sub0292;
		}
		{
			FCase& Case = Add(TEXT("soft object reference to Actor"), TEXT("softobject"), { UEdGraphSchema_K2::PC_SoftObject, NAME_None, Actor });
			Case.SubType = TEXT("Actor");
			Case.CreateDefect = Sub0292;
			Case.ReadBackDefect = Sub0292;
		}

		// Arrays and sets
		AddContainer(TEXT("array of int"), TEXT("int32"), TEXT("array"), { Int }, TEXT("(1,2,3)"));
		AddContainer(TEXT("array of a native struct"), TEXT("HitResult"), TEXT("array"), { Struct, NAME_None, HitResult });
		AddContainer(TEXT("array of objects"), TEXT("AActor"), TEXT("array"), { Object, NAME_None, Actor });
		AddContainer(TEXT("array of a user struct"), TEXT("{S}"), TEXT("array"), { Struct, NAME_None, TEXT("{S}") });
		AddContainer(TEXT("set of names"), TEXT("name"), TEXT("set"), { Name });
		AddContainer(TEXT("set of Guid"), TEXT("FGuid"), TEXT("set"), { Struct, NAME_None, Guid });
		AddContainer(TEXT("set of a user enum"), TEXT("{E}"), TEXT("set"), { Byte, NAME_None, TEXT("{E}") });

		// Maps (the main pin type is the key, PinValueType the value, as the editor's variable types)
		AddMap(TEXT("map name to int"), TEXT("FName"), TEXT("int32"), { Name }, { Int });
		AddMap(TEXT("map Guid to Vector"), TEXT("Guid"), TEXT("FVector"), { Struct, NAME_None, Guid }, { Struct, NAME_None, Vector });
		AddMap(TEXT("map int to object"), TEXT("int"), TEXT("Actor"), { Int }, { Object, NAME_None, Actor });
		AddMap(TEXT("map user enum to user struct"), TEXT("{E}"), TEXT("{S}"), { Byte, NAME_None, TEXT("{E}") }, { Struct, NAME_None, TEXT("{S}") });
		return Out;
	}

	/** The user types and sibling Blueprint a case's placeholders stand for */
	struct FContext
	{
		UUserDefinedStruct* Struct = nullptr;
		UUserDefinedEnum* Enum = nullptr;
		UClass* Sibling = nullptr;

		FString Expand(const TCHAR* Text) const
		{
			FString Out = Text ? Text : TEXT("");
			if (Struct)
			{
				Out.ReplaceInline(TEXT("{SN}"), *Struct->GetName());
				Out.ReplaceInline(TEXT("{S}"), *Struct->GetPathName());
			}
			if (Enum)
			{
				Out.ReplaceInline(TEXT("{EN}"), *Enum->GetName());
				Out.ReplaceInline(TEXT("{E1}"), *Enum->GetNameStringByIndex(1));
				Out.ReplaceInline(TEXT("{E}"), *Enum->GetPathName());
			}
			if (Sibling)
			{
				Out.ReplaceInline(TEXT("{BP}"), *Sibling->GetPathName());
			}
			return Out;
		}
	};

	/** A name no other case or leftover package uses, so a short-name lookup cannot find a stale copy */
	static FName UniqueName(const TCHAR* Prefix)
	{
		return FName(FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12)));
	}

	/** Category, sub-category, sub-type object and container, and a map's value: what makes two variable types equal */
	static FString TypeText(const FEdGraphPinType& Type)
	{
		auto Terminal = [](const FName Category, const FName SubCategory, const UObject* Object)
		{
			return FString::Printf(TEXT("%s/%s/%s"), *Category.ToString(), *SubCategory.ToString(), Object ? *Object->GetPathName() : TEXT("-"));
		};
		const TCHAR* Containers[] = { TEXT("none"), TEXT("array"), TEXT("set"), TEXT("map") };
		FString Text = FString::Printf(TEXT("%s %s"), Containers[FMath::Clamp(static_cast<int32>(Type.ContainerType), 0, 3)],
			*Terminal(Type.PinCategory, Type.PinSubCategory, Type.PinSubCategoryObject.Get()));
		if (Type.ContainerType == EPinContainerType::Map)
		{
			Text += TEXT(" -> ") + Terminal(Type.PinValueType.TerminalCategory, Type.PinValueType.TerminalSubCategory,
				Type.PinValueType.TerminalSubCategoryObject.Get());
		}
		return Text;
	}

	static UObject* FindPath(const FString& Path)
	{
		return Path.IsEmpty() ? nullptr : StaticFindObject(UObject::StaticClass(), nullptr, *Path);
	}

	/** The case's expected pin type, or false when a type it names is not loaded */
	static bool ExpectedType(const FCase& Case, const FContext& Context, FEdGraphPinType& Out, FString& OutMissing)
	{
		Out.ResetToDefaults();
		Out.PinCategory = Case.Expected.Category;
		Out.PinSubCategory = Case.Expected.SubCategory;
		const FString Path = Context.Expand(Case.Expected.Object);
		Out.PinSubCategoryObject = FindPath(Path);
		if (!Path.IsEmpty() && !Out.PinSubCategoryObject.IsValid())
		{
			OutMissing = Path;
			return false;
		}
		const FString Container = Case.Container;
		Out.ContainerType = Container == TEXT("array") ? EPinContainerType::Array
			: Container == TEXT("set") ? EPinContainerType::Set
			: Container == TEXT("map") ? EPinContainerType::Map : EPinContainerType::None;
		if (Out.ContainerType == EPinContainerType::Map)
		{
			Out.PinValueType.TerminalCategory = Case.ExpectedValue.Category;
			Out.PinValueType.TerminalSubCategory = Case.ExpectedValue.SubCategory;
			const FString ValuePath = Context.Expand(Case.ExpectedValue.Object);
			Out.PinValueType.TerminalSubCategoryObject = FindPath(ValuePath);
			if (!ValuePath.IsEmpty() && !Out.PinValueType.TerminalSubCategoryObject.IsValid())
			{
				OutMissing = ValuePath;
				return false;
			}
		}
		return true;
	}

	/** create-variable's arguments for a case, as an MCP client sends them */
	static TSharedPtr<FJsonObject> CreateArgs(const FCase& Case, const FContext& Context, const FString& VariableName)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("variableName"), VariableName);
		Args->SetStringField(TEXT("typeIdentifier"), Context.Expand(Case.Type));
		Args->SetStringField(TEXT("containerType"), Case.Container);
		if (Case.Key && *Case.Key)
		{
			Args->SetStringField(TEXT("mapKeyTypeIdentifier"), Context.Expand(Case.Key));
		}
		if (Case.SubType)
		{
			// Not in create-variable's schema (card 0292): sent anyway, as an agent following the resolver's own
			// INVALID_SUBTYPE error would
			Args->SetStringField(TEXT("subTypeIdentifier"), Case.SubType);
		}
		if (Case.Default)
		{
			Args->SetStringField(TEXT("defaultValue"), Context.Expand(Case.Default));
		}
		return Args;
	}

	/**
	 * What create-variable's Execute runs once it has the Blueprint, from the same arguments: argument parsing, the
	 * container check, FN2CMcpTypeResolver::ResolvePinType with no sub-type (the tool passes none) and
	 * CreateMemberVariable. The tool itself finds the Blueprint from the focused editor, which a headless test can only
	 * open on UE 5.8; the "through the MCP tools" cases run the tool there. Returns the variable's name, or NAME_None
	 * with OutError.
	 */
	static FName CreateLikeTheTool(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Args, FEdGraphPinType& OutResolved, FString& OutError)
	{
		FN2CMcpArgumentParser Parser(Args);
		FN2CMcpCreateVariableTool::FVariableCreationSettings Settings;
		if (!Parser.TryGetRequiredName(TEXT("variableName"), Settings.VariableName, OutError)
			|| !Parser.TryGetRequiredName(TEXT("typeIdentifier"), Settings.TypeIdentifier, OutError))
		{
			return NAME_None;
		}
		Settings.DefaultValue = Parser.GetOptionalString(TEXT("defaultValue"));
		FString Container, Key;
		FN2CMcpVariableUtils::ParseContainerTypeArguments(Parser, Container, Key);
		if (!FN2CMcpVariableUtils::ValidateContainerTypeParameters(Container, Key, OutError)
			|| !FN2CMcpTypeResolver::ResolvePinType(Settings.TypeIdentifier, TEXT(""), Container, Key, false, false, OutResolved, OutError))
		{
			return NAME_None;
		}
		return FN2CMcpCreateVariableTool::CreateMemberVariable(Blueprint, Settings, OutResolved, OutError);
	}

	/**
	 * create-variable arguments read back from get-blueprint-member-variables' description of a variable type: the
	 * typeObject path when there is one (a class, struct or enum), else the category; a class or soft category names
	 * itself (the tool has no sub-type argument); container and map key as the description gives them
	 */
	static void ReadBackArgs(const TSharedPtr<FJsonObject>& Type, FString& OutType, FString& OutContainer, FString& OutKey)
	{
		auto Identifier = [](const TSharedPtr<FJsonObject>& Terminal)
		{
			FString Category, Object;
			Terminal->TryGetStringField(TEXT("category"), Category);
			Terminal->TryGetStringField(TEXT("typeObject"), Object);
			const bool bNamesItself = Category == TEXT("class") || Category == TEXT("softobject") || Category == TEXT("softclass");
			return (!Object.IsEmpty() && !bNamesItself) ? Object : Category;
		};
		OutType = Identifier(Type);
		OutContainer = TEXT("none");
		Type->TryGetStringField(TEXT("container"), OutContainer);
		OutKey.Reset();
		const TSharedPtr<FJsonObject>* KeyType = nullptr;
		if (Type->TryGetObjectField(TEXT("keyType"), KeyType))
		{
			OutKey = Identifier(*KeyType);
		}
	}

	static void Recompile(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

	static const FBPVariableDescription* FindMember(const UBlueprint* Blueprint, const FName Name)
	{
		const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
		return Index == INDEX_NONE ? nullptr : &Blueprint->NewVariables[Index];
	}

	static FEdGraphPinType PropertyPinType(const FProperty* Property)
	{
		FEdGraphPinType Type;
		GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, Type);
		return Type;
	}

	/** Every node of every graph of the Blueprint with this GUID */
	static int32 NodesWithGuid(UBlueprint* Blueprint, const FGuid& Guid)
	{
		int32 Count = 0;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				Count += (Node && Node->NodeGuid == Guid) ? 1 : 0;
			}
		}
		return Count;
	}

	static bool RunKnownDefects()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_RUN_KNOWN_DEFECTS")) == TEXT("1");
	}
}

/**
 * The type table behind create-variable and create-local-variable (card 0148): every row is created on a transient
 * Blueprint in a plugin-mounted package, compiled, and checked through the compiled class with
 * UEdGraphSchema_K2::ConvertPropertyToPinType, so a resolution that the editor would not make (an enum as int32, a
 * struct as UObject) fails here on every engine. A row also checks the variable description, a second compile, the
 * default value on the class defaults, the Get and Set nodes made for it, one exact Undo and Redo, a local variable of
 * the same type, and reading the type back from get-blueprint-member-variables' description. Rejected inputs return an
 * error and change nothing. The resolver in isolation is NodeToCode.MCP.TypeResolver (card 0153); flags, OnRep and
 * the one-step undo of a flagged variable are NodeToCode.MCP.VariableFlags (0154); node GUIDs, pins, wiring and undo of
 * the node tools on a double are NodeToCode.MCP.SpawnedNodes (0155). Review ids: imp-mcp-tools-variables-10;
 * mcp-tools-variables-1, -2, -3, -4, -5, -6, -7, -11, -14.
 */
BEGIN_DEFINE_SPEC(FN2CMcpVariableToolsSpec, "NodeToCode.MCP.Variables", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	N2CVariableToolsSpec::FContext Context;

	/** A Blueprint under /NodeToCode with a user struct, a user enum and a sibling Blueprint, compiled and clean */
	UBlueprint* MakeBlueprint(const TCHAR* Name);
	/** Runs one row through create-variable's path and every check after it */
	void RunMemberCase(const N2CVariableToolsSpec::FCase& Case);
	/** The same type as a local variable of a function */
	void RunLocalCase(const N2CVariableToolsSpec::FCase& Case);
	/** The same type read back from get-blueprint-member-variables' description */
	void RunReadBackCase(const N2CVariableToolsSpec::FCase& Case);
	/**
	 * Runs search-variable-types for each term and checks every typeIdentifier it returns resolves to the kind it names.
	 * bSkipAssetIdentifiers leaves out the export-text identifiers of Blueprint assets (mcp-tools-variables-23).
	 */
	void ExpectSearchResolves(const TArray<FString>& Terms, bool bSkipAssetIdentifiers);
	/** Unique valid GUID in the Blueprint, no two pins with the same name and direction, the value pin of Type */
	void ExpectVariableNode(const FString& What, UBlueprint* Blueprint, const UK2Node_Variable* Node, const FName VariableName,
		const FEdGraphPinType& Type);
END_DEFINE_SPEC(FN2CMcpVariableToolsSpec)

UBlueprint* FN2CMcpVariableToolsSpec::MakeBlueprint(const TCHAR* Name)
{
	using namespace N2CVariableToolsSpec;
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name, TEXT("/NodeToCode"));
	UBlueprint* Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return nullptr;
	}
	Context = FContext();
	Context.Struct = Test->AddUserStruct(UniqueName(TEXT("S_N2CVarLoot")), {
		{ TEXT("Count"), N2CTest::FTestBlueprint::IntPinType() }, { TEXT("Weight"), N2CTest::FTestBlueprint::DoublePinType() } });
	Context.Enum = Test->AddUserEnum(UniqueName(TEXT("E_N2CVarMode")), { TEXT("Idle"), TEXT("Moving"), TEXT("Dead") });
	N2CTest::FTestBlueprint* Sibling = Test->AddSiblingBlueprint(UniqueName(TEXT("BP_N2CVarTarget")).ToString());
	Context.Sibling = Sibling && Sibling->GetBlueprint() ? Sibling->GetBlueprint()->GeneratedClass.Get() : nullptr;
	if (!TestNotNull(TEXT("user struct"), Context.Struct) || !TestNotNull(TEXT("user enum"), Context.Enum)
		|| !TestNotNull(TEXT("sibling Blueprint class"), Context.Sibling))
	{
		return nullptr;
	}
	Test->Settle();
	return Blueprint;
}

void FN2CMcpVariableToolsSpec::ExpectVariableNode(const FString& What, UBlueprint* Blueprint, const UK2Node_Variable* Node,
	const FName VariableName, const FEdGraphPinType& Type)
{
	using namespace N2CVariableToolsSpec;
	if (!TestNotNull(What + TEXT(": node"), Node))
	{
		return;
	}
	TestTrue(What + TEXT(": valid GUID"), Node->NodeGuid.IsValid());
	TestEqual(What + TEXT(": the GUID is unique in the Blueprint"), NodesWithGuid(Blueprint, Node->NodeGuid), 1);
	TSet<FString> Seen;
	const UEdGraphPin* ValuePin = nullptr;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		const FString Key = Pin->PinName.ToString() + (Pin->Direction == EGPD_Input ? TEXT(" in") : TEXT(" out"));
		TestFalse(What + TEXT(": one pin named ") + Key, Seen.Contains(Key));
		Seen.Add(Key);
		if (Pin->PinName == VariableName && !ValuePin)
		{
			ValuePin = Pin;
		}
	}
	if (TestNotNull(What + TEXT(": a value pin named after the variable"), ValuePin))
	{
		TestEqual(What + TEXT(": the value pin has the variable's type"), TypeText(ValuePin->PinType), TypeText(Type));
	}
}

void FN2CMcpVariableToolsSpec::RunMemberCase(const N2CVariableToolsSpec::FCase& Case)
{
	using namespace N2CVariableToolsSpec;
	UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarRow"));
	if (!Blueprint)
	{
		return;
	}
	FEdGraphPinType Expected;
	FString Missing;
	if (!TestTrue(TEXT("every type of the row is loaded: ") + Missing, ExpectedType(Case, Context, Expected, Missing)))
	{
		return;
	}

	// create-variable
	const FString Before = N2CTest::Snapshot(Blueprint);
	const int32 UndoBefore = N2CTest::UndoQueueLength();
	FEdGraphPinType Resolved;
	FString Error;
	FName Name;
	const TArray<FN2CCompileDiagnostics> Compiles = N2CTest::InToolCall([&]()
	{
		Name = CreateLikeTheTool(Blueprint, CreateArgs(Case, Context, TEXT("Var")), Resolved, Error);
	});
	if (!TestFalse(TEXT("created: ") + Error, Name.IsNone()))
	{
		return;
	}
	if (TestEqual(TEXT("one compile for the call"), Compiles.Num(), 1))
	{
		TestTrue(TEXT("the Blueprint compiles"), Compiles[0].bSuccess);
		TestEqual(TEXT("no compile errors"), Compiles[0].ErrorCount, 0);
	}
	TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), UndoBefore + 1);
	TestTrue(TEXT("package dirty"), Test->IsDirty());
	TestEqual(TEXT("the resolver gives the editor's type"), TypeText(Resolved), TypeText(Expected));

	const FBPVariableDescription* Var = FindMember(Blueprint, Name);
	if (!TestNotNull(TEXT("variable description"), Var))
	{
		return;
	}
	TestEqual(TEXT("the description has the type"), TypeText(Var->VarType), TypeText(Expected));

	// The compiled class, after the call's compile and after a second one
	for (int32 Pass = 1; Pass <= 2; ++Pass)
	{
		if (Pass == 2)
		{
			Recompile(Blueprint);
		}
		const FString When = Pass == 1 ? TEXT(" after the call's compile") : TEXT(" after a second compile");
		const FProperty* Property = Blueprint->GeneratedClass ? FindFProperty<FProperty>(Blueprint->GeneratedClass, Name) : nullptr;
		if (!TestNotNull(TEXT("compiled property") + When, Property))
		{
			return;
		}
		TestEqual(TEXT("ConvertPropertyToPinType of the compiled property") + When, TypeText(PropertyPinType(Property)), TypeText(Expected));
		if (const FBPVariableDescription* Again = FindMember(Blueprint, Name))
		{
			TestEqual(TEXT("the description's type") + When, TypeText(Again->VarType), TypeText(Expected));
		}
	}

	// The default value reached the class defaults
	if (Case.Default)
	{
		const FString Default = Context.Expand(Case.Default);
		const FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, Name);
		UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
		if (TestNotNull(TEXT("class defaults"), Defaults) && Property)
		{
			const void* Value = Property->ContainerPtrToValuePtr<void>(Defaults);
			if (const FTextProperty* Text = CastField<FTextProperty>(Property))
			{
				TestEqual(TEXT("the text default on the class defaults"), Text->GetPropertyValue(Value).ToString(), Default);
			}
			else
			{
				void* Imported = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
				void* Zero = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
				Property->InitializeValue(Imported);
				Property->InitializeValue(Zero);
				const bool bImported = Property->ImportText_Direct(*Default, Imported, nullptr, PPF_None) != nullptr;
				FString Actual;
				Property->ExportTextItem_Direct(Actual, Value, nullptr, nullptr, PPF_None);
				if (TestTrue(TEXT("the default parses as the type: ") + Default, bImported))
				{
					TestTrue(FString::Printf(TEXT("the class defaults hold %s (they hold %s)"), *Default, *Actual), Property->Identical(Value, Imported));
					TestFalse(TEXT("the default is not the type's zero value"), Property->Identical(Value, Zero));
				}
				Property->DestroyValue(Imported);
				Property->DestroyValue(Zero);
				FMemory::Free(Imported);
				FMemory::Free(Zero);
			}
		}
	}

	// One undo removes the variable exactly; redo brings it back
	N2CTest::ExpectUndoRedoExact(*this, TEXT("create-variable"), Blueprint, Before);
	TestNotNull(TEXT("the variable is back after redo"), FindMember(Blueprint, Name));
	Recompile(Blueprint);

	// Get and Set nodes for it
	for (const bool bSet : { false, true })
	{
		const FString What = bSet ? TEXT("Set node") : TEXT("Get node");
		TSharedPtr<FJsonObject> Result;
		FString NodeError;
		N2CTest::InToolCall([&]()
		{
			Result = FN2CMcpNodeSpawn::CreateMemberVariableNode(Blueprint, Test->EventGraph(), Name.ToString(), bSet,
				FVector2D(200, bSet ? 300 : 100), FString(), NodeError);
		});
		if (!TestTrue(What + TEXT(" created: ") + NodeError, Result.IsValid()))
		{
			continue;
		}
		const UEdGraphNode* Node = N2CTest::FindNodeByGuid(Blueprint, Result->GetStringField(TEXT("nodeId")));
		const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
		TestTrue(What + TEXT(" is the kind asked for"), VariableNode && VariableNode->IsA<UK2Node_VariableSet>() == bSet);
		ExpectVariableNode(What, Blueprint, VariableNode, Name, Expected);
	}
	N2CTest::ExpectCompilesClean(*this, TEXT("with the Get and Set nodes"), Blueprint);
}

void FN2CMcpVariableToolsSpec::RunLocalCase(const N2CVariableToolsSpec::FCase& Case)
{
	using namespace N2CVariableToolsSpec;
	UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarLocalRow"));
	if (!Blueprint)
	{
		return;
	}
	UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CVarFn")));
	if (!TestNotNull(TEXT("function"), Function))
	{
		return;
	}
	Test->Settle();
	FEdGraphPinType Expected;
	FString Missing;
	if (!TestTrue(TEXT("every type of the row is loaded: ") + Missing, ExpectedType(Case, Context, Expected, Missing)))
	{
		return;
	}

	// What create-local-variable runs once it has the function: the same resolution, then AddLocalVariableToFunction
	FEdGraphPinType Resolved;
	FString Error;
	FName Name;
	const TArray<FN2CCompileDiagnostics> Compiles = N2CTest::InToolCall([&]()
	{
		if (FN2CMcpVariableUtils::ValidateContainerTypeParameters(Case.Container, Context.Expand(Case.Key), Error)
			&& FN2CMcpTypeResolver::ResolvePinType(Context.Expand(Case.Type), TEXT(""), Case.Container, Context.Expand(Case.Key), false, false, Resolved, Error))
		{
			Name = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Local"), Resolved,
				Context.Expand(Case.Default), FString(), Error);
		}
	});
	if (!TestFalse(TEXT("created: ") + Error, Name.IsNone()))
	{
		return;
	}
	if (TestEqual(TEXT("one compile for the call"), Compiles.Num(), 1))
	{
		TestTrue(TEXT("the function compiles"), Compiles[0].bSuccess);
	}
	const UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
	const FBPVariableDescription* Local = Entry ? Entry->LocalVariables.FindByPredicate([Name](const FBPVariableDescription& Var) { return Var.VarName == Name; }) : nullptr;
	if (TestNotNull(TEXT("local variable description"), Local))
	{
		TestEqual(TEXT("the local's description has the type"), TypeText(Local->VarType), TypeText(Expected));
	}
	const UFunction* Compiled = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->FindFunctionByName(Function->GetFName()) : nullptr;
	const FProperty* Property = Compiled ? FindFProperty<FProperty>(Compiled, Name) : nullptr;
	if (TestNotNull(TEXT("the compiled function has the local as a property"), Property))
	{
		TestEqual(TEXT("ConvertPropertyToPinType of the compiled local"), TypeText(PropertyPinType(Property)), TypeText(Expected));
	}
}

void FN2CMcpVariableToolsSpec::RunReadBackCase(const N2CVariableToolsSpec::FCase& Case)
{
	using namespace N2CVariableToolsSpec;
	UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarReadRow"));
	if (!Blueprint)
	{
		return;
	}
	FEdGraphPinType Expected;
	FString Missing;
	if (!TestTrue(TEXT("every type of the row is loaded: ") + Missing, ExpectedType(Case, Context, Expected, Missing)))
	{
		return;
	}
	const TSharedPtr<FN2CMcpGetBlueprintMemberVariablesTool> Reader =
		StaticCastSharedPtr<FN2CMcpGetBlueprintMemberVariablesTool>(N2CTest::FindRegisteredTool(TEXT("get-blueprint-member-variables")));
	if (!TestTrue(TEXT("get-blueprint-member-variables is registered"), Reader.IsValid()))
	{
		return;
	}
	// The variable as the editor makes it, then its description as get-blueprint-member-variables writes it
	if (!TestTrue(TEXT("the variable is added"), Test->AddMemberVariable(TEXT("Var"), Expected)))
	{
		return;
	}
	const FBPVariableDescription* Var = FindMember(Blueprint, TEXT("Var"));
	if (!TestNotNull(TEXT("variable description"), Var))
	{
		return;
	}
	const TSharedPtr<FJsonObject> Described = ((*Reader).*N2CTest::GetPrivateMember(N2CTest::FN2CVariableToolsPinTypeToJson()))(Var->VarType);
	FString JsonText;
	FJsonSerializer::Serialize(Described.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonText));
	FString Type, Container, Key, Error;
	ReadBackArgs(Described, Type, Container, Key);
	FEdGraphPinType ReadBack;
	const bool bResolved = FN2CMcpVariableUtils::ValidateContainerTypeParameters(Container, Key, Error)
		&& FN2CMcpTypeResolver::ResolvePinType(Type, TEXT(""), Container, Key, false, false, ReadBack, Error);
	if (TestTrue(FString::Printf(TEXT("the description (%s) resolves again: %s"), *JsonText, *Error), bResolved))
	{
		TestEqual(TEXT("the type read back is the variable's type (") + JsonText + TEXT(")"), TypeText(ReadBack), TypeText(Expected));
	}
}

void FN2CMcpVariableToolsSpec::ExpectSearchResolves(const TArray<FString>& Terms, bool bSkipAssetIdentifiers)
{
	int32 Checked = 0;
	for (const FString& Term : Terms)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("searchTerm"), Term);
		Args->SetNumberField(TEXT("maxResults"), 25);
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("search-variable-types"), Args);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
		if (!TestTrue(TEXT("results for ") + Term, !Result.bIsError && Json.IsValid() && Json->TryGetArrayField(TEXT("types"), Types) && Types->Num() > 0))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Types)
		{
			const TSharedPtr<FJsonObject> Type = Value->AsObject();
			const FString Identifier = Type->GetStringField(TEXT("typeIdentifier"));
			const FString Category = Type->GetStringField(TEXT("category"));
			if (bSkipAssetIdentifiers && Identifier.Contains(TEXT("'")))
			{
				continue;
			}
			FEdGraphPinType Resolved;
			FString Error;
			++Checked;
			if (!TestTrue(FString::Printf(TEXT("'%s' (%s, from '%s') resolves: %s"), *Identifier, *Category, *Term, *Error),
					FN2CMcpTypeResolver::ResolvePinType(Identifier, TEXT(""), TEXT("none"), TEXT(""), false, false, Resolved, Error)))
			{
				continue;
			}
			const UObject* Object = Resolved.PinSubCategoryObject.Get();
			if (Category == TEXT("struct"))
			{
				TestTrue(TEXT("a struct: ") + Identifier, Resolved.PinCategory == UEdGraphSchema_K2::PC_Struct && Object && Object->GetPathName() == Identifier);
			}
			else if (Category == TEXT("enum"))
			{
				TestTrue(TEXT("an enum byte: ") + Identifier, Resolved.PinCategory == UEdGraphSchema_K2::PC_Byte && Object && Object->GetPathName() == Identifier);
			}
			else if (Category == TEXT("class"))
			{
				TestTrue(TEXT("an object or interface reference: ") + Identifier, (Resolved.PinCategory == UEdGraphSchema_K2::PC_Object
					|| Resolved.PinCategory == UEdGraphSchema_K2::PC_Interface) && Object != nullptr);
			}
		}
	}
	TestTrue(TEXT("search results checked"), Checked > 0);
}

void FN2CMcpVariableToolsSpec::Define()
{
	using namespace N2CVariableToolsSpec;

	AfterEach([this]()
	{
		// Closes the editor, cleans the package and resets the undo buffer
		Test.Reset();
		Context = FContext();
	});

	// Rows that fail until their finding is fixed run as xIt (N2C_RUN_KNOWN_DEFECTS=1 runs them too); the fixing card
	// clears the row's defect field
	const bool bRunKnownDefects = RunKnownDefects();

	Describe(TEXT("create-variable round trips through the compiled class"), [this, bRunKnownDefects]()
	{
		for (const FCase& Case : Cases())
		{
			const FString Title = FString::Printf(TEXT("%s (%s)"), Case.Label, Case.Type);
			if (Case.CreateDefect && !bRunKnownDefects)
			{
				// Known defect: see the row's CreateDefect (finding id and owning card). Enable (xIt -> It) with the fix.
				xIt(Title, [this, Case]() { RunMemberCase(Case); });
			}
			else
			{
				It(Title, [this, Case]() { RunMemberCase(Case); });
			}
		}
	});

	Describe(TEXT("create-local-variable round trips through the compiled function"), [this, bRunKnownDefects]()
	{
		for (const FCase& Case : Cases())
		{
			const FString Title = FString::Printf(TEXT("%s (%s)"), Case.Label, Case.Type);
			if (Case.CreateDefect && !bRunKnownDefects)
			{
				// Known defect: see the row's CreateDefect (finding id and owning card). Enable (xIt -> It) with the fix.
				xIt(Title, [this, Case]() { RunLocalCase(Case); });
			}
			else
			{
				It(Title, [this, Case]() { RunLocalCase(Case); });
			}
		}
	});

	Describe(TEXT("get-blueprint-member-variables describes a type create-variable reads back"), [this, bRunKnownDefects]()
	{
		for (const FCase& Case : Cases())
		{
			const FString Title = FString::Printf(TEXT("%s (%s)"), Case.Label, Case.Type);
			if (Case.ReadBackDefect && !bRunKnownDefects)
			{
				// Known defect: see the row's ReadBackDefect (finding id and owning card). Enable (xIt -> It) with the fix.
				xIt(Title, [this, Case]() { RunReadBackCase(Case); });
			}
			else
			{
				It(Title, [this, Case]() { RunReadBackCase(Case); });
			}
		}
	});

	Describe(TEXT("refuses"), [this]()
	{
		It(TEXT("wildcard, exec, delegate and template types before the Blueprint changes"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarRefuse"));
			if (!Blueprint)
			{
				return;
			}
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 UndoBefore = N2CTest::UndoQueueLength();
			for (const TCHAR* Type : { TEXT("wildcard"), TEXT("exec"), TEXT("delegate"), TEXT("multicastdelegate"), TEXT("TArray<int32>") })
			{
				const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("variableName"), TEXT("Bad"));
				Args->SetStringField(TEXT("typeIdentifier"), Type);
				FEdGraphPinType Resolved;
				FString Error;
				FName Name;
				N2CTest::InToolCall([&]() { Name = CreateLikeTheTool(Blueprint, Args, Resolved, Error); });
				TestTrue(FString::Printf(TEXT("%s is refused"), Type), Name.IsNone());
				TestTrue(FString::Printf(TEXT("%s: INVALID_TYPE error (%s)"), Type, *Error), Error.StartsWith(TEXT("INVALID_TYPE")));
			}
			TestEqual(TEXT("the Blueprint is unchanged"), N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint)), FString());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), UndoBefore);
			TestFalse(TEXT("package clean"), Test->IsDirty());
		});

		It(TEXT("an over-long variable name or type identifier through the tools, with an error result"), [this]()
		{
			const FString Long = FString::ChrN(NAME_SIZE + 100, TEXT('A'));
			for (const TCHAR* Tool : { TEXT("create-variable"), TEXT("create-local-variable") })
			{
				for (const TCHAR* Field : { TEXT("variableName"), TEXT("typeIdentifier") })
				{
					const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
					Args->SetStringField(TEXT("variableName"), TEXT("Fine"));
					Args->SetStringField(TEXT("typeIdentifier"), TEXT("int"));
					Args->SetStringField(Field, Long);
					const FMcpToolCallResult Result = N2CTest::RunTool(Tool, Args);
					const FString Text = N2CTest::ResultText(Result);
					TestTrue(FString::Printf(TEXT("%s with a %d-character %s is an error result"), Tool, Long.Len(), Field), Result.bIsError);
					TestTrue(FString::Printf(TEXT("%s names the length limit of %s: %s"), Tool, Field, *Text.Left(200)),
						Text.Contains(Field) && Text.Contains(TEXT("limited")));
				}
			}
		});
	});

	Describe(TEXT("the editor's variable type tree"), [this]()
	{
		It(TEXT("resolves every class, struct and enum it offers, by path, to that type"), [this]()
		{
			TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>> Tree;
			GetDefault<UEdGraphSchema_K2>()->GetVariableTypeTree(Tree, ETypeTreeFilter::None);
			TArray<FString> Mismatches;
			int32 Checked = 0;
			TFunction<void(const TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>>&)> Walk;
			Walk = [&](const TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>>& Items)
			{
				for (const TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>& Item : Items)
				{
					if (!Item.IsValid())
					{
						continue;
					}
					Walk(Item->Children);
					UObject* TypeObject = Item->GetPinType(/*bForceLoadedSubCategoryObject*/ false).PinSubCategoryObject.Get();
					if (!TypeObject)
					{
						continue;
					}
					FEdGraphPinType Expected;
					if (UClass* Class = Cast<UClass>(TypeObject))
					{
						Expected.PinCategory = Class->HasAnyClassFlags(CLASS_Interface) ? UEdGraphSchema_K2::PC_Interface : UEdGraphSchema_K2::PC_Object;
					}
					else if (TypeObject->IsA<UScriptStruct>())
					{
						Expected.PinCategory = UEdGraphSchema_K2::PC_Struct;
					}
					else if (TypeObject->IsA<UEnum>())
					{
						Expected.PinCategory = UEdGraphSchema_K2::PC_Byte;
					}
					else
					{
						continue;
					}
					Expected.PinSubCategoryObject = TypeObject;
					++Checked;
					FEdGraphPinType Resolved;
					FString Error;
					const bool bOk = FN2CMcpTypeResolver::ResolvePinType(TypeObject->GetPathName(), TEXT(""), TEXT("none"), TEXT(""), false, false, Resolved, Error);
					if (!bOk || TypeText(Resolved) != TypeText(Expected))
					{
						Mismatches.Add(FString::Printf(TEXT("%s: %s"), *TypeObject->GetPathName(), bOk ? *TypeText(Resolved) : *Error));
					}
				}
			};
			Walk(Tree);
			TestTrue(FString::Printf(TEXT("the tree offers types (%d checked)"), Checked), Checked > 100);
			for (int32 Index = 0; Index < FMath::Min(Mismatches.Num(), 10); ++Index)
			{
				AddError(TEXT("resolves differently from the type tree: ") + Mismatches[Index]);
			}
			TestEqual(TEXT("type tree entries the resolver gets wrong"), Mismatches.Num(), 0);
		});
	});

#if N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
	Describe(TEXT("through the MCP tools on a focused Blueprint (UE 5.8)"), [this]()
	{
		It(TEXT("create-variable makes every row's type and get-blueprint-member-variables lists them"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarTools"));
			if (!Blueprint || !Test->OpenInEditor(*this, Test->EventGraph()))
			{
				return;
			}
			TMap<FString, FEdGraphPinType> Made;
			int32 Index = 0;
			for (const FCase& Case : Cases())
			{
				if (Case.CreateDefect)
				{
					continue;
				}
				FEdGraphPinType Expected;
				FString Missing;
				if (!TestTrue(FString::Printf(TEXT("%s: every type is loaded: %s"), Case.Label, *Missing), ExpectedType(Case, Context, Expected, Missing)))
				{
					continue;
				}
				const FString VariableName = FString::Printf(TEXT("Row%02d"), Index++);
				const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-variable"), CreateArgs(Case, Context, VariableName));
				if (!TestTrue(FString::Printf(TEXT("%s: create-variable compiles: %s"), Case.Label, *N2CTest::ResultText(Result).Left(300)),
						N2CTest::ExpectToolCompiled(*this, Case.Label, Result)))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
				const FString Actual = Json.IsValid() ? Json->GetStringField(TEXT("actualName")) : FString();
				TestEqual(FString::Printf(TEXT("%s: actualName"), Case.Label), Actual, VariableName);
				const FProperty* Property = Blueprint->GeneratedClass ? FindFProperty<FProperty>(Blueprint->GeneratedClass, FName(*Actual)) : nullptr;
				if (TestNotNull(FString::Printf(TEXT("%s: compiled property"), Case.Label), Property))
				{
					TestEqual(FString::Printf(TEXT("%s: ConvertPropertyToPinType of the compiled property"), Case.Label),
						TypeText(PropertyPinType(Property)), TypeText(Expected));
				}
				Made.Add(Actual, Expected);
			}

			const FMcpToolCallResult Listed = N2CTest::RunTool(TEXT("get-blueprint-member-variables"), MakeShared<FJsonObject>());
			const TSharedPtr<FJsonObject> ListJson = N2CTest::ResultJson(Listed);
			const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
			if (!TestTrue(TEXT("get-blueprint-member-variables lists variables"), ListJson.IsValid() && ListJson->TryGetArrayField(TEXT("variables"), Variables)))
			{
				return;
			}
			TSet<FString> ListedNames;
			for (const TSharedPtr<FJsonValue>& Value : *Variables)
			{
				ListedNames.Add(Value->AsObject()->GetStringField(TEXT("name")));
			}
			for (const TPair<FString, FEdGraphPinType>& Pair : Made)
			{
				TestTrue(TEXT("listed: ") + Pair.Key, ListedNames.Contains(Pair.Key));
			}
		});

		It(TEXT("create-variable refuses a wildcard on the focused Blueprint and changes nothing"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CVarToolRefuse"));
			if (!Blueprint || !Test->OpenInEditor(*this, Test->EventGraph()))
			{
				return;
			}
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 UndoBefore = N2CTest::UndoQueueLength();
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("variableName"), TEXT("Bad"));
			Args->SetStringField(TEXT("typeIdentifier"), TEXT("wildcard"));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-variable"), Args);
			TestTrue(TEXT("an error result"), Result.bIsError);
			TestTrue(TEXT("INVALID_TYPE: ") + N2CTest::ResultText(Result).Left(200), N2CTest::ResultText(Result).Contains(TEXT("INVALID_TYPE")));
			TestEqual(TEXT("the Blueprint is unchanged"), N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint)), FString());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), UndoBefore);
		});

		It(TEXT("search-variable-types returns identifiers the resolver turns into the type they name"), [this]()
		{
			if (MakeBlueprint(TEXT("BP_N2CVarSearch")) && Test->OpenInEditor(*this, Test->EventGraph()))
			{
				ExpectSearchResolves({ TEXT("HitResult"), TEXT("CollisionChannel"), TEXT("Actor"), TEXT("Vector"), TEXT("Integer"), TEXT("Name"),
					Context.Struct->GetDisplayNameText().ToString(), Context.Enum->GetDisplayNameText().ToString() }, /*bSkipAssetIdentifiers*/ true);
			}
		});

		// Known defect mcp-tools-variables-23 (card 0291): a Blueprint class found through the asset registry is returned
		// as export text ("/Script/Engine.BlueprintGeneratedClass'/Engine/....ActorMacros_C'"), which the resolver
		// refuses (INVALID_TYPE_PATH). Enable (xIt -> It) with the fix.
		xIt(TEXT("search-variable-types returns Blueprint classes as identifiers the resolver accepts"), [this]()
		{
			if (MakeBlueprint(TEXT("BP_N2CVarSearchBP")) && Test->OpenInEditor(*this, Test->EventGraph()))
			{
				ExpectSearchResolves({ TEXT("Actor"), TEXT("Macros") }, /*bSkipAssetIdentifiers*/ false);
			}
		});
	});
#else
	Describe(TEXT("through the MCP tools on a focused Blueprint (UE 5.8)"), [this]()
	{
		It(TEXT("is skipped below UE 5.8"), [this]()
		{
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there (see N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR); the headless rows above run the same path"));
		});
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
