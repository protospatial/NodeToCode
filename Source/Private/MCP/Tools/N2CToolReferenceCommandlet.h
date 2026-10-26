// Copyright (c) 2026 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "N2CToolReferenceCommandlet.generated.h"

/**
 * Writes the MCP tool reference (N2CMcpToolReference, card 0179) from the tool registry:
 *
 *   UnrealEditor-Cmd.exe <project>.uproject -run=NodeToCode.N2CToolReference -unattended -nullrhi [-Output=<folder>] [-Check]
 *
 * The "NodeToCode." prefix is needed: the engine looks the commandlet class up before it loads PostEngineInit plugin
 * modules, and a "<Module>." prefix makes it load that module first. A bare -run=N2CToolReference is not found.
 *
 * -Output   folder for tools.generated.json and tools.generated.md (default: Context/mcp-server of the checkout the
 *           plugin's Source folder belongs to, see N2CMcpToolReference::DefaultOutputDir)
 * -Check    write nothing; exit 1 when either file is missing or differs from what the registry builds
 *
 * Exits 0 on success, 1 on a failed check, 2 when it cannot write or finds no tools. Scripts/generate_tool_reference.py
 * runs it.
 */
UCLASS()
class UN2CToolReferenceCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UN2CToolReferenceCommandlet();

	virtual int32 Main(const FString& Params) override;
};
