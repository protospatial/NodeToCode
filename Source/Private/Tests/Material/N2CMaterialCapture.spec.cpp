// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestMaterialFactory.h"
#include "Core/N2CCapture.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Utils/N2CGraphUtils.h"

/**
 * Material capture (epic 0442, decision 0009). Card 0448: the FTestMaterial factory builds materials and material
 * functions in code, and N2C::CaptureGraph refuses a material graph (built as the Material Editor builds it) with
 * UNSUPPORTED_GRAPH_TYPE naming the graph class, as it refuses any other non-K2 graph, instead of "No nodes to capture".
 * Card 0449 turns material capture on through its own entry points (N2C::CaptureMaterial); CaptureGraph keeps refusing
 * a UMaterialGraph, since a material is captured from the asset, not from the editor graph.
 */
BEGIN_DEFINE_SPEC(FN2CMaterialCaptureSpec, "NodeToCode.Material.Capture", N2C_TEST_FLAGS)

	/** Reports the factory's failures; false when there were any */
	bool NoFactoryErrors(const N2CTest::FTestMaterial& Material)
	{
		for (const FString& Error : Material.Errors())
		{
			AddError(TEXT("FTestMaterial: ") + Error);
		}
		return Material.Errors().Num() == 0;
	}

	/** A constant 0.5 plus a constant 0.25 into Roughness, and a constant colour into BaseColor */
	void BuildConstantMath(N2CTest::FTestMaterial& Material)
	{
		UMaterialExpressionConstant* A = Material.Add<UMaterialExpressionConstant>(nullptr, -400, 0);
		UMaterialExpressionConstant* B = Material.Add<UMaterialExpressionConstant>(nullptr, -400, 100);
		UMaterialExpressionAdd* Sum = Material.Add<UMaterialExpressionAdd>(nullptr, -200, 50);
		UMaterialExpressionConstant3Vector* Colour = Material.Add<UMaterialExpressionConstant3Vector>(nullptr, -200, -100);
		if (A && B && Sum && Colour)
		{
			A->R = 0.5f;
			B->R = 0.25f;
			Colour->Constant = FLinearColor(0.8f, 0.2f, 0.1f);
			Material.Connect(A, 0, Sum, TEXT("A"));
			Material.Connect(B, 0, Sum, TEXT("B"));
			Material.ConnectToProperty(Sum, 0, MP_Roughness);
			Material.ConnectToProperty(Colour, 0, MP_BaseColor);
		}
	}

END_DEFINE_SPEC(FN2CMaterialCaptureSpec)

void FN2CMaterialCaptureSpec::Define()
{
	Describe(TEXT("Factory"), [this]()
	{
		It(TEXT("builds a material's expressions in creation order, wired by input property name, each with its own GUID"), [this]()
		{
			N2CTest::FTestMaterial Material(TEXT("M_N2CTest_Factory"));
			if (!TestNotNull(TEXT("the material"), Material.GetMaterial()))
			{
				return;
			}
			BuildConstantMath(Material);
			if (!NoFactoryErrors(Material))
			{
				return;
			}
			TestEqual(TEXT("named as asked"), Material.GetMaterial()->GetName(), FString(TEXT("M_N2CTest_Factory")));
			TestTrue(TEXT("in a /Game/N2CTest_ package"), Material.GetPackagePath().StartsWith(TEXT("/Game/N2CTest_")));
			TestTrue(TEXT("never to be saved (transient package)"), Material.GetPackage()->HasAnyFlags(RF_Transient));

			const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material.GetMaterial()->GetExpressions();
			if (!TestEqual(TEXT("four expressions"), Expressions.Num(), 4))
			{
				return;
			}
			TestTrue(TEXT("creation order: Constant, Constant, Add, Constant3Vector"),
				Expressions[0]->IsA<UMaterialExpressionConstant>() && Expressions[1]->IsA<UMaterialExpressionConstant>()
				&& Expressions[2]->IsA<UMaterialExpressionAdd>() && Expressions[3]->IsA<UMaterialExpressionConstant3Vector>());
			TSet<FGuid> Guids;
			for (const UMaterialExpression* Expression : Expressions)
			{
				TestTrue(TEXT("a GUID"), Expression->MaterialExpressionGuid.IsValid());
				TestTrue(TEXT("owned by the material"), Expression->Material == Material.GetMaterial());
				Guids.Add(Expression->MaterialExpressionGuid);
			}
			TestEqual(TEXT("four distinct GUIDs"), Guids.Num(), 4);

			const UMaterialExpressionAdd* Sum = Cast<UMaterialExpressionAdd>(Expressions[2]);
			TestTrue(TEXT("Add.A is the first constant"), Sum->A.Expression == Expressions[0]);
			TestTrue(TEXT("Add.B is the second constant"), Sum->B.Expression == Expressions[1]);
			TestTrue(TEXT("Roughness is the Add"), Material.GetMaterial()->GetExpressionInputForProperty(MP_Roughness)->Expression == Sum);
			TestTrue(TEXT("BaseColor is the colour"), Material.GetMaterial()->GetExpressionInputForProperty(MP_BaseColor)->Expression == Expressions[3]);

			// A display name or an unknown property is never an input identifier
			N2CTest::FTestMaterial Other(TEXT("M_N2CTest_Bad"));
			UMaterialExpressionConstant* C = Other.Add<UMaterialExpressionConstant>();
			UMaterialExpressionAdd* Add = Other.Add<UMaterialExpressionAdd>();
			TestFalse(TEXT("no input named \"Value\" on Add"), Other.Connect(C, 0, Add, TEXT("Value")));
			TestEqual(TEXT("the failure is kept"), Other.Errors().Num(), 1);
		});

		It(TEXT("builds a material function in the same package, its expressions in the function"), [this]()
		{
			N2CTest::FTestMaterial Material(TEXT("M_N2CTest_WithFunction"));
			UMaterialFunction* Function = Material.AddFunction(TEXT("MF_N2CTest_Half"));
			UMaterialExpressionConstant* Half = Material.Add<UMaterialExpressionConstant>(Function);
			UMaterialExpressionFunctionOutput* Output = Material.Add<UMaterialExpressionFunctionOutput>(Function);
			if (!NoFactoryErrors(Material) || !TestNotNull(TEXT("the function"), Function) || !Half || !Output)
			{
				return;
			}
			Half->R = 0.5f;
			Material.Connect(Half, 0, Output, TEXT("A"));
			NoFactoryErrors(Material);
			TestTrue(TEXT("the function is in the material's package"), Function->GetPackage() == Material.GetPackage());
			const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Function->GetExpressions();
			TestEqual(TEXT("two expressions in the function"), Expressions.Num(), 2);
			TestTrue(TEXT("in creation order"), Expressions.Num() == 2 && Expressions[0] == Half && Expressions[1] == Output);
			TestTrue(TEXT("owned by the function"), Half->Function == Function && Half->GetOuter() == Function);
			TestEqual(TEXT("none in the material"), Material.GetMaterial()->GetExpressions().Num(), 0);
			TestTrue(TEXT("the output is wired"), Output->A.Expression == Half);
		});
	});

	Describe(TEXT("Refusal"), [this]()
	{
		It(TEXT("refuses a material graph built as the Material Editor builds it with UNSUPPORTED_GRAPH_TYPE naming the graph class"), [this]()
		{
			N2CTest::FTestMaterial Material(TEXT("M_N2CTest_Refusal"));
			BuildConstantMath(Material);
			UMaterialGraph* Graph = Material.BuildGraph();
			if (!NoFactoryErrors(Material) || !TestNotNull(TEXT("the material graph"), Graph))
			{
				return;
			}
			// The editor graph holds the root node and one node per expression: it is not empty
			TestTrue(TEXT("the graph has the root and the four expression nodes"), Graph->Nodes.Num() >= 5);

			const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Graph);
			if (!TestTrue(TEXT("CaptureGraph refuses it"), Capture.HasError()))
			{
				return;
			}
			const FString& Error = Capture.GetError();
			TestTrue(TEXT("UNSUPPORTED_GRAPH_TYPE (got: ") + Error + TEXT(")"), Error.StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE: ")));
			TestTrue(TEXT("names the graph class: ") + Error, Error.Contains(TEXT("MaterialGraph (schema MaterialGraphSchema)")));
			TestTrue(TEXT("names the graph: ") + Error, Error.Contains(Graph->GetName()));
			TestFalse(TEXT("not \"No nodes to capture\""), Error.Contains(TEXT("No nodes")));

			const FN2CGraphTypeInfo Info = FN2CGraphUtils::ClassifyGraphType(Graph);
			TestFalse(TEXT("the classifier refuses it"), Info.bSupported);
			TestEqual(TEXT("as another editor's graph"), Info.Kind, FString(TEXT("other")));
			TestTrue(TEXT("CaptureGraphs refuses it too"), N2C::CaptureGraphs({ Graph }).HasError());
		});

		It(TEXT("refuses any other non-K2 graph the same way"), [this]()
		{
			N2CTest::FTestMaterial Holder(TEXT("M_N2CTest_Holder"));
			UEdGraph* Graph = NewObject<UEdGraph>(Holder.GetPackage(), TEXT("PlainGraph"), RF_Transactional);
			if (!TestNotNull(TEXT("a graph"), Graph))
			{
				return;
			}
			Graph->Schema = UEdGraphSchema::StaticClass();
			const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Graph);
			TestTrue(TEXT("UNSUPPORTED_GRAPH_TYPE naming the class and schema"), Capture.HasError()
				&& Capture.GetError().StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE: "))
				&& Capture.GetError().Contains(TEXT("EdGraph (schema EdGraphSchema)")));
		});
	});

	Describe(TEXT("Hygiene"), [this]()
	{
		It(TEXT("leaves no test package, material, function or graph once garbage is collected"), [this]()
		{
			FString PackageName;
			TWeakObjectPtr<UMaterial> WeakMaterial;
			TWeakObjectPtr<UMaterialFunction> WeakFunction;
			TWeakObjectPtr<UMaterialGraph> WeakGraph;
			{
				N2CTest::FTestMaterial Material(TEXT("M_N2CTest_Hygiene"));
				BuildConstantMath(Material);
				UMaterialFunction* Function = Material.AddFunction(TEXT("MF_N2CTest_Hygiene"));
				Material.Add<UMaterialExpressionConstant>(Function);
				WeakGraph = Material.BuildGraph();
				NoFactoryErrors(Material);
				WeakMaterial = Material.GetMaterial();
				WeakFunction = Function;
				PackageName = Material.GetPackagePath();
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestFalse(TEXT("the material is collected"), WeakMaterial.IsValid());
			TestFalse(TEXT("the function is collected"), WeakFunction.IsValid());
			TestFalse(TEXT("the graph is collected"), WeakGraph.IsValid());
			TestNull(TEXT("the test package after garbage collection"), FindObject<UPackage>(nullptr, *PackageName));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
