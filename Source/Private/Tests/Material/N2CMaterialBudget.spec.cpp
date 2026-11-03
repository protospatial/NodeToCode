// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialFunction.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestMaterialFactory.h"
#include "Tests/N2CToolSpecHarness.h"

/**
 * The capture budget for large materials (card 0456). Capture runs on the game thread, so a production material with
 * hundreds or thousands of expressions and nested functions must not stall the editor, and the document must not flood
 * an agent's context.
 *
 * Fixture: a generated material of LeafCount Constant expressions summed by a balanced tree of Add expressions (about
 * 2,000 expressions, every one live) into a call of MF_N2CBudget_Outer, which calls MF_N2CBudget_Inner (In * 0.5):
 * a depth-2 function chain, captured with Max Translation Depth 2.
 *
 * Budget: CaptureBudgetSeconds (1 s) for N2C::CaptureMaterialAsset of the fixture, measured as the best of
 * MeasuredRuns captures after one warm-up; the spec fails above twice the budget. The document size is recorded (it
 * is far over get-material-n2cjson's MaxInlineTokens), and the tool must write it to a file without being asked.
 * The first capture (which also builds the material's editor graph) is held to the same limit. Each run logs one line
 * "N2C_MATERIAL_BUDGET engine=<x.y> expressions=<n> nodes=<n> first_capture_ms=<n> capture_ms=<n> json_bytes=<n>
 * tokens=<n>" for the record.
 */
BEGIN_DEFINE_SPEC(FN2CMaterialBudgetSpec, "NodeToCode.Material.Budget", N2C_TEST_FLAGS)

	static constexpr int32 LeafCount = 1000;
	static constexpr double CaptureBudgetSeconds = 1.0;
	static constexpr int32 MeasuredRuns = 3;

	/** MF_N2CBudget_Inner (In * 0.5 into Out), then MF_N2CBudget_Outer (In through a call of Inner into Out) */
	static UMaterialFunction* BuildFunctionChain(N2CTest::FTestMaterial& M)
	{
		UMaterialFunction* Inner = M.AddFunction(TEXT("MF_N2CBudget_Inner"));
		UMaterialFunction* Outer = M.AddFunction(TEXT("MF_N2CBudget_Outer"));
		if (!Inner || !Outer)
		{
			return nullptr;
		}
		UMaterialExpressionFunctionInput* InnerIn = M.Add<UMaterialExpressionFunctionInput>(Inner, -300, 0);
		UMaterialExpressionMultiply* Half = M.Add<UMaterialExpressionMultiply>(Inner, -150, 0);
		UMaterialExpressionFunctionOutput* InnerOut = M.Add<UMaterialExpressionFunctionOutput>(Inner, 0, 0);
		UMaterialExpressionFunctionInput* OuterIn = M.Add<UMaterialExpressionFunctionInput>(Outer, -300, 0);
		UMaterialExpressionMaterialFunctionCall* CallInner = M.Add<UMaterialExpressionMaterialFunctionCall>(Outer, -150, 0);
		UMaterialExpressionFunctionOutput* OuterOut = M.Add<UMaterialExpressionFunctionOutput>(Outer, 0, 0);
		if (!InnerIn || !Half || !InnerOut || !OuterIn || !CallInner || !OuterOut)
		{
			return nullptr;
		}
		InnerIn->InputName = TEXT("In");
		InnerIn->ConditionallyGenerateId(true);
		InnerOut->OutputName = TEXT("Out");
		InnerOut->ConditionallyGenerateId(true);
		Half->ConstB = 0.5f;
		M.Connect(InnerIn, 0, Half, TEXT("A"));
		M.Connect(Half, 0, InnerOut, TEXT("A"));

		OuterIn->InputName = TEXT("In");
		OuterIn->ConditionallyGenerateId(true);
		OuterOut->OutputName = TEXT("Out");
		OuterOut->ConditionallyGenerateId(true);
		CallInner->SetMaterialFunction(Inner);
		M.Connect(OuterIn, 0, CallInner, TEXT("In"));
		M.Connect(CallInner, 0, OuterOut, TEXT("A"));
		return Outer;
	}

	/** The fixture; OutExpressions is the material's own expression count */
	static UMaterial* BuildLargeMaterial(N2CTest::FTestMaterial& M, int32& OutExpressions)
	{
		OutExpressions = 0;
		UMaterialFunction* Outer = BuildFunctionChain(M);
		if (!Outer || !M.GetMaterial())
		{
			return nullptr;
		}
		TArray<UMaterialExpression*> Level;
		for (int32 Index = 0; Index < LeafCount; ++Index)
		{
			UMaterialExpressionConstant* Leaf = M.Add<UMaterialExpressionConstant>(nullptr, -4000, Index * 20);
			if (!Leaf)
			{
				return nullptr;
			}
			Leaf->R = static_cast<float>(Index % 7) * 0.01f;
			Level.Add(Leaf);
			++OutExpressions;
		}
		// A balanced tree of Adds, each summing two of the level below (an odd one is carried up as it is)
		int32 Column = 1;
		while (Level.Num() > 1)
		{
			TArray<UMaterialExpression*> Next;
			for (int32 Index = 0; Index + 1 < Level.Num(); Index += 2)
			{
				UMaterialExpressionAdd* Sum = M.Add<UMaterialExpressionAdd>(nullptr, -4000 + Column * 300, Index * 20);
				if (!Sum || !M.Connect(Level[Index], 0, Sum, TEXT("A")) || !M.Connect(Level[Index + 1], 0, Sum, TEXT("B")))
				{
					return nullptr;
				}
				Next.Add(Sum);
				++OutExpressions;
			}
			if (Level.Num() % 2 == 1)
			{
				Next.Add(Level.Last());
			}
			Level = MoveTemp(Next);
			++Column;
		}
		UMaterialExpressionMaterialFunctionCall* Call = M.Add<UMaterialExpressionMaterialFunctionCall>(nullptr, -200, 0);
		if (!Call)
		{
			return nullptr;
		}
		++OutExpressions;
		Call->SetMaterialFunction(Outer);
		if (!M.Connect(Level[0], 0, Call, TEXT("In")) || !M.ConnectToProperty(Call, 0, MP_BaseColor))
		{
			return nullptr;
		}
		return M.GetMaterial();
	}

	static FString EngineVersion()
	{
		return FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	}

END_DEFINE_SPEC(FN2CMaterialBudgetSpec)

void FN2CMaterialBudgetSpec::Define()
{
	It(TEXT("captures a 2,000-expression material with a depth-2 function chain within twice the 1 s budget"), [this]()
	{
		N2CTest::FTestMaterial M(TEXT("M_N2CBudget_Large"));
		int32 Expressions = 0;
		UMaterial* Material = BuildLargeMaterial(M, Expressions);
		if (!TestTrue(TEXT("the fixture builds: ") + FString::Join(M.Errors(), TEXT("; ")), Material != nullptr && M.Errors().Num() == 0))
		{
			return;
		}
		TestTrue(TEXT("about 2,000 expressions"), Expressions >= 1990);

		FN2CCaptureOptions Options;
		Options.Depth = 2;
		// The first capture also builds the material's editor graph and loads what it touches: recorded and held to
		// the same 2x limit; the budget itself is the best of the later runs
		const double ColdStart = FPlatformTime::Seconds();
		const TValueOrError<FN2CCapture, FString> WarmUp = N2C::CaptureMaterialAsset(*Material, Options);
		const double Cold = FPlatformTime::Seconds() - ColdStart;
		if (!TestTrue(TEXT("captured (") + (WarmUp.HasError() ? WarmUp.GetError() : FString()) + TEXT(")"), WarmUp.HasValue()))
		{
			return;
		}
		double Best = TNumericLimits<double>::Max();
		FString Json;
		int32 Nodes = 0;
		TArray<FString> Graphs;
		for (int32 Run = 0; Run < MeasuredRuns; ++Run)
		{
			const double Start = FPlatformTime::Seconds();
			const TValueOrError<FN2CCapture, FString> Result = N2C::CaptureMaterialAsset(*Material, Options);
			const double Seconds = FPlatformTime::Seconds() - Start;
			if (!TestTrue(TEXT("a measured capture"), Result.HasValue()))
			{
				return;
			}
			Best = FMath::Min(Best, Seconds);
			if (Run == 0)
			{
				Json = FN2CSerializer::ToJson(Result.GetValue().Blueprint, EN2CJsonStyle::Condensed);
				for (const FN2CGraph& Graph : Result.GetValue().Blueprint.Graphs)
				{
					Nodes += Graph.Nodes.Num();
					Graphs.Add(Graph.Name);
				}
				TestFalse(TEXT("no error diagnostic"), Result.GetValue().HasErrors());
			}
		}
		const int32 Tokens = N2CMcpBlueprintExport::EstimateTokens(Json);
		const FString Record = FString::Printf(TEXT("N2C_MATERIAL_BUDGET engine=%s expressions=%d nodes=%d first_capture_ms=%.0f capture_ms=%.0f json_bytes=%d tokens=%d"),
			*EngineVersion(), Expressions, Nodes, Cold * 1000.0, Best * 1000.0, FTCHARToUTF8(*Json).Length(), Tokens);
		AddInfo(Record);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Record);

		TestTrue(TEXT("the material and both functions are captured: ") + FString::Join(Graphs, TEXT(", ")),
			Graphs.Contains(TEXT("MF_N2CBudget_Outer")) && Graphs.Contains(TEXT("MF_N2CBudget_Inner")));
		TestTrue(TEXT("every expression is a node"), Nodes >= Expressions);
		TestTrue(FString::Printf(TEXT("capture takes %.0f ms, at most twice the %.0f ms budget"), Best * 1000.0, CaptureBudgetSeconds * 1000.0),
			Best <= 2.0 * CaptureBudgetSeconds);
		TestTrue(FString::Printf(TEXT("the first capture takes %.0f ms, also at most twice the budget"), Cold * 1000.0),
			Cold <= 2.0 * CaptureBudgetSeconds);
		TestTrue(TEXT("the document is over the inline limit (the next test's premise)"), Tokens > N2CMcpBlueprintExport::MaxInlineTokens);
	});

	It(TEXT("writes the large material to a file through get-material-n2cjson without being asked"), [this]()
	{
		N2CTest::FTestMaterial M(TEXT("M_N2CBudget_Tool"));
		int32 Expressions = 0;
		UMaterial* Material = BuildLargeMaterial(M, Expressions);
		if (!TestNotNull(TEXT("the fixture builds"), Material))
		{
			return;
		}
		N2CTest::FScratchDir Scratch(TEXT("N2CMaterialBudget"));
		N2CMcpBlueprintExport::SetExportDirectoryForTests(Scratch.Path);
		ON_SCOPE_EXIT { N2CMcpBlueprintExport::SetExportDirectoryForTests(FString()); };

		const TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Arguments->SetNumberField(TEXT("depth"), 2);
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-material-n2cjson"), Arguments);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		if (!TestTrue(TEXT("a file result, not the document inline"), !Result.bIsError && Json.IsValid() && Json->HasField(TEXT("path"))))
		{
			return;
		}
		TestEqual(TEXT("reason: size (toFile was not asked for)"), Json->GetStringField(TEXT("reason")), FString(TEXT("size")));
		TestTrue(TEXT("estimatedTokens over maxInlineTokens"),
			Json->GetNumberField(TEXT("estimatedTokens")) > Json->GetNumberField(TEXT("maxInlineTokens")));
		TestTrue(TEXT("nodeCount covers the expressions"), Json->GetNumberField(TEXT("nodeCount")) >= Expressions);
		TestTrue(TEXT("the file is in the export folder"), FPaths::IsUnderDirectory(Json->GetStringField(TEXT("path")), Scratch.Path)
			&& FPaths::FileExists(Json->GetStringField(TEXT("path"))));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
