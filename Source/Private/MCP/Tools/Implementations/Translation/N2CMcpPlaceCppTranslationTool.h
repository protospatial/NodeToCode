// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * place-cpp-translation (card 0314): turns a saved C++ translation run into a class a project module compiles, through
 * FN2CCppPlacement. dry_run (the default) returns the files, target paths, includes and modules and writes nothing;
 * dry_run false writes the header and .cpp into the module's Source folder and adds missing modules to its Build.cs,
 * backing up every file it replaces.
 */
class FN2CMcpPlaceCppTranslationTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * The run folder RunFolder names: a folder of the translation output directory (absolute, or relative to it), or the
	 * latest run when empty. Refuses anything outside the output directory.
	 */
	static bool ResolveRunFolder(const FString& RunFolder, FString& OutFolder, FString& OutError);
};
