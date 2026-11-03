// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Resources/N2CMcpResourceTypes.h"

/**
 * nodetocode://python/unreal-stub (card 0214): the engine's Python API stub for this project, the unreal.py that the
 * Python Editor Script Plugin writes to <Project>/Intermediate/PythonStub/unreal.py when its Developer Mode is on. An
 * agent reads it to look up the unreal module's classes and signatures for the editor it is talking to, instead of a
 * third-party copy. Read from disk on each request; when there is no stub the text says how to generate it.
 */
struct FN2CMcpPythonStubResource
{
	static constexpr const TCHAR* Uri = TEXT("nodetocode://python/unreal-stub");

	static FMcpResourceDefinition GetDefinition();

	/** <Project>/Intermediate/PythonStub/unreal.py, absolute */
	static FString StubPath();

	static FMcpResourceContents Read(const FString& RequestedUri);
};
