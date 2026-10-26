// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

/**
 * @file N2CJsonContract.h
 * @brief The N2C JSON contract's version and the field names this version introduced or reshaped
 *
 * Decision record .agents/decisions/0003-n2c-json-v2-contract.md (provisional, card 0117) chose them. Change them here
 * only, so the serializer, the reader, the validator and the tests follow; the prompts (Content/Prompting) and the
 * batch spec describe the same names in prose.
 */

#pragma once

#include "CoreMinimal.h"

namespace N2CJsonContract
{
	/** The version ToJson writes: the v2 contract. Additive optional fields bump the minor, breaking changes the major. */
	inline constexpr const TCHAR* Version = TEXT("2.0.0");

	/** Major version of Version */
	inline constexpr int32 Major = 2;

	/** The oldest major FromJson still reads (1.x: 1.2.x and earlier 2.0 builds); kept through the 2.x line */
	inline constexpr int32 OldestReadableMajor = 1;

	/** Field names */
	inline constexpr const TCHAR* VersionField = TEXT("version");
	inline constexpr const TCHAR* FlowsField = TEXT("flows");
	inline constexpr const TCHAR* ExecutionField = TEXT("execution");

	/**
	 * flows.data: an object keyed by the output pin ref ("N1.P2"). The value is the one input pin ref it feeds, or an
	 * array of input pin refs when it feeds several (fan-out). v1 always had one string, keeping one edge per output.
	 */
	inline constexpr const TCHAR* DataField = TEXT("data");

	/**
	 * flows.execution: one entry per exec link, from the exec output pin to the exec input pin it runs,
	 * "N3.P5->N4.P1" (card 0136). The source pin tells which output fired (Branch True/False, Sequence then_0/then_1,
	 * ForEachLoop Loop Body/Completed, a Switch case), the target pin which input runs (Gate Enter/Open/Close,
	 * Timeline Play/Reverse). Node-only entries ("N1->N2", or a chain "N1->N2->N3") are the 1.x form, also written by
	 * 2.0 development builds before card 0136: readers accept both, writers emit only the pin form.
	 */
	inline constexpr const TCHAR* ExecEdgeSeparator = TEXT("->");

	/**
	 * Node liveness flags (card 0137, issue #13), each written only when true. "unused": a pure node (no exec pins)
	 * whose outputs no live node reads, so Blueprint never evaluates it. "unreachable": an impure node that no entry
	 * (an event, a function entry, a tunnel entry: an impure node without exec inputs) reaches through exec links.
	 * "disabled": a node the compiler leaves out (UEdGraphNode::IsNodeEnabled() is false: Disabled, or DevelopmentOnly
	 * when the Blueprint does not compile in development mode); exec passes through it when it has exactly one exec
	 * input and one exec output, as UK2Node::GetPassThroughPin does, and flows.execution records that bypass. The
	 * editor's automatically placed ghost nodes (a new Blueprint's greyed-out BeginPlay, ActorBeginOverlap and Tick) are
	 * not captured at all.
	 */
	inline constexpr const TCHAR* UnusedField = TEXT("unused");
	inline constexpr const TCHAR* UnreachableField = TEXT("unreachable");
	inline constexpr const TCHAR* DisabledField = TEXT("disabled");

	/**
	 * Stable identifiers and hints (card 0138, gap-5). A node's identity is its "type", its "class" (the UK2Node class,
	 * "K2Node_CallFunction") and, for a member, "member_parent" (the owning class or struct, a Blueprint class by its
	 * Blueprint's name) and "member_name" (the function, variable, event or macro as declared, "_C" and all). A pin's
	 * "name" is UEdGraphPin::PinName ("execute", "then", "ReturnValue"). None of these change with the editor language
	 * or preferences. Display text is only a hint: the node's "title" and the pin's "label", in the source (English)
	 * text whatever the editor language, and each written only when it says more than the identifier it labels (not
	 * when it is the identifier or the identifier spelled as a display name, see IsRedundantHint). v1 documents call
	 * the node title "name" (NodeNameFieldV1) and their pin "name" is display text; the reader keeps both as they are.
	 */
	inline constexpr const TCHAR* NodeClassField = TEXT("class");
	inline constexpr const TCHAR* NodeTitleField = TEXT("title");
	inline constexpr const TCHAR* NodeNameFieldV1 = TEXT("name");
	inline constexpr const TCHAR* PinNameField = TEXT("name");
	inline constexpr const TCHAR* PinLabelField = TEXT("label");

	/**
	 * True when a hint adds nothing to Identifier: it is empty, the identifier itself, or the identifier as the editor
	 * spells it for display (FName::NameToDisplayString, "ReturnValue" -> "Return Value", "bEnabled" -> "Enabled").
	 * UE 5.8 titles a function call by its name where 5.6 and 5.7 use that display spelling, and the friendly-names
	 * preference switches pin labels between the two, so neither is ever written as a hint.
	 */
	inline bool IsRedundantHint(const FString& Hint, const FString& Identifier)
	{
		return Hint.IsEmpty()
			|| Hint.Equals(Identifier, ESearchCase::CaseSensitive)
			|| Hint.Equals(FName::NameToDisplayString(Identifier, false), ESearchCase::CaseSensitive)
			|| Hint.Equals(FName::NameToDisplayString(Identifier, true), ESearchCase::CaseSensitive);
	}

	/**
	 * A graph's comment boxes (card 0235, additive): "comments" is an optional array after "flows", one object per
	 * comment box in graph order, {"text": the comment, "nodes": the IDs of the captured nodes inside the box}. "nodes"
	 * is left out when the box holds no captured node; "comments" is left out when the graph has no comment box.
	 */
	inline constexpr const TCHAR* CommentsField = TEXT("comments");
	inline constexpr const TCHAR* CommentTextField = TEXT("text");
	inline constexpr const TCHAR* CommentNodesField = TEXT("nodes");

	/**
	 * A map pin's value type (card 0238, additive): on a pin with "is_map", "type" and "sub_type" are the key's and
	 * "value_type" and "value_sub_type" the value's (FEdGraphPinType::PinValueType), in the same spellings. Written
	 * after the pin's flags, on map pins only; "value_sub_type" only when the value has a struct, enum, class or object.
	 */
	inline constexpr const TCHAR* PinValueTypeField = TEXT("value_type");
	inline constexpr const TCHAR* PinValueSubTypeField = TEXT("value_sub_type");

	/** Separates a node ID from a pin ID in a pin ref ("N3.P5") */
	inline constexpr const TCHAR* PinRefSeparator = TEXT(".");

	/** One end of an exec flow entry: a node ID and, in the pin form, a pin ID (empty in the node-only form) */
	struct FExecEndpoint
	{
		FString Node;
		FString Pin;
	};

	/** The exec flow entry from SourceNode.SourcePin to TargetNode.TargetPin ("N3.P5->N4.P1") */
	inline FString MakeExecEdge(const FString& SourceNode, const FString& SourcePin, const FString& TargetNode, const FString& TargetPin)
	{
		return SourceNode + PinRefSeparator + SourcePin + ExecEdgeSeparator + TargetNode + PinRefSeparator + TargetPin;
	}

	/**
	 * Splits an exec flow entry into its endpoints, in order: two for the pin form, two or more for the node-only form.
	 * Returns false when an endpoint is empty or has more than one '.', or there are fewer than two endpoints.
	 */
	inline bool ParseExecFlow(const FString& Flow, TArray<FExecEndpoint>& OutEndpoints)
	{
		OutEndpoints.Reset();
		TArray<FString> Parts;
		Flow.ParseIntoArray(Parts, ExecEdgeSeparator, false);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			FExecEndpoint& Endpoint = OutEndpoints.AddDefaulted_GetRef();
			if (!Part.Split(PinRefSeparator, &Endpoint.Node, &Endpoint.Pin))
			{
				Endpoint.Node = Part;
			}
			else if (Endpoint.Pin.IsEmpty() || Endpoint.Pin.Contains(PinRefSeparator))
			{
				return false;
			}
			if (Endpoint.Node.IsEmpty())
			{
				return false;
			}
		}
		return OutEndpoints.Num() >= 2;
	}

	/** The major of a version string ("2.0.0" -> 2), or INDEX_NONE when it does not start with a number */
	inline int32 ParseMajor(const FString& VersionString)
	{
		FString MajorText;
		if (!VersionString.Split(TEXT("."), &MajorText, nullptr))
		{
			MajorText = VersionString;
		}
		MajorText.TrimStartAndEndInline();
		return !MajorText.IsEmpty() && MajorText.IsNumeric() && !MajorText.Contains(TEXT("-")) && !MajorText.Contains(TEXT("."))
			? FCString::Atoi(*MajorText) : INDEX_NONE;
	}

	/**
	 * Reader policy (card 0445): FromJson reads a document of a readable major without dropping what it does not know,
	 * so a 2.0 build reads a later 2.x document (for example a material capture) and writes it back unchanged.
	 * - An unknown "blueprint_type", "graph_type", node "type" or pin "type" is read as the enum's Unknown value, its
	 *   text kept (UnknownBlueprintType, UnknownGraphType, UnknownType) and written back in place of "Unknown". The
	 *   document is still read; the node keeps its pins and the flows that name them.
	 * - A field this build does not know, at the top level or in "metadata", a graph, a node or a pin, is kept as
	 *   condensed JSON text (UnknownFields) and written back after the known fields of its object, in the order read.
	 * - A 2.x document keeps its own version string (a 2.1.0 document stays 2.1.0); 1.x is read into Version.
	 * - Each of these, and each graph, node or pin skipped because it is malformed (a missing required field), gives
	 *   one structured warning (FN2CJsonReadWarning: a code below, a path such as "graphs[0].nodes[2].type", the
	 *   value) and one log warning. Text that is not JSON, or JSON without a version, metadata or graphs, is still
	 *   rejected.
	 */
	namespace ReadWarning
	{
		inline constexpr const TCHAR* UnknownBlueprintType = TEXT("UNKNOWN_BLUEPRINT_TYPE");
		inline constexpr const TCHAR* UnknownGraphType = TEXT("UNKNOWN_GRAPH_TYPE");
		inline constexpr const TCHAR* UnknownNodeType = TEXT("UNKNOWN_NODE_TYPE");
		inline constexpr const TCHAR* UnknownPinType = TEXT("UNKNOWN_PIN_TYPE");
		inline constexpr const TCHAR* UnknownField = TEXT("UNKNOWN_FIELD");
		inline constexpr const TCHAR* NewerVersion = TEXT("NEWER_VERSION");
		inline constexpr const TCHAR* SkippedGraph = TEXT("SKIPPED_GRAPH");
		inline constexpr const TCHAR* SkippedNode = TEXT("SKIPPED_NODE");
		inline constexpr const TCHAR* SkippedPin = TEXT("SKIPPED_PIN");
		/** A "comments" entry that is not an object (card 0235); the graph and its other comments are kept */
		inline constexpr const TCHAR* SkippedComment = TEXT("SKIPPED_COMMENT");
	}

	/** True when FromJson reads this version (majors OldestReadableMajor..Major) */
	inline bool IsReadable(const FString& VersionString)
	{
		const int32 ParsedMajor = ParseMajor(VersionString);
		return ParsedMajor >= OldestReadableMajor && ParsedMajor <= Major;
	}
}
