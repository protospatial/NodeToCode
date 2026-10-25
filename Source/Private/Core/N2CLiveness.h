// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

struct FN2CGraph;

namespace N2CLiveness
{
	/**
	 * Marks the nodes of a captured graph that Blueprint never runs (card 0137, issue #13; the "unused" and
	 * "unreachable" flags of N2CJsonContract). Works on the graph's own nodes and flows, so it also serves a document
	 * read from JSON. Nodes already marked disabled are left as they are and never make another node live.
	 *
	 * - Entries are impure nodes (with exec pins) that have exec outputs and no exec input: events, custom events,
	 *   input events, function entries, tunnel entries.
	 * - Reachable: impure nodes the entries reach through flows.execution. A disabled node is not reachable, but exec
	 *   passes through one that has exactly one exec input and one exec output (UK2Node::GetPassThroughPin), so a
	 *   document written with the disabled node still in the exec chain reads as the compiler sees it.
	 * - Live data: walking upstream through flows.data from every reachable impure node, and from FunctionResult and
	 *   Tunnel nodes without exec pins (the outputs of a pure function or pure macro), through pure nodes only.
	 * - "unreachable": an impure node that is neither an entry nor reachable. "unused": a pure node (no exec pins) the
	 *   walk never reaches, other than a FunctionEntry, FunctionResult or Tunnel node.
	 *
	 * Clears and recomputes bUnused and bUnreachable on every node.
	 */
	void MarkGraph(FN2CGraph& Graph);
}
