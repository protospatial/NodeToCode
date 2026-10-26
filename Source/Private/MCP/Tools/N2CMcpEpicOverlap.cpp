// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpEpicOverlap.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersionComparison.h"

const TCHAR* N2CMcpEpicOverlap::FindEpicCounterpart(const FString& ToolName)
{
	for (const FOverlap& Overlap : Tools)
	{
		if (ToolName.Equals(Overlap.Tool, ESearchCase::CaseSensitive))
		{
			return Overlap.EpicCounterpart;
		}
	}
	return nullptr;
}

N2CMcpEpicOverlap::FEnvironment N2CMcpEpicOverlap::FEnvironment::Current()
{
	FEnvironment Environment;
	Environment.bEngineHasUnrealMcp = !UE_VERSION_OLDER_THAN(5, 8, 0);
	IPluginManager& Plugins = IPluginManager::Get();
	Environment.bUnrealMcpEnabled = Plugins.FindEnabledPlugin(UnrealMcpPlugin).IsValid();
	Environment.bEditorToolsetEnabled = Plugins.FindEnabledPlugin(EditorToolsetPlugin).IsValid();
	return Environment;
}

N2CMcpEpicOverlap::FDecision N2CMcpEpicOverlap::Decide(EN2CMcpOverlappingTools Setting, EN2CMcpToolProfile Profile,
	const FEnvironment& Environment, bool bDefault)
{
	FDecision Decision;
	if (!Environment.bEngineHasUnrealMcp)
	{
		Decision.Reason = TEXT("this engine has no Unreal MCP (UE 5.8 and later have it)");
		return Decision;
	}
	if (!Environment.bUnrealMcpEnabled || !Environment.bEditorToolsetEnabled)
	{
		TArray<FString> Missing;
		if (!Environment.bUnrealMcpEnabled)
		{
			Missing.Add(UnrealMcpPlugin);
		}
		if (!Environment.bEditorToolsetEnabled)
		{
			Missing.Add(EditorToolsetPlugin);
		}
		Decision.Reason = FString::Printf(TEXT("%s %s not enabled, so Unreal MCP does not serve them"),
			*FString::Join(Missing, TEXT(" and ")), Missing.Num() > 1 ? TEXT("are") : TEXT("is"));
		return Decision;
	}

	const FString Enabled = FString::Printf(TEXT("UE 5.8 with %s (Unreal MCP) and %s enabled"), UnrealMcpPlugin, EditorToolsetPlugin);
	switch (Setting)
	{
	case EN2CMcpOverlappingTools::AlwaysServe:
		Decision.Reason = Enabled + TEXT(", but Overlapping Tools is Always serve them");
		return Decision;
	case EN2CMcpOverlappingTools::LeaveToUnrealMcp:
		Decision.bLeaveToUnrealMcp = true;
		Decision.Reason = Enabled + TEXT(" and Overlapping Tools is Leave them to Unreal MCP");
		return Decision;
	case EN2CMcpOverlappingTools::Automatic:
	default:
		if (!bDefault)
		{
			Decision.Reason = Enabled + TEXT(", and NodeToCode's default serves them");
		}
		else if (Profile != EN2CMcpToolProfile::DefaultToolsets)
		{
			Decision.Reason = Enabled + TEXT(", but Tool Profile is not Default toolsets: the profile's toolsets are served as chosen");
		}
		else
		{
			Decision.bLeaveToUnrealMcp = true;
			Decision.Reason = Enabled + TEXT(" (Overlapping Tools: Automatic)");
		}
		return Decision;
	}
}
