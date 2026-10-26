// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UClass;
class UEdGraph;
class UEdGraphNode;
class UEnum;
class UK2Node_BreakStruct;
class UK2Node_CallFunction;
class UK2Node_Composite;
class UK2Node_CustomEvent;
class UK2Node_Event;
class UK2Node_ExecutionSequence;
class UK2Node_FormatText;
class UK2Node_FunctionEntry;
class UK2Node_FunctionResult;
class UK2Node_IfThenElse;
class UK2Node_Knot;
class UK2Node_MacroInstance;
class UK2Node_MakeStruct;
class UK2Node_SwitchEnum;
class UK2Node_SwitchInteger;
class UK2Node_Timeline;
class UK2Node_Tunnel;
class UK2Node_VariableGet;
class UPackage;
class UScriptStruct;
class UUserDefinedEnum;
class UUserDefinedStruct;

namespace N2CTest
{
	/**
	 * Builds Blueprint graphs in code for capture tests, without assets on disk. Each instance makes one Actor Blueprint
	 * named exactly as asked inside its own transient package, <MountRoot>/N2CTest_<guid> (/Temp by default; /Game when
	 * the code under test only treats content under /Game/ as the user's), so the Blueprint and class names are stable
	 * from run to run while two instances never collide. User structs, enums and input actions made through it live in
	 * the same package. The destructor clears the standalone and public flags of everything in the package so the
	 * garbage collector can take it; nothing is saved or registered with the asset registry.
	 *
	 * Nodes are created in the order of the calls, which is the order of UEdGraph::Nodes and so the order capture
	 * sees them in. Links go through UEdGraphSchema_K2::TryCreateConnection, as the graph editor makes them (knots
	 * take their type from the first link). Every helper returns null, and Link returns false, on failure; the
	 * failure text is kept in Errors() for the test to report.
	 */
	class FTestBlueprint
	{
	public:
		explicit FTestBlueprint(const FString& BlueprintName, const FString& MountRoot = TEXT("/Temp"));
		~FTestBlueprint();

		FTestBlueprint(const FTestBlueprint&) = delete;
		FTestBlueprint& operator=(const FTestBlueprint&) = delete;

		/**
		 * An Actor Blueprint named BlueprintName in a package the caller made and keeps (the perf fixture generator saves
		 * it as an asset): the destructor leaves the package and the Blueprint's flags alone. Errors() says what failed.
		 */
		static TUniquePtr<FTestBlueprint> InPackage(const FString& BlueprintName, UPackage* InPackage);

		UBlueprint* GetBlueprint() const { return Blueprint; }
		/** The transient package's path, for tests that normalise it out of captured text */
		FString GetPackagePath() const;
		UPackage* GetPackage() const { return Package; }
		/** What failed, in this Blueprint and in its siblings */
		TArray<FString> Errors() const;

		/**
		 * Another Actor Blueprint named BlueprintName in this one's package (so the same package path normalises both),
		 * owned by this one and released with it. Null and an error when it cannot be made.
		 */
		FTestBlueprint* AddSiblingBlueprint(const FString& BlueprintName);

		/**
		 * Another Actor Blueprint named BlueprintName in a transient package of its own under MountRoot (for example
		 * /NodeToCode, the plugin's own mount, standing in for a content or GameFeature plugin), owned by this one and
		 * released with it. Null and an error when it cannot be made.
		 */
		FTestBlueprint* AddBlueprintAt(const FString& BlueprintName, const FString& MountRoot);

		/** The event graph (holds the default ghost events CreateBlueprint adds to an Actor Blueprint) */
		UEdGraph* EventGraph() const;

		/** A new user function graph with its entry node */
		UEdGraph* AddFunction(const FName Name);
		UK2Node_FunctionEntry* FunctionEntry(UEdGraph* FunctionGraph) const;
		/** Adds a user input parameter (an output pin of the entry node) */
		UEdGraphPin* AddFunctionInput(UEdGraph* FunctionGraph, const FName Name, const FEdGraphPinType& Type);
		/** Adds a return node with one output parameter, or adds the parameter to the existing return node */
		UK2Node_FunctionResult* AddFunctionOutput(UEdGraph* FunctionGraph, const FName Name, const FEdGraphPinType& Type);

		UK2Node_CustomEvent* AddCustomEvent(UEdGraph* Graph, const FName EventName);
		UK2Node_CallFunction* AddCall(UEdGraph* Graph, UClass* Owner, const FName FunctionName);
		UK2Node_IfThenElse* AddBranch(UEdGraph* Graph);
		/** A Sequence node with its two default outputs (then_0, then_1) */
		UK2Node_ExecutionSequence* AddSequence(UEdGraph* Graph);
		UK2Node_Knot* AddKnot(UEdGraph* Graph);
		UK2Node_SwitchInteger* AddSwitchOnInt(UEdGraph* Graph, int32 CaseCount);
		UK2Node_SwitchEnum* AddSwitchOnEnum(UEdGraph* Graph, UEnum* Enum);
		UK2Node_MakeStruct* AddMakeStruct(UEdGraph* Graph, UScriptStruct* Struct);
		UK2Node_BreakStruct* AddBreakStruct(UEdGraph* Graph, UScriptStruct* Struct);
		/** An instance of a macro from /Engine/EditorBlueprintResources/StandardMacros (Gate, DoOnce, ForEachLoop...) */
		UK2Node_MacroInstance* AddStandardMacro(UEdGraph* Graph, const FName MacroName);
		/**
		 * A Format Text node whose Format is FormatPattern, with one wildcard argument input per {name} in it, as the
		 * editor makes them when the Format text is typed in
		 */
		UK2Node_FormatText* AddFormatText(UEdGraph* Graph, const FString& FormatPattern);
		/** The event node in Graph that implements the event function EventName (for example ReceiveBeginPlay) */
		UK2Node_Event* FindEvent(UEdGraph* Graph, const FName EventName);
		/** A Timeline node with a new, empty timeline template of that name (no tracks) */
		UK2Node_Timeline* AddTimeline(UEdGraph* Graph, const FName TimelineName);

		/**
		 * A call to one of this Blueprint's own functions. Regenerates the skeleton class first (as the editor does after
		 * a signature change) so the function and its parameters exist; add the function's inputs before calling this.
		 */
		UK2Node_CallFunction* AddSelfCall(UEdGraph* Graph, const FName FunctionName);
		/** Adds a member variable to this Blueprint and regenerates the skeleton class, so variable nodes resolve it */
		bool AddMemberVariable(const FName Name, const FEdGraphPinType& Type);
		/**
		 * A Get node for the member variable Name: this Blueprint's own (self) when Owner is null, else Owner's (a
		 * sibling's), read through the node's Target pin
		 */
		UK2Node_VariableGet* AddVariableGet(UEdGraph* Graph, const FName Name, FTestBlueprint* Owner = nullptr);
		/** A new macro graph in this Blueprint (with its entry and exit tunnels), as My Blueprint > Add Macro makes */
		UEdGraph* AddMacro(const FName Name);
		UK2Node_MacroInstance* AddMacroInstance(UEdGraph* Graph, UEdGraph* MacroGraph);
		/** A collapsed graph node whose bound graph is renamed to GraphName, with empty entry and exit tunnels */
		UK2Node_Composite* AddCollapsedGraph(UEdGraph* Graph, const FName GraphName);
		/** The entry (outputs only) or exit (inputs only) tunnel of a macro or collapsed graph */
		UK2Node_Tunnel* TunnelEntry(UEdGraph* TunnelGraph);
		UK2Node_Tunnel* TunnelExit(UEdGraph* TunnelGraph);
		/** Adds a pin to a tunnel (an output of the entry, an input of the exit); mirrored onto a collapsed node */
		UEdGraphPin* AddTunnelPin(UK2Node_Tunnel* Tunnel, const FName Name, const FEdGraphPinType& Type);

		/**
		 * Copies the nodes and pastes them into Graph as the Blueprint editor does (PrepareForCopying,
		 * FEdGraphUtilities::ExportNodesToText then ImportNodesFromText, and a new node GUID for each pasted node;
		 * pin IDs are kept, as in the editor). Returns the pasted nodes in graph order.
		 */
		TArray<UEdGraphNode*> CopyPaste(UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes);

		/** An Enhanced Input action event node for a new UInputAction named ActionName in this package (by reflection,
		 *  so the plugin needs no EnhancedInput dependency); null and an error when the EnhancedInput plugin is off */
		UEdGraphNode* AddEnhancedInputAction(UEdGraph* Graph, const FName ActionName);

		/**
		 * A user-defined enum in this package (or in Into, a package the caller keeps) with one enumerator per display
		 * name (NewEnumerator0..n)
		 */
		UUserDefinedEnum* AddUserEnum(const FName Name, const TArray<FString>& DisplayNames, UPackage* Into = nullptr);
		/** A user-defined struct in this package (or in Into, a package the caller keeps) with these members, in order */
		UUserDefinedStruct* AddUserStruct(const FName Name, const TArray<TPair<FString, FEdGraphPinType>>& Members,
			UPackage* Into = nullptr);

		/** The node's visible pin with this name (and direction, unless EGPD_MAX); null and an error when missing */
		UEdGraphPin* Pin(UEdGraphNode* Node, const FName PinName, EEdGraphPinDirection Direction = EGPD_MAX);
		/** Connects two pins through the K2 schema; false and an error when the schema refuses */
		bool Link(UEdGraphPin* A, UEdGraphPin* B);
		/** Link(Pin(From, FromPin, EGPD_Output), Pin(To, ToPin, EGPD_Input)) */
		bool Link(UEdGraphNode* From, const FName FromPin, UEdGraphNode* To, const FName ToPin);

		static FEdGraphPinType IntPinType();
		static FEdGraphPinType DoublePinType();
		static FEdGraphPinType BoolPinType();
		static FEdGraphPinType TextPinType();
		static FEdGraphPinType EnumPinType(UEnum* Enum);
		static FEdGraphPinType ExecPinType();
		static FEdGraphPinType ObjectPinType(UClass* Class);
		static FEdGraphPinType StructPinType(UScriptStruct* Struct);

	private:
		/** A sibling: a Blueprint in InPackage, which the sibling does not own */
		FTestBlueprint(const FString& BlueprintName, UPackage* InPackage);
		void CreateBlueprintIn(const FString& BlueprintName);

		UPackage* Package = nullptr;
		bool bOwnsPackage = true;
		/** InPackage: the Blueprint is the caller's asset, so the destructor clears none of its flags */
		bool bKeepBlueprint = false;
		UBlueprint* Blueprint = nullptr;
		TArray<FString> ErrorList;
		TArray<TUniquePtr<FTestBlueprint>> Siblings;

		void AddError(const FString& Error) { ErrorList.Add(Error); }
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
