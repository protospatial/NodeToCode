// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestMaterialFactory.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "MaterialEditor/PreviewMaterial.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionComposite.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionPinBase.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"
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
 * Card 0449 turns material capture on through its own entry points (N2C::CaptureMaterial, CaptureMaterialFunction,
 * CaptureMaterialAsset); CaptureGraph keeps refusing a UMaterialGraph, since a material is captured from the asset, not
 * from the editor graph. "Golden" compares each case's capture with Goldens/Material/<case>.json (package path and GUIDs
 * normalised, the capture's warnings listed) and round-trips it; "Asset" checks the dispatch and its refusals.
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


	/** One golden case: Build fills the material (and functions) and returns the asset to capture */
	struct FMaterialCase
	{
		FString Name;
		FString Description;
		TFunction<UObject*(N2CTest::FTestMaterial&)> Build;
		int32 Depth = 0;
	};
	TArray<FMaterialCase> Cases;

	/** A function MF_N2CGolden_Half: input "In" times 0.5 into output "Out" */
	static UMaterialFunction* BuildHalfFunction(N2CTest::FTestMaterial& M)
	{
		UMaterialFunction* Function = M.AddFunction(TEXT("MF_N2CGolden_Half"));
		UMaterialExpressionFunctionInput* In = M.Add<UMaterialExpressionFunctionInput>(Function, -300, 0);
		UMaterialExpressionMultiply* Half = M.Add<UMaterialExpressionMultiply>(Function, -150, 0);
		UMaterialExpressionFunctionOutput* Out = M.Add<UMaterialExpressionFunctionOutput>(Function, 0, 0);
		if (!Function || !In || !Half || !Out)
		{
			return nullptr;
		}
		In->InputName = TEXT("In");
		In->ConditionallyGenerateId(true);
		Out->OutputName = TEXT("Out");
		Out->ConditionallyGenerateId(true);
		Half->ConstB = 0.5f;
		M.Connect(In, 0, Half, TEXT("A"));
		M.Connect(Half, 0, Out, TEXT("A"));
		return Function;
	}

	/** A material whose BaseColor is MF_N2CGolden_Half of a constant colour */
	static UObject* BuildFunctionCall(N2CTest::FTestMaterial& M)
	{
		UMaterialFunction* Function = BuildHalfFunction(M);
		UMaterialExpressionConstant3Vector* Colour = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -400, 0);
		UMaterialExpressionMaterialFunctionCall* Call = M.Add<UMaterialExpressionMaterialFunctionCall>(nullptr, -200, 0);
		if (!Function || !Colour || !Call)
		{
			return nullptr;
		}
		Colour->Constant = FLinearColor(1.0f, 0.5f, 0.25f);
		Call->SetMaterialFunction(Function);
		M.Connect(Colour, 0, Call, TEXT("In"));
		M.ConnectToProperty(Call, 0, MP_BaseColor);
		return M.GetMaterial();
	}

	/** The engine's CameraDepthFade material function (Engine_MaterialFunctions01/Opacity; the same asset on 5.6-5.8) */
	static UMaterialFunction* LoadEngineCameraDepthFade()
	{
		return LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions01/Opacity/CameraDepthFade.CameraDepthFade"));
	}

	/** A translucent material whose Opacity is CameraDepthFade's first output */
	static UObject* BuildEngineFunctionCall(N2CTest::FTestMaterial& M)
	{
		UMaterialFunction* Function = LoadEngineCameraDepthFade();
		UMaterialExpressionMaterialFunctionCall* Call = M.Add<UMaterialExpressionMaterialFunctionCall>(nullptr, -200, 0);
		if (!Function || !Call || !M.GetMaterial())
		{
			return nullptr;
		}
		M.GetMaterial()->BlendMode = BLEND_Translucent;
		Call->SetMaterialFunction(Function);
		M.ConnectToProperty(Call, 0, MP_Opacity);
		return M.GetMaterial();
	}

	/** Captures Case's asset; the capture's warnings go to OutWarnings */
	TSharedPtr<FJsonObject> CaptureCase(const FMaterialCase& Case, FString& OutJson, TArray<FN2CDiagnostic>& OutWarnings)
	{
		N2CTest::FTestMaterial Material(FString::Printf(TEXT("M_N2CGolden_%s"), *Case.Name));
		UObject* Asset = Case.Build(Material);
		if (!NoFactoryErrors(Material) || !TestNotNull(Case.Name + TEXT(": the asset"), Asset))
		{
			return nullptr;
		}
		FN2CCaptureOptions Options;
		Options.Depth = Case.Depth;
		const TValueOrError<FN2CCapture, FString> Result = N2C::CaptureMaterialAsset(*Asset, Options);
		if (!TestTrue(Case.Name + TEXT(": captured (") + (Result.HasError() ? Result.GetError() : FString()) + TEXT(")"), Result.HasValue()))
		{
			return nullptr;
		}
		TestFalse(Case.Name + TEXT(": no error diagnostic"), Result.GetValue().HasErrors());
		OutWarnings = Result.GetValue().Diagnostics;
		const FString Raw = FN2CSerializer::ToJson(Result.GetValue().Blueprint, EN2CJsonStyle::Pretty);
		const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(N2CTest::NormaliseCapture(Raw, Material.GetPackagePath()));
		if (TestTrue(Case.Name + TEXT(": ToJson parses"), Parsed.IsValid()))
		{
			OutJson = N2CTest::PrettyJson(Parsed);
		}
		return Parsed;
	}

	/** The golden file: the case's name, description, the capture's warning codes and the document */
	static TSharedPtr<FJsonObject> GoldenObject(const FMaterialCase& Case, const TSharedPtr<FJsonObject>& Capture,
		const TArray<FN2CDiagnostic>& Warnings)
	{
		const TSharedPtr<FJsonObject> Golden = MakeShared<FJsonObject>();
		Golden->SetStringField(TEXT("case"), Case.Name);
		Golden->SetStringField(TEXT("description"), Case.Description);
		Golden->SetNumberField(TEXT("depth"), Case.Depth);
		TArray<TSharedPtr<FJsonValue>> Codes;
		for (const FN2CDiagnostic& Warning : Warnings)
		{
			Codes.Add(MakeShared<FJsonValueString>(Warning.Code + TEXT(" ") + Warning.Path));
		}
		Golden->SetArrayField(TEXT("warnings"), Codes);
		Golden->SetObjectField(TEXT("capture"), Capture);
		return Golden;
	}

	void RunGolden(const FMaterialCase& Case)
	{
		FString Json;
		TArray<FN2CDiagnostic> Warnings;
		const TSharedPtr<FJsonObject> Capture = CaptureCase(Case, Json, Warnings);
		if (!Capture.IsValid())
		{
			return;
		}
		const FString Actual = N2CTest::PrettyJson(GoldenObject(Case, Capture, Warnings));
		// A per-engine golden, <case>.<major>.<minor>.json, wins where the engine's own output differs: on UE 5.6 a Vector
		// Parameter has no RGBA output (added in 5.7)
		const FString Dir = N2CTest::GoldensDir(TEXT("Material"));
		const FString EngineGolden = Dir / FString::Printf(TEXT("%s.%d.%d.json"), *Case.Name, ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
		const FString GoldenPath = FPaths::FileExists(EngineGolden) ? EngineGolden : Dir / (Case.Name + TEXT(".json"));
		if (N2CTest::UpdateGoldens())
		{
			TestTrue(TEXT("golden written: ") + GoldenPath, FFileHelper::SaveStringToFile(Actual, *GoldenPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			return;
		}
		FString Expected;
		if (!TestTrue(TEXT("golden exists (build.ps1 -Test -UpdateGoldens writes it): ") + GoldenPath,
			FFileHelper::LoadFileToString(Expected, *GoldenPath)))
		{
			return;
		}
		TestEqual(Case.Name + TEXT(": the capture matches its golden"), Actual, Expected.Replace(TEXT("\r\n"), TEXT("\n")));
	}

	void RunRoundTrip(const FMaterialCase& Case)
	{
		FString Json;
		TArray<FN2CDiagnostic> Warnings;
		if (!CaptureCase(Case, Json, Warnings).IsValid())
		{
			return;
		}
		FN2CBlueprint Read;
		TArray<FN2CJsonReadWarning> ReadWarnings;
		if (TestTrue(Case.Name + TEXT(": FromJson reads it"), FN2CSerializer::FromJson(Json, Read, ReadWarnings)))
		{
			TestEqual(Case.Name + TEXT(": read without warnings"), ReadWarnings.Num(), 0);
			const TSharedPtr<FJsonObject> Again = N2CTest::ParseJsonObject(FN2CSerializer::ToJson(Read, EN2CJsonStyle::Pretty));
			TestEqual(Case.Name + TEXT(": ToJson -> FromJson -> ToJson is unchanged"), Again.IsValid() ? N2CTest::PrettyJson(Again) : FString(), Json);
		}
	}

	void DefineCases()
	{
		Cases.Add({ TEXT("ConstantMath"), TEXT("A constant colour times a constant into BaseColor; 0.5 plus 0.25 into Roughness"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant3Vector* Colour = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -400, -100);
				UMaterialExpressionConstant* Scale = M.Add<UMaterialExpressionConstant>(nullptr, -400, 0);
				UMaterialExpressionMultiply* Tinted = M.Add<UMaterialExpressionMultiply>(nullptr, -200, -50);
				UMaterialExpressionConstant* A = M.Add<UMaterialExpressionConstant>(nullptr, -400, 100);
				UMaterialExpressionAdd* Sum = M.Add<UMaterialExpressionAdd>(nullptr, -200, 100);
				if (!Colour || !Scale || !Tinted || !A || !Sum)
				{
					return nullptr;
				}
				Colour->Constant = FLinearColor(0.8f, 0.2f, 0.1f);
				Scale->R = 2.0f;
				A->R = 0.5f;
				Sum->ConstB = 0.25f; // B unwired: its value is ConstB
				M.Connect(Colour, 0, Tinted, TEXT("A"));
				M.Connect(Scale, 0, Tinted, TEXT("B"));
				M.Connect(A, 0, Sum, TEXT("A"));
				M.ConnectToProperty(Tinted, 0, MP_BaseColor);
				M.ConnectToProperty(Sum, 0, MP_Roughness);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("FanOut"), TEXT("One scalar parameter feeds three inputs: Roughness, a Multiply into Metallic and a Lerp's alpha into BaseColor"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionScalarParameter* Amount = M.Add<UMaterialExpressionScalarParameter>(nullptr, -400, 0);
				UMaterialExpressionMultiply* Twice = M.Add<UMaterialExpressionMultiply>(nullptr, -200, 100);
				UMaterialExpressionLinearInterpolate* Blend = M.Add<UMaterialExpressionLinearInterpolate>(nullptr, -200, -100);
				if (!Amount || !Twice || !Blend)
				{
					return nullptr;
				}
				Amount->ParameterName = TEXT("Amount");
				Amount->DefaultValue = 0.4f;
				Twice->ConstB = 2.0f;
				M.Connect(Amount, 0, Twice, TEXT("A"));
				M.Connect(Amount, 0, Blend, TEXT("Alpha"));
				M.ConnectToProperty(Amount, 0, MP_Roughness);
				M.ConnectToProperty(Twice, 0, MP_Metallic);
				M.ConnectToProperty(Blend, 0, MP_BaseColor);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("Reroutes"), TEXT("A constant through a reroute and a named reroute (declaration and usage) into BaseColor; the reroutes are traced, not captured"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant3Vector* Colour = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -600, 0);
				UMaterialExpressionReroute* Reroute = M.Add<UMaterialExpressionReroute>(nullptr, -450, 0);
				UMaterialExpressionNamedRerouteDeclaration* Declaration = M.Add<UMaterialExpressionNamedRerouteDeclaration>(nullptr, -300, 0);
				UMaterialExpressionNamedRerouteUsage* Usage = M.Add<UMaterialExpressionNamedRerouteUsage>(nullptr, -150, 0);
				UMaterialExpressionOneMinus* Invert = M.Add<UMaterialExpressionOneMinus>(nullptr, -50, 0);
				if (!Colour || !Reroute || !Declaration || !Usage || !Invert)
				{
					return nullptr;
				}
				Colour->Constant = FLinearColor(0.1f, 0.6f, 0.3f);
				Declaration->Name = TEXT("Tint");
				Usage->Declaration = Declaration;
				M.Connect(Colour, 0, Reroute, TEXT("Input"));
				M.Connect(Reroute, 0, Declaration, TEXT("Input"));
				M.Connect(Usage, 0, Invert, TEXT("Input"));
				M.ConnectToProperty(Invert, 0, MP_BaseColor);
				M.ConnectToProperty(Usage, 0, MP_EmissiveColor);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("Parameters"), TEXT("A texture sample parameter's RGB times a vector parameter into BaseColor; a scalar parameter into Roughness"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionTextureSampleParameter2D* Albedo = M.Add<UMaterialExpressionTextureSampleParameter2D>(nullptr, -500, -100);
				UMaterialExpressionVectorParameter* Tint = M.Add<UMaterialExpressionVectorParameter>(nullptr, -500, 100);
				UMaterialExpressionMultiply* Tinted = M.Add<UMaterialExpressionMultiply>(nullptr, -250, 0);
				UMaterialExpressionScalarParameter* Rough = M.Add<UMaterialExpressionScalarParameter>(nullptr, -250, 200);
				if (!Albedo || !Tint || !Tinted || !Rough)
				{
					return nullptr;
				}
				Albedo->ParameterName = TEXT("Albedo");
				Tint->ParameterName = TEXT("Tint");
				Tint->DefaultValue = FLinearColor(1.0f, 0.9f, 0.8f);
				Rough->ParameterName = TEXT("Roughness");
				Rough->DefaultValue = 0.3f;
				M.Connect(Albedo, 0, Tinted, TEXT("A"));
				M.Connect(Tint, 0, Tinted, TEXT("B"));
				M.ConnectToProperty(Tinted, 0, MP_BaseColor);
				M.ConnectToProperty(Rough, 0, MP_Roughness);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("Comment"), TEXT("A comment box around two expressions, one of them with a node comment"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant* A = M.Add<UMaterialExpressionConstant>(nullptr, -400, 0);
				UMaterialExpressionOneMinus* Invert = M.Add<UMaterialExpressionOneMinus>(nullptr, -200, 0);
				UMaterialExpressionConstant* Outside = M.Add<UMaterialExpressionConstant>(nullptr, -400, 600);
				UMaterialExpressionComment* Comment = M.AddComment(TEXT("Roughness from a constant"), -450, -50, 400, 200);
				if (!A || !Invert || !Outside || !Comment)
				{
					return nullptr;
				}
				A->R = 0.2f;
				Invert->Desc = TEXT("Invert the constant");
				Outside->R = 1.0f;
				M.Connect(A, 0, Invert, TEXT("Input"));
				M.ConnectToProperty(Invert, 0, MP_Roughness);
				M.ConnectToProperty(Outside, 0, MP_Metallic);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("UnusedAndDangling"), TEXT("An Add nothing reads (unused, with the constant it reads), an expression with no wires, and an input wired to an expression of another material (DANGLING_INPUT)"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant* Used = M.Add<UMaterialExpressionConstant>(nullptr, -200, 0);
				UMaterialExpressionConstant* Feeds = M.Add<UMaterialExpressionConstant>(nullptr, -400, 200);
				UMaterialExpressionAdd* Orphan = M.Add<UMaterialExpressionAdd>(nullptr, -200, 200);
				UMaterialExpressionConstant* Alone = M.Add<UMaterialExpressionConstant>(nullptr, -200, 400);
				UMaterialExpressionOneMinus* Invert = M.Add<UMaterialExpressionOneMinus>(nullptr, -200, 600);
				// An expression of another (transient) material, as a copy-paste between materials can leave behind
				UMaterialExpressionConstant* Foreign = NewObject<UMaterialExpressionConstant>(GetTransientPackage());
				if (!Used || !Feeds || !Orphan || !Alone || !Invert || !Foreign)
				{
					return nullptr;
				}
				Used->R = 0.7f;
				Feeds->R = 3.0f;
				Alone->R = 9.0f;
				M.Connect(Feeds, 0, Orphan, TEXT("A"));
				M.Connect(Foreign, 0, Invert, TEXT("Input"));
				M.ConnectToProperty(Used, 0, MP_Roughness);
				M.ConnectToProperty(Invert, 0, MP_Metallic);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("DuplicateGuid"), TEXT("Two constants share a MaterialExpressionGuid (a paste inside a composite keeps it): the capture derives a GUID for the second and warns DUPLICATE_EXPRESSION_GUID"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant* A = M.Add<UMaterialExpressionConstant>(nullptr, -400, 0);
				UMaterialExpressionConstant* B = M.Add<UMaterialExpressionConstant>(nullptr, -400, 100);
				UMaterialExpressionAdd* Sum = M.Add<UMaterialExpressionAdd>(nullptr, -200, 50);
				if (!A || !B || !Sum)
				{
					return nullptr;
				}
				A->R = 0.1f;
				B->R = 0.2f;
				B->MaterialExpressionGuid = A->MaterialExpressionGuid;
				M.Connect(A, 0, Sum, TEXT("A"));
				M.Connect(B, 0, Sum, TEXT("B"));
				M.ConnectToProperty(Sum, 0, MP_Roughness);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("StaticSwitch"), TEXT("A static switch parameter between two colours: both branches are kept and live"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant3Vector* Red = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -400, -100);
				UMaterialExpressionConstant3Vector* Blue = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -400, 100);
				UMaterialExpressionStaticSwitchParameter* Switch = M.Add<UMaterialExpressionStaticSwitchParameter>(nullptr, -200, 0);
				if (!Red || !Blue || !Switch)
				{
					return nullptr;
				}
				Red->Constant = FLinearColor(1.0f, 0.0f, 0.0f);
				Blue->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
				Switch->ParameterName = TEXT("UseRed");
				M.Connect(Red, 0, Switch, TEXT("A"));
				M.Connect(Blue, 0, Switch, TEXT("B"));
				M.ConnectToProperty(Switch, 0, MP_BaseColor);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("FunctionCall_Depth0"), TEXT("A call to a material function at depth 0: the call is described by name, the function's graph is left out (DEPTH_LIMIT)"),
			[](N2CTest::FTestMaterial& M) -> UObject* { return BuildFunctionCall(M); }, 0 });
		Cases.Add({ TEXT("FunctionCall_Depth1"), TEXT("The same call at depth 1: the function's graph follows the material's"),
			[](N2CTest::FTestMaterial& M) -> UObject* { return BuildFunctionCall(M); }, 1 });
		Cases.Add({ TEXT("MaterialFunction"), TEXT("A material function captured as the asset: graph_type MaterialFunction, FunctionOutput is the boundary"),
			[](N2CTest::FTestMaterial& M) -> UObject* { return BuildHalfFunction(M); } });
		// Card 0450
		Cases.Add({ TEXT("MaterialAttributes"), TEXT("Make Material Attributes into Set Material Attributes (adding Metallic by attribute GUID) into Break Material Attributes, whose BaseColor and Metallic feed the material: attribute pins are named by attribute"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant3Vector* Colour = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -800, -100);
				UMaterialExpressionConstant* Rough = M.Add<UMaterialExpressionConstant>(nullptr, -800, 0);
				UMaterialExpressionMakeMaterialAttributes* Make = M.Add<UMaterialExpressionMakeMaterialAttributes>(nullptr, -600, 0);
				UMaterialExpressionConstant* Metal = M.Add<UMaterialExpressionConstant>(nullptr, -600, 300);
				UMaterialExpressionSetMaterialAttributes* Set = M.Add<UMaterialExpressionSetMaterialAttributes>(nullptr, -400, 0);
				UMaterialExpressionBreakMaterialAttributes* Break = M.Add<UMaterialExpressionBreakMaterialAttributes>(nullptr, -200, 0);
				if (!Colour || !Rough || !Make || !Metal || !Set || !Break)
				{
					return nullptr;
				}
				Colour->Constant = FLinearColor(0.5f, 0.5f, 0.5f);
				Rough->R = 0.6f;
				Metal->R = 1.0f;
				M.Connect(Colour, 0, Make, TEXT("BaseColor"));
				M.Connect(Rough, 0, Make, TEXT("Roughness"));
				// One attribute added to Set Material Attributes, as its details panel adds it: a GUID and an input
				Set->AttributeSetTypes.Add(FMaterialAttributeDefinitionMap::GetID(MP_Metallic));
				Set->Inputs.AddDefaulted();
				Make->ConnectExpression(&Set->Inputs[0], 0);
				Metal->ConnectExpression(&Set->Inputs[1], 0);
				Set->ConnectExpression(Break->GetInput(0), 0);
				const int32 BaseColorOutput = Break->GetOutputs().IndexOfByPredicate([](const FExpressionOutput& Output) { return Output.OutputName == TEXT("BaseColor"); });
				const int32 MetallicOutput = Break->GetOutputs().IndexOfByPredicate([](const FExpressionOutput& Output) { return Output.OutputName == TEXT("Metallic"); });
				M.ConnectToProperty(Break, BaseColorOutput, MP_BaseColor);
				M.ConnectToProperty(Break, MetallicOutput, MP_Metallic);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("Composite"), TEXT("A constant through a composite (collapsed nodes) whose inside inverts it, into BaseColor: the composite and its pin bases are traced, the One Minus inside is captured"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant3Vector* Colour = M.Add<UMaterialExpressionConstant3Vector>(nullptr, -600, 0);
				UMaterialExpressionComposite* Composite = M.Add<UMaterialExpressionComposite>(nullptr, -400, 0);
				UMaterialExpressionPinBase* Entry = M.Add<UMaterialExpressionPinBase>(nullptr, -300, 0);
				UMaterialExpressionPinBase* Exit = M.Add<UMaterialExpressionPinBase>(nullptr, 100, 0);
				UMaterialExpressionReroute* In = M.Add<UMaterialExpressionReroute>(nullptr, -300, 50);
				UMaterialExpressionReroute* Out = M.Add<UMaterialExpressionReroute>(nullptr, 100, 50);
				UMaterialExpressionOneMinus* Invert = M.Add<UMaterialExpressionOneMinus>(nullptr, -100, 0);
				if (!Colour || !Composite || !Entry || !Exit || !In || !Out || !Invert)
				{
					return nullptr;
				}
				Colour->Constant = FLinearColor(0.2f, 0.4f, 0.6f);
				// As the Material Editor's Collapse Nodes builds it (MaterialEditor.cpp): the entry pin base outputs into
				// the composite, the exit pin base takes its inputs, each pin a reroute in the expression list
				Composite->SubgraphName = TEXT("Invert");
				Composite->InputExpressions = Entry;
				Composite->OutputExpressions = Exit;
				Entry->PinDirection = EGPD_Output;
				Exit->PinDirection = EGPD_Input;
				Entry->ReroutePins.Add(FCompositeReroute(TEXT("Colour"), In));
				Exit->ReroutePins.Add(FCompositeReroute(TEXT("Inverted"), Out));
				for (UMaterialExpression* Inside : TArray<UMaterialExpression*>{ Entry, Exit, In, Out, Invert })
				{
					Inside->SubgraphExpression = Composite;
				}
				M.Connect(Colour, 0, In, TEXT("Input"));
				M.Connect(Entry, 0, Invert, TEXT("Input"));
				M.Connect(Invert, 0, Out, TEXT("Input"));
				M.ConnectToProperty(Composite, 0, MP_BaseColor);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("StaticSwitchValue"), TEXT("A Static Switch (not a parameter) with its Value input unwired and Default Value true: both branches kept and live, the default in properties"),
			[](N2CTest::FTestMaterial& M) -> UObject*
			{
				UMaterialExpressionConstant* One = M.Add<UMaterialExpressionConstant>(nullptr, -400, -100);
				UMaterialExpressionConstant* Zero = M.Add<UMaterialExpressionConstant>(nullptr, -400, 100);
				UMaterialExpressionStaticSwitch* Switch = M.Add<UMaterialExpressionStaticSwitch>(nullptr, -200, 0);
				if (!One || !Zero || !Switch)
				{
					return nullptr;
				}
				One->R = 1.0f;
				Switch->DefaultValue = true;
				M.Connect(One, 0, Switch, TEXT("A"));
				M.Connect(Zero, 0, Switch, TEXT("B"));
				M.ConnectToProperty(Switch, 0, MP_Roughness);
				return M.GetMaterial();
			} });
		Cases.Add({ TEXT("EngineCameraDepthFade"), TEXT("The engine's /Engine/Functions CameraDepthFade function captured as the asset (the same asset on UE 5.6, 5.7 and 5.8)"),
			[](N2CTest::FTestMaterial&) -> UObject* { return LoadEngineCameraDepthFade(); } });
		Cases.Add({ TEXT("EngineFunctionCall_Depth0"), TEXT("A material calling the engine's CameraDepthFade at depth 0: described by name (decision 0009 item 4), DEPTH_LIMIT"),
			[](N2CTest::FTestMaterial& M) -> UObject* { return BuildEngineFunctionCall(M); }, 0 });
		Cases.Add({ TEXT("EngineFunctionCall_Depth1"), TEXT("The same call at depth 1: CameraDepthFade's graph is expanded after the material's"),
			[](N2CTest::FTestMaterial& M) -> UObject* { return BuildEngineFunctionCall(M); }, 1 });
	}

END_DEFINE_SPEC(FN2CMaterialCaptureSpec)

void FN2CMaterialCaptureSpec::Define()
{
	DefineCases();

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

	// Card 0449: captures from the saved asset
	Describe(TEXT("Golden"), [this]()
	{
		for (const FMaterialCase& Case : Cases)
		{
			Describe(Case.Name, [this, Case]()
			{
				It(TEXT("captures the golden N2C JSON"), [this, Case]() { RunGolden(Case); });
				It(TEXT("survives ToJson -> FromJson -> ToJson unchanged"), [this, Case]() { RunRoundTrip(Case); });
			});
		}
	});

	Describe(TEXT("Asset"), [this]()
	{
		It(TEXT("captures the material whose editor graph CaptureGraph refuses, from the asset: asset_kind Material, graph_type Material"), [this]()
		{
			N2CTest::FTestMaterial Material(TEXT("M_N2CTest_FromAsset"));
			BuildConstantMath(Material);
			UMaterialGraph* Graph = Material.BuildGraph();
			if (!NoFactoryErrors(Material) || !TestNotNull(TEXT("the material graph"), Graph))
			{
				return;
			}
			TestTrue(TEXT("CaptureGraph still refuses the editor graph"), N2C::CaptureGraph(*Graph).HasError());
			const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureMaterial(*Material.GetMaterial());
			if (!TestTrue(TEXT("CaptureMaterial captures the asset"), Capture.HasValue()))
			{
				return;
			}
			const FN2CBlueprint& Document = Capture.GetValue().Blueprint;
			TestTrue(TEXT("asset_kind Material"), Document.Metadata.AssetKind == EN2CAssetKind::Material);
			TestEqual(TEXT("asset_class"), Document.Metadata.AssetClass, FString(TEXT("Material")));
			TestTrue(TEXT("material metadata"), Document.Metadata.Material.bSet);
			TestEqual(TEXT("domain"), Document.Metadata.Material.Domain, FString(TEXT("Surface")));
			TestEqual(TEXT("blend mode"), Document.Metadata.Material.BlendMode, FString(TEXT("Opaque")));
			TestTrue(TEXT("DefaultLit"), Document.Metadata.Material.ShadingModels.Contains(TEXT("DefaultLit")));
			if (TestEqual(TEXT("one graph"), Document.Graphs.Num(), 1))
			{
				TestTrue(TEXT("graph_type Material"), Document.Graphs[0].GraphType == EN2CGraphType::Material);
				TestEqual(TEXT("four expressions and the output node"), Document.Graphs[0].Nodes.Num(), 5);
			}
			TestFalse(TEXT("no error"), Capture.GetValue().HasErrors());

			// Node IDs map to the expressions' GUIDs
			const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material.GetMaterial()->GetExpressions();
			const FGuid* First = Capture.GetValue().NodeGuids.Find(TEXT("N1"));
			TestTrue(TEXT("N1 is the first expression's GUID"), First && Expressions.Num() > 0 && *First == Expressions[0]->MaterialExpressionGuid);
		});

		// One refusal case per material asset class decision 0009 leaves out (card 0450): each is UNSUPPORTED_ASSET_TYPE
		// naming its class, whichever entry point it reaches
		const TPair<UClass*, const TCHAR*> RefusedClasses[] = {
			{ UPreviewMaterial::StaticClass(), TEXT("PreviewMaterial") },
			{ UMaterialFunctionMaterialLayer::StaticClass(), TEXT("MaterialFunctionMaterialLayer") },
			{ UMaterialFunctionMaterialLayerBlend::StaticClass(), TEXT("MaterialFunctionMaterialLayerBlend") },
			{ UMaterialFunctionInstance::StaticClass(), TEXT("MaterialFunctionInstance") },
			{ UMaterialFunctionMaterialLayerInstance::StaticClass(), TEXT("MaterialFunctionMaterialLayerInstance") },
			{ UMaterialFunctionMaterialLayerBlendInstance::StaticClass(), TEXT("MaterialFunctionMaterialLayerBlendInstance") },
			{ UMaterialInstanceConstant::StaticClass(), TEXT("MaterialInstanceConstant") },
		};
		for (const TPair<UClass*, const TCHAR*>& Refused : RefusedClasses)
		{
			UClass* const Class = Refused.Key;
			const FString ClassName = Refused.Value;
			It(FString::Printf(TEXT("refuses a %s with UNSUPPORTED_ASSET_TYPE naming the class"), *ClassName), [this, Class, ClassName]()
			{
				N2CTest::FTestMaterial Holder(TEXT("M_N2CTest_Refused"));
				if (!TestNotNull(TEXT("a package"), Holder.GetPackage()))
				{
					return;
				}
				UObject* Asset = NewObject<UObject>(Holder.GetPackage(), Class, FName(TEXT("Refused_") + ClassName));
				const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureMaterialAsset(*Asset);
				TestTrue(TEXT("UNSUPPORTED_ASSET_TYPE naming the class (got: ") + (Capture.HasError() ? Capture.GetError() : FString()) + TEXT(")"),
					Capture.HasError() && Capture.GetError().StartsWith(TEXT("UNSUPPORTED_ASSET_TYPE: ")) && Capture.GetError().Contains(TEXT("is a ") + ClassName));
				// The typed entry points check the exact class too
				if (UMaterial* Material = Cast<UMaterial>(Asset))
				{
					TestTrue(TEXT("CaptureMaterial refuses it"), N2C::CaptureMaterial(*Material).HasError());
				}
				if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
				{
					TestTrue(TEXT("CaptureMaterialFunction refuses it"), N2C::CaptureMaterialFunction(*Function).HasError());
				}
			});
		}

		It(TEXT("describes a call to a material layer by name and never follows it, whatever the depth"), [this]()
		{
			N2CTest::FTestMaterial Material(TEXT("M_N2CTest_CallsLayer"));
			UMaterialFunctionMaterialLayer* Layer = NewObject<UMaterialFunctionMaterialLayer>(Material.GetPackage(), TEXT("ML_N2CTest_Layer"));
			UMaterialExpressionMaterialFunctionCall* Call = Material.Add<UMaterialExpressionMaterialFunctionCall>();
			if (!NoFactoryErrors(Material) || !Layer || !Call)
			{
				return;
			}
			Call->MaterialFunction = Layer;
			FN2CCaptureOptions Options;
			Options.Depth = 5;
			const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureMaterial(*Material.GetMaterial(), Options);
			if (TestTrue(TEXT("captured"), Capture.HasValue()))
			{
				const FN2CBlueprint& Document = Capture.GetValue().Blueprint;
				TestEqual(TEXT("only the material's graph"), Document.Graphs.Num(), 1);
				const FN2CNodeDefinition* CallNode = Document.Graphs.Num() > 0 ? Document.Graphs[0].Nodes.FindByPredicate(
					[](const FN2CNodeDefinition& Node) { return Node.NodeType == EN2CNodeType::MaterialFunctionCall; }) : nullptr;
				TestTrue(TEXT("the call names the layer"), CallNode && CallNode->MemberName == TEXT("ML_N2CTest_Layer"));
			}
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
