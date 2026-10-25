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

	/** True when FromJson reads this version (majors OldestReadableMajor..Major) */
	inline bool IsReadable(const FString& VersionString)
	{
		const int32 ParsedMajor = ParseMajor(VersionString);
		return ParsedMajor >= OldestReadableMajor && ParsedMajor <= Major;
	}
}
