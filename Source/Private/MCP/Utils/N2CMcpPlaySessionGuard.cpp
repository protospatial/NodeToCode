// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpPlaySessionGuard.h"

#include "Editor.h"
#include "Utils/N2CLogger.h"

const TCHAR* const FN2CMcpPlaySessionGuard::ErrorCode = TEXT("PIE_ACTIVE");

const TCHAR* const FN2CMcpPlaySessionGuard::DescriptionNote =
	TEXT("Refused with PIE_ACTIVE while a Play In Editor session runs; stop PIE first.");

bool FN2CMcpPlaySessionGuard::IsPlaySessionActive()
{
	return GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress());
}

bool FN2CMcpPlaySessionGuard::IsRefusedDuringPlaySession(const FMcpToolDefinition& Definition)
{
	return !Definition.Annotations.IsReadOnly() && !Definition.bAllowedDuringPlaySession;
}

TOptional<FMcpToolCallResult> FN2CMcpPlaySessionGuard::CheckCall(const FMcpToolDefinition& Definition)
{
	if (!IsRefusedDuringPlaySession(Definition) || !IsPlaySessionActive())
	{
		return {};
	}

	FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Refused MCP tool %s: a Play In Editor session is running"), *Definition.Name), TEXT("MCP"));
	return FMcpToolCallResult::CreateErrorResult(FString::Printf(
		TEXT("%s: %s is refused while a Play In Editor (PIE) session is running. It can change a Blueprint, an asset or a ")
		TEXT("level, and doing that during PIE reinstances the running game's actors. Stop the PIE session (the Stop button ")
		TEXT("or Esc in the editor) and call it again; read-only tools keep working during PIE."),
		ErrorCode, *Definition.Name));
}

void FN2CMcpPlaySessionGuard::AddRuleToDescription(FMcpToolDefinition& Definition)
{
	if (!IsRefusedDuringPlaySession(Definition) || Definition.Description.Contains(DescriptionNote))
	{
		return;
	}
	Definition.Description = Definition.Description.IsEmpty()
		? FString(DescriptionNote)
		: FString::Printf(TEXT("%s %s"), *Definition.Description.TrimEnd(), DescriptionNote);
}
