// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestContentFolder.h"
#include "MCP/Tools/Implementations/ContentBrowser/N2CMcpOpenBlueprintAssetTool.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2COpenBlueprintAssetSpec
{
	/** A Widget Blueprint in its own /Game/N2CTest_<guid> package (never saved), released by ReleaseWidget */
	UBlueprint* MakeWidget(UPackage*& OutPackage)
	{
		OutPackage = CreatePackage(*FString::Printf(TEXT("/Game/N2CTest_%s/WBP_N2COpenAsset"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		return FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(), OutPackage, TEXT("WBP_N2COpenAsset"),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass());
	}

	void ReleaseWidget(UPackage* Package, UBlueprint* Widget)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetEditors && Widget)
		{
			AssetEditors->CloseAllEditorsForAsset(Widget);
		}
		if (Package)
		{
			Package->SetDirtyFlag(false);
			ForEachObjectWithPackage(Package, [](UObject* Object)
			{
				Object->ClearFlags(RF_Standalone | RF_Public);
				return true;
			}, N2CTest::TopLevelObjectsOnly);
		}
	}

	/** An unsaved, uncompiled Blueprint object of BlueprintClass with this parent and type, for the kind checks */
	UBlueprint* MakeBare(UClass* BlueprintClass, UClass* ParentClass, EBlueprintType Type)
	{
		UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage(), BlueprintClass, NAME_None, RF_Transient);
		Blueprint->ParentClass = ParentClass;
		Blueprint->BlueprintType = Type;
		return Blueprint;
	}

	int32 Count(const TSharedPtr<FJsonObject>& Result, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Counts = nullptr;
		return Result.IsValid() && Result->TryGetObjectField(TEXT("graph_counts"), Counts)
			? static_cast<int32>((*Counts)->GetNumberField(Field)) : -1;
	}
}

BEGIN_DEFINE_SPEC(FN2COpenBlueprintAssetSpec, "NodeToCode.MCP.OpenBlueprintAsset", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2COpenBlueprintAssetSpec)

void FN2COpenBlueprintAssetSpec::Define()
{
	using namespace N2COpenBlueprintAssetSpec;

	Describe(TEXT("asset_type"), [this]()
	{
		It(TEXT("names the Blueprint's kind from its asset and parent class, not Actor for every normal Blueprint"), [this]()
		{
			TestEqual(TEXT("Actor Blueprint"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UBlueprint::StaticClass(), AActor::StaticClass(), BPTYPE_Normal)), FString(TEXT("Actor")));
			TestEqual(TEXT("Widget Blueprint"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UWidgetBlueprint::StaticClass(), UUserWidget::StaticClass(), BPTYPE_Normal)), FString(TEXT("Widget")));
			TestEqual(TEXT("Anim Blueprint"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UAnimBlueprint::StaticClass(), UAnimInstance::StaticClass(), BPTYPE_Normal)), FString(TEXT("Animation")));
			TestEqual(TEXT("Actor Component Blueprint"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UBlueprint::StaticClass(), UActorComponent::StaticClass(), BPTYPE_Normal)), FString(TEXT("Component")));
			TestEqual(TEXT("Object Blueprint"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UBlueprint::StaticClass(), UObject::StaticClass(), BPTYPE_Normal)), FString(TEXT("Object")));
			TestEqual(TEXT("Blueprint Interface"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UBlueprint::StaticClass(), UInterface::StaticClass(), BPTYPE_Interface)), FString(TEXT("Interface")));
			TestEqual(TEXT("Function Library"), FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(
				MakeBare(UBlueprint::StaticClass(), UBlueprintFunctionLibrary::StaticClass(), BPTYPE_FunctionLibrary)), FString(TEXT("FunctionLibrary")));
		});
	});

	Describe(TEXT("graph_counts"), [this]()
	{
		It(TEXT("counts the event graph pages of an Actor and a Widget Blueprint"), [this]()
		{
			N2CTest::FToolSpecBlueprint Actor(TEXT("BP_N2COpenAsset"), TEXT("/Game"));
			const UEdGraph* Function = Actor.AddFunction(FName(TEXT("N2COpenAssetFunction")));
			const TSharedPtr<FJsonObject> ActorResult = FN2CMcpOpenBlueprintAssetTool::BuildSuccessResult(Actor.GetBlueprint(), Actor.Path(), FString());
			TestTrue(TEXT("the Actor Blueprint was built"), Function != nullptr && Actor.GetBlueprint()->UbergraphPages.Num() >= 1);
			TestEqual(TEXT("Actor event_graphs"), Count(ActorResult, TEXT("event_graphs")), Actor.GetBlueprint()->UbergraphPages.Num());
			TestTrue(TEXT("Actor event_graphs >= 1"), Count(ActorResult, TEXT("event_graphs")) >= 1);
			TestEqual(TEXT("Actor function_graphs"), Count(ActorResult, TEXT("function_graphs")), Actor.GetBlueprint()->FunctionGraphs.Num());
			TestEqual(TEXT("Actor asset_type"), ActorResult->GetStringField(TEXT("asset_type")), FString(TEXT("Actor")));

			UPackage* WidgetPackage = nullptr;
			UBlueprint* Widget = MakeWidget(WidgetPackage);
			ON_SCOPE_EXIT { ReleaseWidget(WidgetPackage, Widget); };
			if (!TestNotNull(TEXT("the Widget Blueprint"), Widget))
			{
				return;
			}
			const TSharedPtr<FJsonObject> WidgetResult = FN2CMcpOpenBlueprintAssetTool::BuildSuccessResult(Widget, Widget->GetPathName(), FString());
			TestTrue(TEXT("Widget event_graphs >= 1"), Count(WidgetResult, TEXT("event_graphs")) >= 1);
			TestEqual(TEXT("Widget event_graphs"), Count(WidgetResult, TEXT("event_graphs")), Widget->UbergraphPages.Num());
			TestEqual(TEXT("Widget asset_type"), WidgetResult->GetStringField(TEXT("asset_type")), FString(TEXT("Widget")));
		});
	});

	Describe(TEXT("the tool"), [this]()
	{
		It(TEXT("opens a Widget Blueprint on its EventGraph and reports a Widget with its event graph"), [this]()
		{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
			// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor's window (see N2CToolSpecHarness.h); the
			// result the tool builds is checked on every engine by the graph_counts and asset_type cases above.
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
#else
			UPackage* WidgetPackage = nullptr;
			UBlueprint* Widget = MakeWidget(WidgetPackage);
			ON_SCOPE_EXIT { ReleaseWidget(WidgetPackage, Widget); };
			if (!TestNotNull(TEXT("the Widget Blueprint"), Widget))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("asset_path"), Widget->GetPathName());
			Args->SetStringField(TEXT("focus_graph"), TEXT("eventgraph"));
			Args->SetBoolField(TEXT("bring_to_front"), false);
			const FMcpToolCallResult Result = N2CTest::RunToolUnmarked(TEXT("open-blueprint-asset"), Args);
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (!TestFalse(TEXT("the tool succeeds"), Result.bIsError) || !TestTrue(TEXT("a JSON result"), Json.IsValid()))
			{
				return;
			}
			TestEqual(TEXT("asset_type"), Json->GetStringField(TEXT("asset_type")), FString(TEXT("Widget")));
			TestEqual(TEXT("focused_graph (matched without case)"), Json->GetStringField(TEXT("focused_graph")), FString(TEXT("eventgraph")));
			TestTrue(TEXT("graph_counts.event_graphs >= 1"), Count(Json, TEXT("event_graphs")) >= 1);
#endif
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
