// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestMaterialFactory.h"
#include "Tests/Dependencies/N2CDependencyFixtures.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Models/N2CJsonContract.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * Card 0453: get-material-n2cjson over engine materials (BasicShapeMaterial, WorldGridMaterial) and the engine's
 * CameraDepthFade material function: the document (asset_kind Material, nodes, the target with workingCopy false)
 * marked as untrusted, toFile writing the export, and the refusals: a Blueprint path, a material instance, a missing
 * path, a bad depth. get-blueprint-n2cjson given a material path names get-material-n2cjson.
 */
BEGIN_DEFINE_SPEC(FN2CMaterialN2CJsonSpec, "NodeToCode.MCP.MaterialN2CJson", N2C_TEST_FLAGS)

	static FString FirstText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}

	static TSharedPtr<FJsonObject> Args(const FString& Path)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("materialPath"), Path);
		return Json;
	}

	static int32 NodeCount(const TSharedPtr<FJsonObject>& Document)
	{
		int32 Count = 0;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (Document.IsValid() && Document->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& Graph : *Graphs)
			{
				const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
				if (Graph->AsObject()->TryGetArrayField(TEXT("nodes"), Nodes))
				{
					Count += Nodes->Num();
				}
			}
		}
		return Count;
	}

	/** Reads Path; the document, after checking it is a marked, successful read of Kind with workingCopy false */
	TSharedPtr<FJsonObject> ExpectRead(const FString& Path, const FString& Kind)
	{
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-material-n2cjson"), Args(Path));
		if (!TestFalse(Path + TEXT(": not an error (") + FirstText(Result).Left(300) + TEXT(")"), Result.bIsError))
		{
			return nullptr;
		}
		TestTrue(Path + TEXT(": marked as untrusted"), N2CTest::IsMarkedUntrusted(Result));
		const TSharedPtr<FJsonObject> Document = N2CTest::ResultJson(Result);
		if (!TestTrue(Path + TEXT(": a document"), Document.IsValid()))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Metadata = nullptr;
		FString AssetKind;
		TestTrue(Path + TEXT(": metadata.asset_kind is set"), Document->TryGetObjectField(TEXT("metadata"), Metadata)
			&& (*Metadata)->TryGetStringField(TEXT("asset_kind"), AssetKind) && !AssetKind.IsEmpty());
		TestTrue(Path + TEXT(": nodes"), NodeCount(Document) > 0);
		const TSharedPtr<FJsonObject>* Target = nullptr;
		if (TestTrue(Path + TEXT(": target"), Document->TryGetObjectField(TEXT("target"), Target)))
		{
			TestEqual(Path + TEXT(": target.kind"), (*Target)->GetStringField(TEXT("kind")), Kind);
			TestEqual(Path + TEXT(": target.resolvedBy"), (*Target)->GetStringField(TEXT("resolvedBy")), FString(TEXT("path")));
			TestFalse(Path + TEXT(": target.workingCopy is false (the saved asset)"), (*Target)->GetBoolField(TEXT("workingCopy")));
			TestTrue(Path + TEXT(": target.assetPath"), (*Target)->GetStringField(TEXT("assetPath")).StartsWith(Path));
		}
		return Document;
	}

	void ExpectError(const FString& What, const TSharedPtr<FJsonObject>& Arguments, const FString& Code, const FString& Contains = FString())
	{
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-material-n2cjson"), Arguments);
		const FString Text = FirstText(Result);
		TestTrue(What + TEXT(": an error"), Result.bIsError);
		TestTrue(FString::Printf(TEXT("%s: %s (%s)"), *What, *Code, *Text), Text.StartsWith(Code) && (Contains.IsEmpty() || Text.Contains(Contains)));
	}

END_DEFINE_SPEC(FN2CMaterialN2CJsonSpec)

void FN2CMaterialN2CJsonSpec::Define()
{
	It(TEXT("is a read-only MaterialTools tool that returns untrusted content"), [this]()
	{
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(TEXT("get-material-n2cjson"));
		if (!TestTrue(TEXT("registered"), Tool.IsValid()))
		{
			return;
		}
		const FMcpToolDefinition Definition = Tool->GetDefinition();
		TestTrue(TEXT("read-only"), Definition.Annotations.IsReadOnly());
		TestTrue(TEXT("MaterialTools"), Definition.Toolset == &N2CMcpToolsets::Material);
		TestTrue(TEXT("untrusted"), Definition.bReturnsUntrustedContent);
	});

	It(TEXT("reads engine materials and a material function from the saved asset"), [this]()
	{
		ExpectRead(TEXT("/Engine/BasicShapes/BasicShapeMaterial"), TEXT("material"));
		ExpectRead(TEXT("/Engine/EngineMaterials/WorldGridMaterial"), TEXT("material"));
		ExpectRead(TEXT("/Engine/Functions/Engine_MaterialFunctions01/Opacity/CameraDepthFade"), TEXT("material-function"));
		// The object path works too
		ExpectRead(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), TEXT("material"));
	});

	It(TEXT("writes the document to the export folder with toFile"), [this]()
	{
		N2CTest::FScratchDir Scratch(TEXT("N2CMaterialExport"));
		N2CMcpBlueprintExport::SetExportDirectoryForTests(Scratch.Path);
		ON_SCOPE_EXIT { N2CMcpBlueprintExport::SetExportDirectoryForTests(FString()); };
		const TSharedPtr<FJsonObject> Arguments = Args(TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
		Arguments->SetBoolField(TEXT("toFile"), true);
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-material-n2cjson"), Arguments);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		if (!TestTrue(TEXT("a file result: ") + FirstText(Result).Left(300), !Result.bIsError && Json.IsValid() && Json->HasField(TEXT("path"))))
		{
			return;
		}
		const FString Path = Json->GetStringField(TEXT("path"));
		TestTrue(TEXT("inside the export folder"), FPaths::IsUnderDirectory(Path, Scratch.Path));
		TestEqual(TEXT("reason"), Json->GetStringField(TEXT("reason")), FString(TEXT("toFile")));
		TestTrue(TEXT("nodeCount"), Json->GetNumberField(TEXT("nodeCount")) > 0);
		TestFalse(TEXT("target.workingCopy"), Json->GetObjectField(TEXT("target"))->GetBoolField(TEXT("workingCopy")));
		FString Text;
		const TSharedPtr<FJsonObject> File = FFileHelper::LoadFileToString(Text, *Path) ? N2CTest::ParseJsonObject(Text) : nullptr;
		TestTrue(TEXT("the file is the document"), File.IsValid() && NodeCount(File) > 0 && File->HasField(TEXT("target")));
	});

	It(TEXT("reads a material instance as its base material with the instance summary (card 0470)"), [this]()
	{
		N2CTest::FTestMaterial Base(TEXT("M_N2CTest_ToolInstanceBase"));
		UMaterialExpressionScalarParameter* Rough = Base.Add<UMaterialExpressionScalarParameter>();
		if (!TestNotNull(TEXT("a scalar parameter"), Rough))
		{
			return;
		}
		Rough->ParameterName = TEXT("Roughness");
		Base.ConnectToProperty(Rough, 0, MP_Roughness);
		Base.GetMaterial()->UpdateCachedExpressionData();
		UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(Base.GetPackage(), TEXT("MI_N2CTest_ToolInstance"));
		Instance->SetParentEditorOnly(Base.GetMaterial(), /*RecacheShader*/ false);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Roughness")), 0.25f);
		const TSharedPtr<FJsonObject> Document = ExpectRead(Instance->GetPathName(), TEXT("material"));
		const TSharedPtr<FJsonObject>* Metadata = nullptr;
		const TSharedPtr<FJsonObject>* Material = nullptr;
		const TSharedPtr<FJsonObject>* Summary = nullptr;
		if (TestTrue(TEXT("metadata.material.instance"), Document.IsValid() && Document->TryGetObjectField(TEXT("metadata"), Metadata)
			&& (*Metadata)->TryGetObjectField(TEXT("material"), Material) && (*Material)->TryGetObjectField(TEXT("instance"), Summary)))
		{
			TestEqual(TEXT("the instance's name"), (*Metadata)->GetStringField(TEXT("name")), FString(TEXT("MI_N2CTest_ToolInstance")));
			TestEqual(TEXT("Roughness"), (*Summary)->GetObjectField(TEXT("scalars"))->GetStringField(TEXT("Roughness")), FString(TEXT("0.250000")));
		}
	});

	It(TEXT("returns the capture's warnings, inline and in the file result (card 0538)"), [this]()
	{
		// WorldGridMaterial calls the engine function CameraDepthFade: named, not expanded, at depth 0 (DEPTH_LIMIT)
		auto Codes = [](const TSharedPtr<FJsonObject>& Json)
		{
			TArray<FString> Found;
			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			if (Json.IsValid() && Json->TryGetArrayField(N2CJsonContract::WarningsField, Warnings))
			{
				for (const TSharedPtr<FJsonValue>& Warning : *Warnings)
				{
					const TSharedPtr<FJsonObject> Object = Warning->AsObject();
					if (Object.IsValid() && !Object->GetStringField(N2CJsonContract::WarningMessageField).IsEmpty())
					{
						Found.Add(Object->GetStringField(N2CJsonContract::WarningCodeField));
					}
				}
			}
			return Found;
		};
		const TSharedPtr<FJsonObject> Depth0 = Args(TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
		Depth0->SetNumberField(TEXT("depth"), 0);
		const TArray<FString> Inline = Codes(N2CTest::ResultJson(N2CTest::RunTool(TEXT("get-material-n2cjson"), Depth0)));
		TestTrue(TEXT("depth 0: DEPTH_LIMIT in the document's warnings (") + FString::Join(Inline, TEXT(", ")) + TEXT(")"),
			Inline.Contains(N2CJsonContract::Diagnostic::DepthLimit));

		const TSharedPtr<FJsonObject> Depth1 = Args(TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
		Depth1->SetNumberField(TEXT("depth"), 1);
		TestFalse(TEXT("depth 1: the function is expanded, no DEPTH_LIMIT"),
			Codes(N2CTest::ResultJson(N2CTest::RunTool(TEXT("get-material-n2cjson"), Depth1))).Contains(N2CJsonContract::Diagnostic::DepthLimit));

		N2CTest::FScratchDir Scratch(TEXT("N2CMaterialWarnings"));
		N2CMcpBlueprintExport::SetExportDirectoryForTests(Scratch.Path);
		ON_SCOPE_EXIT { N2CMcpBlueprintExport::SetExportDirectoryForTests(FString()); };
		Depth0->SetBoolField(TEXT("toFile"), true);
		const TSharedPtr<FJsonObject> FileResult = N2CTest::ResultJson(N2CTest::RunTool(TEXT("get-material-n2cjson"), Depth0));
		TestTrue(TEXT("a file result"), FileResult.IsValid() && FileResult->HasField(TEXT("path")));
		TestTrue(TEXT("the file result lists DEPTH_LIMIT too"), Codes(FileResult).Contains(N2CJsonContract::Diagnostic::DepthLimit));
	});

	It(TEXT("refuses a Blueprint, a material instance without a parent, a missing path and a bad depth"), [this]()
	{
		ExpectError(TEXT("a Blueprint"), Args(N2CTest::DependencyFixtures::ObjectPath(N2CTest::DependencyFixtures::AName)),
			TEXT("UNSUPPORTED_ASSET_TYPE"), TEXT("get-blueprint-n2cjson"));

		N2CTest::FTestMaterial Holder(TEXT("M_N2CTest_ToolRefused"));
		if (TestNotNull(TEXT("a package"), Holder.GetPackage()))
		{
			// A material instance is summarized (card 0470); one with no parent material has nothing to summarize
			UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(Holder.GetPackage(), TEXT("MI_N2CTest_ToolRefused"));
			ExpectError(TEXT("a material instance with no parent"), Args(Instance->GetPathName()), TEXT("UNSUPPORTED_ASSET_TYPE"), TEXT("no parent material"));
		}
		ExpectError(TEXT("a missing path"), Args(TEXT("/Game/N2CNoSuchMaterial")), TEXT("ASSET_NOT_FOUND"));
		const TSharedPtr<FJsonObject> BadDepth = Args(TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
		BadDepth->SetNumberField(TEXT("depth"), 9);
		ExpectError(TEXT("depth 9"), BadDepth, TEXT("INVALID_ARGUMENT"));
	});

	It(TEXT("get-blueprint-n2cjson names get-material-n2cjson for a material path"), [this]()
	{
		const TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("blueprintPath"), TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-blueprint-n2cjson"), Arguments);
		const FString Text = FirstText(Result);
		TestTrue(TEXT("UNSUPPORTED_ASSET_TYPE naming get-material-n2cjson: ") + Text, Result.bIsError
			&& Text.StartsWith(TEXT("UNSUPPORTED_ASSET_TYPE")) && Text.Contains(TEXT("get-material-n2cjson")));
		Arguments->SetStringField(TEXT("blueprintPath"), TEXT("/Game/N2CNoSuchBlueprint"));
		TestTrue(TEXT("a missing path stays ASSET_NOT_FOUND"), FirstText(N2CTest::RunTool(TEXT("get-blueprint-n2cjson"), Arguments)).StartsWith(TEXT("ASSET_NOT_FOUND")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
