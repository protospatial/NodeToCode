// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpFunctionGuidUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Misc/SecureHash.h"
#include "Kismet2/BlueprintEditorUtils.h"

FGuid FN2CMcpFunctionGuidUtils::GetFunctionId(const UEdGraph* FunctionGraph)
{
	if (!FunctionGraph)
	{
		return FGuid();
	}
	// UEdGraph::PostInitProperties gives every new graph a GUID and it is saved with the graph; a graph loaded from
	// an asset saved without one keeps the old scheme so it still has an ID
	return FunctionGraph->GraphGuid.IsValid() ? FunctionGraph->GraphGuid : GetLegacyFunctionId(FunctionGraph);
}

FGuid FN2CMcpFunctionGuidUtils::GetLegacyFunctionId(const UEdGraph* FunctionGraph)
{
	if (!FunctionGraph)
	{
		return FGuid();
	}
	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(FunctionGraph);
	return Blueprint ? GenerateDeterministicGuid(Blueprint, FunctionGraph->GetName()) : FGuid();
}

FGuid FN2CMcpFunctionGuidUtils::GenerateDeterministicGuid(const UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint || FunctionName.IsEmpty())
	{
		return FGuid();
	}

	// The pre-0152 scheme, kept byte for byte so old IDs still match: MD5 over the TCHAR bytes of "<path>::<name>"
	const FString UniqueString = FString::Printf(TEXT("%s::%s"), *Blueprint->GetPathName(), *FunctionName);
	FMD5 Md5Gen;
	Md5Gen.Update(reinterpret_cast<const uint8*>(*UniqueString), UniqueString.Len() * sizeof(TCHAR));
	uint8 Digest[16];
	Md5Gen.Final(Digest);

	uint32 Parts[4];
	FMemory::Memcpy(Parts, Digest, sizeof(Parts));
	return FGuid(Parts[0], Parts[1], Parts[2], Parts[3]);
}

UEdGraph* FN2CMcpFunctionGuidUtils::FindFunctionByGuid(UBlueprint* Blueprint, const FGuid& FunctionGuid, bool* bOutLegacy)
{
	if (bOutLegacy)
	{
		*bOutLegacy = false;
	}
	if (!Blueprint || !FunctionGuid.IsValid())
	{
		return nullptr;
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && GetFunctionId(Graph) == FunctionGuid)
		{
			return Graph;
		}
	}

	// Deprecated: an ID from before card 0152 (accepted until LegacyIdRemovalVersion)
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && GetLegacyFunctionId(Graph) == FunctionGuid)
		{
			if (bOutLegacy)
			{
				*bOutLegacy = true;
			}
			return Graph;
		}
	}
	return nullptr;
}

bool FN2CMcpFunctionGuidUtils::IsFunctionGraph(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
	return Blueprint && Graph && Blueprint->FunctionGraphs.Contains(Graph);
}
