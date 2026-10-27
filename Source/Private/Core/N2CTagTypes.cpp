// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CTagTypes.h"

FN2CTaggedBlueprintGraph FN2CGraphRef::MakeTaggedGraph(const FN2CTagEntry& Entry) const
{
	FN2CTaggedBlueprintGraph Tagged;
	Tagged.Tag = Entry.Tag;
	Tagged.Category = Entry.Category;
	Tagged.Description = Entry.Description;
	Tagged.GraphGuid = GraphGuid;
	Tagged.GraphName = GraphName;
	Tagged.OwningBlueprint = Blueprint;
	Tagged.Timestamp = Entry.Timestamp;
	return Tagged;
}

FN2CTagInfo FN2CTagInfo::FromTaggedGraph(const FN2CTaggedBlueprintGraph& TaggedGraph)
{
	FN2CTagInfo Info;
	Info.Tag = TaggedGraph.Tag;
	Info.Category = TaggedGraph.Category;
	Info.Description = TaggedGraph.Description;
	Info.GraphGuid = TaggedGraph.GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
	Info.GraphName = TaggedGraph.GraphName;
	Info.BlueprintPath = TaggedGraph.OwningBlueprint.ToString();
	Info.Timestamp = TaggedGraph.Timestamp;
	return Info;
}

FN2CTaggedBlueprintGraph FN2CTagInfo::ToTaggedGraph() const
{
	FN2CTaggedBlueprintGraph TaggedGraph;
	TaggedGraph.Tag = Tag;
	TaggedGraph.Category = Category;
	TaggedGraph.Description = Description;
	FGuid::Parse(GraphGuid, TaggedGraph.GraphGuid);
	TaggedGraph.GraphName = GraphName;
	TaggedGraph.OwningBlueprint = FSoftObjectPath(BlueprintPath);
	TaggedGraph.Timestamp = Timestamp;
	return TaggedGraph;
}
