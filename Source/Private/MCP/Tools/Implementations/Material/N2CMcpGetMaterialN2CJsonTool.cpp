// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpGetMaterialN2CJsonTool.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CCapture.h"
#include "Core/N2CMaterialCaptureContext.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Utils/N2CPathUtils.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "IMaterialEditor.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpGetMaterialN2CJsonTool)

namespace N2CMcpGetMaterialN2CJsonToolPrivate
{
	/** /Game/M_Foo -> /Game/M_Foo.M_Foo; an object path is kept */
	FString ObjectPathOf(const FString& Path)
	{
		FString Trimmed = Path.TrimStartAndEnd();
		if (Trimmed.Contains(TEXT(".")))
		{
			return Trimmed;
		}
		return Trimmed + TEXT(".") + FPackageName::GetShortName(Trimmed);
	}

	FString Serialize(const TSharedRef<FJsonObject>& Json, bool bCompact)
	{
		FString Text;
		if (bCompact)
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Json, Writer);
		}
		else
		{
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Json, Writer);
		}
		return Text;
	}

	/**
	 * Captures a Material Editor's working copy (WorkingCopy, the transient copy of Original) as Original: the document
	 * is named after the asset and its class, not the transient copy's. The same capture and validation as
	 * N2C::CaptureMaterialAsset, whose exact-class check would refuse the UPreviewMaterial the editor works on.
	 */
	FN2CCapture CaptureWorkingCopy(UObject& WorkingCopy, const UObject& Original, const FN2CCaptureOptions& Options)
	{
		FN2CMaterialCaptureContext Context(Options);
		if (UMaterialFunction* Function = Cast<UMaterialFunction>(&WorkingCopy))
		{
			Context.CaptureFunction(*Function);
		}
		else
		{
			Context.CaptureMaterial(*CastChecked<UMaterial>(&WorkingCopy));
		}
		FN2CCapture Captured = Context.TakeCapture();
		FN2CMetadata& Metadata = Captured.Blueprint.Metadata;
		Metadata.Name = Original.GetName();
		Metadata.AssetClass = Original.GetClass()->GetName();
		// The first graph is the asset's own; a duplicate can carry a suffixed name (MF_Foo_1)
		if (Captured.Blueprint.Graphs.Num() > 0 && Captured.Blueprint.Graphs[0].Name == WorkingCopy.GetName())
		{
			Captured.Blueprint.Graphs[0].Name = Original.GetName();
		}
		FN2CBlueprintValidator().Validate(Captured.Blueprint, Captured.Diagnostics);
		return Captured;
	}

	void Describe(const TSharedPtr<FJsonObject>& Schema, const TCHAR* Name, const TCHAR* Description)
	{
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		const TSharedPtr<FJsonObject>* Property = nullptr;
		if (Schema->TryGetObjectField(TEXT("properties"), Properties) && (*Properties)->TryGetObjectField(Name, Property))
		{
			(*Property)->SetStringField(TEXT("description"), Description);
		}
	}
}

UObject* FN2CMcpGetMaterialN2CJsonTool::LoadMaterialAsset(const FString& Path, FString& OutError)
{
	using namespace N2CMcpGetMaterialN2CJsonToolPrivate;
	const FString Trimmed = Path.TrimStartAndEnd();
	const FString ObjectPath = ObjectPathOf(Trimmed);
	UObject* Asset = nullptr;
	if (Trimmed.StartsWith(TEXT("/")) && FPackageName::IsValidObjectPath(ObjectPath))
	{
		Asset = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("ASSET_NOT_FOUND: no asset at '%s'. Use the asset path, e.g. /Engine/BasicShapes/BasicShapeMaterial"),
			*Trimmed.Left(200));
	}
	return Asset;
}

bool FN2CMcpGetMaterialN2CJsonTool::IsMaterialAssetPath(const FString& Path)
{
	using namespace N2CMcpGetMaterialN2CJsonToolPrivate;
	const FString Trimmed = Path.TrimStartAndEnd();
	if (!Trimmed.StartsWith(TEXT("/")))
	{
		return false;
	}
	// The AssetRegistry first (no load), then a load for an asset not in it
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPathOf(Trimmed)));
	if (Data.IsValid())
	{
		const UClass* Class = Data.GetClass();
		return Class && (Class->IsChildOf(UMaterialInterface::StaticClass()) || Class->IsChildOf(UMaterialFunctionInterface::StaticClass()));
	}
	FString Ignored;
	const UObject* Asset = LoadMaterialAsset(Trimmed, Ignored);
	return Asset && (Asset->IsA<UMaterialInterface>() || Asset->IsA<UMaterialFunctionInterface>());
}

FString FN2CMcpGetMaterialN2CJsonTool::NotABlueprintMessage(const FString& Path)
{
	return FString::Printf(TEXT("UNSUPPORTED_ASSET_TYPE: '%s' is a material asset, not a Blueprint: read it with get-material-n2cjson (materialPath)"),
		*Path.TrimStartAndEnd().Left(200));
}

FMcpToolDefinition FN2CMcpGetMaterialN2CJsonTool::GetDefinition() const
{
	using namespace N2CMcpGetMaterialN2CJsonToolPrivate;

	FMcpToolDefinition Definition(
		TEXT("get-material-n2cjson"),
		TEXT("Reads a Material or Material Function as NodeToCode's N2C JSON: every expression with its pins, the wiring to ")
		TEXT("the material's outputs, parameters, static switches (both branches) and material function calls (engine ")
		TEXT("functions are named at depth 0 and expanded only with depth > 0). With materialPath it reads the saved asset ")
		TEXT("with no editor open, so unapplied edits in an open Material Editor are not included (workingCopy false). ")
		TEXT("Without materialPath it reads the working copy of the Material Editor the user activated last, unapplied edits ")
		TEXT("included (resolvedBy focus, workingCopy true), or fails with NO_FOCUSED_MATERIAL when none is open. The result ")
		TEXT("also has target {assetPath (the saved asset's path, also for a working copy), kind: material | ")
		TEXT("material-function, resolvedBy: path | focus, workingCopy, workingCopyPath (focus only)}. Over ")
		TEXT("about 20000 tokens, or with toFile, the document is written under Saved/NodeToCode/Exports and the result is ")
		TEXT("{path, bytes, estimatedTokens, reason, nodeCount, target}. Material instances, material layers and layer blends ")
		TEXT("are refused with UNSUPPORTED_ASSET_TYPE; read a Blueprint with get-blueprint-n2cjson."),
		N2CMcpToolsets::Material
	);

	TMap<FString, FString> Properties;
	Properties.Add(TEXT("materialPath"), TEXT("string"));
	Properties.Add(TEXT("depth"), TEXT("integer"));
	Properties.Add(TEXT("compact"), TEXT("boolean"));
	Properties.Add(TEXT("toFile"), TEXT("boolean"));
	const TSharedPtr<FJsonObject> Schema = BuildInputSchema(Properties);
	Describe(Schema, TEXT("materialPath"), TEXT("Optional. Asset path of the Material or Material Function, e.g. /Engine/BasicShapes/BasicShapeMaterial or /Game/Materials/M_Rock.M_Rock: reads the saved asset. Omit it to read the working copy of the Material Editor the user activated last."));
	Describe(Schema, TEXT("depth"), TEXT("Optional, 0 to 5. How deep material function calls are expanded; engine functions (/Engine/Functions) are named only at depth 0. Default: the project's Max Translation Depth setting."));
	Describe(Schema, TEXT("compact"), TEXT("Optional, default true. No whitespace; false indents the JSON."));
	Describe(Schema, TEXT("toFile"), TEXT("Optional, default false. Always write the document to a file and return its path."));
	const TSharedPtr<FJsonObject>* SchemaProperties = nullptr;
	if (Schema->TryGetObjectField(TEXT("properties"), SchemaProperties))
	{
		const TSharedPtr<FJsonObject> Depth = (*SchemaProperties)->GetObjectField(TEXT("depth"));
		Depth->SetNumberField(TEXT("minimum"), 0);
		Depth->SetNumberField(TEXT("maximum"), 5);
		(*SchemaProperties)->GetObjectField(TEXT("compact"))->SetBoolField(TEXT("default"), true);
		(*SchemaProperties)->GetObjectField(TEXT("toFile"))->SetBoolField(TEXT("default"), false);
	}
	Definition.InputSchema = Schema;

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	// Expression descriptions, parameter names and Custom node code come from whoever authored the material (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpGetMaterialN2CJsonTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([Arguments]() -> FMcpToolCallResult
	{
		using namespace N2CMcpGetMaterialN2CJsonToolPrivate;

		FString MaterialPath;
		if (Arguments.IsValid() && Arguments->HasField(TEXT("materialPath")) && !Arguments->TryGetStringField(TEXT("materialPath"), MaterialPath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: materialPath must be a string, the asset path of a Material such as /Engine/BasicShapes/BasicShapeMaterial"));
		}
		// No path (or an empty one): the focused Material Editor's working copy (card 0463)
		const bool bFocus = MaterialPath.TrimStartAndEnd().IsEmpty();
		int32 Depth = INDEX_NONE;
		if (Arguments.IsValid() && Arguments->HasField(TEXT("depth")))
		{
			double Value = 0.0;
			if (!Arguments->TryGetNumberField(TEXT("depth"), Value) || Value != FMath::RoundToDouble(Value) || Value < 0.0 || Value > 5.0)
			{
				return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: depth must be a whole number from 0 to 5"));
			}
			Depth = static_cast<int32>(Value);
		}
		bool bCompact = true;
		bool bToFile = false;
		if (Arguments.IsValid())
		{
			Arguments->TryGetBoolField(TEXT("compact"), bCompact);
			Arguments->TryGetBoolField(TEXT("toFile"), bToFile);
		}

		FString Error;
		UObject* Asset = nullptr;
		UObject* WorkingCopy = nullptr;
		if (bFocus)
		{
			// The Material Editor the user activated last: its working copy, unapplied edits included
			Asset = FN2CBlueprintEditorTracker::Get().GetFocusedMaterialAsset();
			const TSharedPtr<IMaterialEditor> Editor = FN2CBlueprintEditorTracker::GetMaterialEditorFor(Asset);
			WorkingCopy = Editor.IsValid() ? FN2CBlueprintEditorTracker::WorkingCopyOf(*Editor) : nullptr;
			if (!Asset || !WorkingCopy)
			{
				return FMcpToolCallResult::CreateErrorResult(TEXT("NO_FOCUSED_MATERIAL: no Material Editor is open. Open a Material or ")
					TEXT("Material Function in the editor, or pass materialPath to read the saved asset."));
			}
		}
		else
		{
			Asset = LoadMaterialAsset(MaterialPath, Error);
			if (!Asset)
			{
				return FMcpToolCallResult::CreateErrorResult(Error);
			}
			if (Asset->IsA<UBlueprint>())
			{
				return FMcpToolCallResult::CreateErrorResult(FString::Printf(
					TEXT("UNSUPPORTED_ASSET_TYPE: '%s' is a Blueprint, not a material: read it with get-blueprint-n2cjson (blueprintPath)"), *MaterialPath.Left(200)));
			}
		}

		// The asset's exact class decides what is captured (decision 0009 item 1), also for a working copy: a material
		// layer's editor works on a plain UMaterialFunction duplicate, so the asset is checked, not its copy
		const bool bSupportedClass = Asset->GetClass() == UMaterial::StaticClass() || Asset->GetClass() == UMaterialFunction::StaticClass();
		TValueOrError<FN2CCapture, FString> Capture = [&]() -> TValueOrError<FN2CCapture, FString>
		{
			if (WorkingCopy && bSupportedClass)
			{
				return MakeValue(CaptureWorkingCopy(*WorkingCopy, *Asset, N2CMcpBlueprintExport::CaptureOptions(Depth)));
			}
			return N2C::CaptureMaterialAsset(*Asset, N2CMcpBlueprintExport::CaptureOptions(Depth));
		}();
		if (Capture.HasError())
		{
			const FString Message = Capture.GetError();
			// UNSUPPORTED_ASSET_TYPE (an instance, a layer or blend) keeps its code; anything else is a failed capture
			return FMcpToolCallResult::CreateErrorResult(Message.StartsWith(TEXT("UNSUPPORTED_ASSET_TYPE"))
				? Message : TEXT("CAPTURE_FAILED: ") + Message);
		}
		const FN2CCapture& Captured = Capture.GetValue();

		const TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
		Target->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Target->SetStringField(TEXT("kind"), Asset->IsA<UMaterialFunctionInterface>() ? TEXT("material-function") : TEXT("material"));
		Target->SetStringField(TEXT("resolvedBy"), bFocus ? TEXT("focus") : TEXT("path"));
		Target->SetBoolField(TEXT("workingCopy"), bFocus);
		if (WorkingCopy)
		{
			// Which copy was read: the editor's transient copy (/Engine/Transient.<name>), not the saved asset
			Target->SetStringField(TEXT("workingCopyPath"), WorkingCopy->GetPathName());
		}

		// The document get-blueprint-n2cjson returns for a graph (ids {short, guid}, flows, guids), plus the target
		const TSharedPtr<FJsonObject> Document = N2CMcpOutputSchema::ParseObject(N2CMcpBlueprintExport::ToResultJson(Captured, /*bCompact*/ true));
		if (!Document.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("CAPTURE_FAILED: the captured material could not be serialized"));
		}
		Document->SetObjectField(TEXT("target"), Target);
		const FString Text = Serialize(Document.ToSharedRef(), bCompact);

		int32 NodeCount = 0;
		for (const FN2CGraph& Graph : Captured.Blueprint.Graphs)
		{
			NodeCount += Graph.Nodes.Num();
		}
		const int32 Estimated = N2CMcpBlueprintExport::EstimateTokens(Text);
		if (!bToFile && Estimated <= N2CMcpBlueprintExport::MaxInlineTokens)
		{
			return FMcpToolCallResult::CreateTextResult(Text);
		}

		// A working copy gets its own file, so it never overwrites the saved asset's export
		const FString Key = FString::Printf(TEXT("%s|%d|%d%s"), *Asset->GetPathName(), Depth, bCompact ? 1 : 0, bFocus ? TEXT("|workingCopy") : TEXT(""));
		const FString Stem = FN2CPathUtils::MakeSafeFileStem(Asset->GetName());
		const FString FileName = FString::Printf(TEXT("%s__%s__%08x.json"), Stem.IsEmpty() ? TEXT("Material") : *Stem,
			bFocus ? TEXT("MaterialWorkingCopy") : TEXT("Material"), FCrc::StrCrc32(*Key));
		FString SavedTo;
		int64 Bytes = 0;
		if (!N2CMcpBlueprintExport::WriteExport(Text, FileName, SavedTo, Bytes, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}
		const TSharedRef<FJsonObject> FileResult = MakeShared<FJsonObject>();
		FileResult->SetStringField(TEXT("path"), SavedTo);
		FileResult->SetNumberField(TEXT("bytes"), static_cast<double>(Bytes));
		FileResult->SetNumberField(TEXT("estimatedTokens"), Estimated);
		FileResult->SetNumberField(TEXT("maxInlineTokens"), N2CMcpBlueprintExport::MaxInlineTokens);
		FileResult->SetStringField(TEXT("reason"), bToFile ? TEXT("toFile") : TEXT("size"));
		FileResult->SetNumberField(TEXT("nodeCount"), NodeCount);
		FileResult->SetObjectField(TEXT("target"), Target);
		return FMcpToolCallResult::CreateTextResult(Serialize(FileResult, true));
	}, 60.0f);
}
