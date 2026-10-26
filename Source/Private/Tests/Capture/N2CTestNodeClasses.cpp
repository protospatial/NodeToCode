// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/Capture/N2CTestNodeClasses.h"

#include "EdGraphSchema_K2.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(N2CTestNodeClasses)

void UK2Node_N2CTestUnmapped::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	Super::AllocateDefaultPins();
}

int32 UK2Node_N2CTestTitleCounter::TitleCalls = 0;

FText UK2Node_N2CTestTitleCounter::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	++TitleCalls;
	return Super::GetNodeTitle(TitleType);
}
