// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolsets.h"

const FN2CMcpToolset* N2CMcpToolsets::Find(const FString& Name)
{
	for (const FN2CMcpToolset* Toolset : All)
	{
		if (Name.Equals(Toolset->Name, ESearchCase::IgnoreCase))
		{
			return Toolset;
		}
	}
	return nullptr;
}

TArray<FString> N2CMcpToolsets::AllNames()
{
	TArray<FString> Names;
	for (const FN2CMcpToolset* Toolset : All)
	{
		Names.Add(Toolset->Name);
	}
	return Names;
}
