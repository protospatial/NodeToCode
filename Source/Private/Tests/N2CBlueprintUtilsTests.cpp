// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CMarkModifiedDirtiesTest, "NodeToCode.Robustness.MarkModifiedDirties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CMarkModifiedDirtiesTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor"), GEditor))
	{
		return false;
	}

	// A /Game package (never saved), so MarkPackageDirty is not skipped the way it is for the transient package
	const FString PackageName = FString::Printf(TEXT("/Game/E05Probe/BP_N2CMarkDirty_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(*FPackageName::GetShortName(PackageName)),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Blueprint"), Blueprint))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		// Delete the probe Blueprint and leave its package clean, so nothing asks to save it
		Package->SetDirtyFlag(false);
		TestEqual(TEXT("probe Blueprint deleted"), ObjectTools::ForceDeleteObjects({ Blueprint }, /*ShowConfirmation*/ false), 1);
		Package->SetDirtyFlag(false);
	};

	// A function with a result node, compiled, as a saved Blueprint would be before the tool runs
	UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(TEXT("N2CProbeFunction")),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FunctionGraph, /*bIsUserCreated*/ true, nullptr);
	UK2Node_FunctionResult* FunctionResult = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(
		FN2CMcpFunctionPinUtils::FindFunctionEntryNode(FunctionGraph));
	if (!TestNotNull(TEXT("function result node"), FunctionResult))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	Package->SetDirtyFlag(false);
	TestFalse(TEXT("baseline: package clean"), Package->IsDirty());

	// The add-function-return-pin path: one tool call (card 0151) running FN2CMcpFunctionPinUtils::AddFunctionPin (its
	// own transaction, card 0156); the call compiles once after the transaction closes
	FN2CMcpToolCallScope CallScope;
	{
		FEdGraphPinType BoolType;
		BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		FN2CFunctionPinEdit Edit;
		if (!TestTrue(TEXT("return pin added: ") + Edit.Error, FN2CMcpFunctionPinUtils::AddFunctionPin(Blueprint, FunctionGraph,
			/*bReturnValue*/ true, TEXT("bOk"), BoolType, FString(), FString(),
			NSLOCTEXT("NodeToCode", "N2CMarkDirtyTest", "Add Return Pin (test)"), Edit)))
		{
			return false;
		}
	}
	const TArray<FN2CCompileDiagnostics> Compiles = CallScope.Flush();
	TestEqual(TEXT("the call compiled the Blueprint once"), Compiles.Num(), 1);

	TestTrue(TEXT("result node has the new pin"), FunctionResult->FindPin(FName(TEXT("bOk"))) != nullptr);
	TestTrue(TEXT("package dirty after MarkModified and Compile"), Blueprint->GetPackage()->IsDirty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
