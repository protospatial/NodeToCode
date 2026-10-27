// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Utils/Processors/N2CTimelineProcessor.h"

#include "Engine/Blueprint.h"
#include "Engine/TimelineTemplate.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_Timeline.h"
#include "Utils/N2CStableNames.h"

void FN2CTimelineProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    const UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node);
    if (!TimelineNode)
    {
        return;
    }
    OutNodeDef.MemberName = TimelineNode->TimelineName.ToString();
    UBlueprint* Blueprint = TimelineNode->GetBlueprint();
    if (!Blueprint)
    {
        return;
    }
    OutNodeDef.MemberParent = Blueprint->GetName();

    // The template the node plays (FBlueprintEditorUtils keeps one per timeline variable)
    const UTimelineTemplate* Template = Blueprint->FindTimelineTemplateByVariableName(TimelineNode->TimelineName);
    if (!Template)
    {
        return;
    }
    FN2CTimelineSummary& Summary = OutNodeDef.Timeline;
    Summary.bSet = true;
    Summary.Length = Template->TimelineLength;
    Summary.bLoop = Template->bLoop;
    Summary.bAutoPlay = Template->bAutoPlay;
    for (const FTTFloatTrack& Track : Template->FloatTracks)
    {
        Summary.Tracks.Add({ Track.GetTrackName().ToString(), TEXT("float") });
    }
    for (const FTTVectorTrack& Track : Template->VectorTracks)
    {
        Summary.Tracks.Add({ Track.GetTrackName().ToString(), TEXT("vector") });
    }
    for (const FTTLinearColorTrack& Track : Template->LinearColorTracks)
    {
        Summary.Tracks.Add({ Track.GetTrackName().ToString(), TEXT("color") });
    }
    for (const FTTEventTrack& Track : Template->EventTracks)
    {
        Summary.Tracks.Add({ Track.GetTrackName().ToString(), TEXT("event") });
    }
}

void FN2CAsyncTaskProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Cast<UK2Node_BaseAsyncTask>(Node))
    {
        return;
    }
    // ProxyFactoryClass and ProxyFactoryFunctionName are protected UPROPERTYs on every engine: read by reflection
    const UClass* NodeClass = Node->GetClass();
    if (const FObjectPropertyBase* ClassProperty = FindFProperty<FObjectPropertyBase>(NodeClass, TEXT("ProxyFactoryClass")))
    {
        if (const UClass* FactoryClass = Cast<UClass>(ClassProperty->GetObjectPropertyValue_InContainer(Node)))
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(FactoryClass);
        }
    }
    if (const FNameProperty* FunctionProperty = FindFProperty<FNameProperty>(NodeClass, TEXT("ProxyFactoryFunctionName")))
    {
        const FName FunctionName = FunctionProperty->GetPropertyValue_InContainer(Node);
        if (!FunctionName.IsNone())
        {
            OutNodeDef.MemberName = FunctionName.ToString();
        }
    }
}
