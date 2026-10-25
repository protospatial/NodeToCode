// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateBlueprintClassTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "PackageTools.h"
#include "FileHelpers.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "UnrealEd.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "FileHelpers.h"
#include "Utils/N2CLogger.h"

REGISTER_MCP_TOOL(FN2CMcpCreateBlueprintClassTool)

FMcpToolDefinition FN2CMcpCreateBlueprintClassTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("create-blueprint-class"),
		TEXT("Creates a new Blueprint class with the specified parent class and settings"),
		TEXT("Blueprint Classes")
	);

	// Build input schema manually
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// blueprintName property
	TSharedPtr<FJsonObject> BlueprintNameProp = MakeShareable(new FJsonObject);
	BlueprintNameProp->SetStringField(TEXT("type"), TEXT("string"));
	BlueprintNameProp->SetStringField(TEXT("description"), TEXT("Name for the new Blueprint; BP_ is put in front unless the name already starts with it"));
	Properties->SetObjectField(TEXT("blueprintName"), BlueprintNameProp);

	// parentClassPath property
	TSharedPtr<FJsonObject> ParentClassPathProp = MakeShareable(new FJsonObject);
	ParentClassPathProp->SetStringField(TEXT("type"), TEXT("string"));
	ParentClassPathProp->SetStringField(TEXT("description"), TEXT("Path to the parent class (e.g., '/Script/Engine.Actor')"));
	Properties->SetObjectField(TEXT("parentClassPath"), ParentClassPathProp);

	// assetPath property
	TSharedPtr<FJsonObject> AssetPathProp = MakeShareable(new FJsonObject);
	AssetPathProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetPathProp->SetStringField(TEXT("description"), TEXT("Content folder for the new Blueprint (e.g., '/Game/Blueprints/Doors'). The folder must not exist yet: the tool creates it and refuses an existing one"));
	Properties->SetObjectField(TEXT("assetPath"), AssetPathProp);

	// openInEditor property
	TSharedPtr<FJsonObject> OpenInEditorProp = MakeShareable(new FJsonObject);
	OpenInEditorProp->SetStringField(TEXT("type"), TEXT("boolean"));
	OpenInEditorProp->SetBoolField(TEXT("default"), true);
	OpenInEditorProp->SetStringField(TEXT("description"), TEXT("Open the Blueprint in the editor after creation"));
	Properties->SetObjectField(TEXT("openInEditor"), OpenInEditorProp);

	// openInFullEditor property
	TSharedPtr<FJsonObject> OpenInFullEditorProp = MakeShareable(new FJsonObject);
	OpenInFullEditorProp->SetStringField(TEXT("type"), TEXT("boolean"));
	OpenInFullEditorProp->SetBoolField(TEXT("default"), true);
	OpenInFullEditorProp->SetStringField(TEXT("description"), TEXT("Open in full Blueprint editor (vs. simplified editor)"));
	Properties->SetObjectField(TEXT("openInFullEditor"), OpenInFullEditorProp);

	// description property
	TSharedPtr<FJsonObject> DescriptionProp = MakeShareable(new FJsonObject);
	DescriptionProp->SetStringField(TEXT("type"), TEXT("string"));
	DescriptionProp->SetStringField(TEXT("description"), TEXT("Description/tooltip for the Blueprint"));
	Properties->SetObjectField(TEXT("description"), DescriptionProp);

	// generateConstructionScript property
	TSharedPtr<FJsonObject> GenerateConstructionScriptProp = MakeShareable(new FJsonObject);
	GenerateConstructionScriptProp->SetStringField(TEXT("type"), TEXT("boolean"));
	GenerateConstructionScriptProp->SetBoolField(TEXT("default"), true);
	GenerateConstructionScriptProp->SetStringField(TEXT("description"), TEXT("Has no effect: an Actor Blueprint always gets its construction script"));
	Properties->SetObjectField(TEXT("generateConstructionScript"), GenerateConstructionScriptProp);

	// blueprintType property
	TSharedPtr<FJsonObject> BlueprintTypeProp = MakeShareable(new FJsonObject);
	BlueprintTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> BlueprintTypeEnum;
	BlueprintTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("auto"))));
	BlueprintTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("normal"))));
	BlueprintTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("const"))));
	BlueprintTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("interface"))));
	BlueprintTypeProp->SetArrayField(TEXT("enum"), BlueprintTypeEnum);
	BlueprintTypeProp->SetStringField(TEXT("default"), TEXT("auto"));
	BlueprintTypeProp->SetStringField(TEXT("description"), TEXT("Blueprint type. normal, const and interface are used as given, whatever the parent; auto (and any other value) gives interface for an interface parent, otherwise normal"));
	Properties->SetObjectField(TEXT("blueprintType"), BlueprintTypeProp);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("blueprintName"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("parentClassPath"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("assetPath"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpCreateBlueprintClassTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FString BlueprintName;
		if (!Arguments->TryGetStringField(TEXT("blueprintName"), BlueprintName))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: blueprintName"));
		}

		FString ParentClassPath;
		if (!Arguments->TryGetStringField(TEXT("parentClassPath"), ParentClassPath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: parentClassPath"));
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: assetPath"));
		}

		bool bOpenInEditor = true;
		Arguments->TryGetBoolField(TEXT("openInEditor"), bOpenInEditor);

		bool bOpenInFullEditor = true;
		Arguments->TryGetBoolField(TEXT("openInFullEditor"), bOpenInFullEditor);

		// Validate parent class
		UClass* ParentClass = nullptr;
		FString ErrorMessage;
		if (!ValidateParentClass(ParentClassPath, ParentClass, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Validate and prepare asset path
		FString PackageName, AssetName;
		if (!ValidateAndPrepareAssetPath(AssetPath, BlueprintName, PackageName, AssetName, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Determine Blueprint type
		FString BlueprintTypeStr = TEXT("auto");
		Arguments->TryGetStringField(TEXT("blueprintType"), BlueprintTypeStr);
		
		EBlueprintType BlueprintType;
		if (BlueprintTypeStr == TEXT("normal"))
		{
			BlueprintType = BPTYPE_Normal;
		}
		else if (BlueprintTypeStr == TEXT("const"))
		{
			BlueprintType = BPTYPE_Const;
		}
		else if (BlueprintTypeStr == TEXT("interface"))
		{
			BlueprintType = BPTYPE_Interface;
		}
		else // auto
		{
			BlueprintType = DetermineBlueprintType(ParentClass);
		}

		// Create the Blueprint
		UBlueprint* NewBlueprint = CreateBlueprintAsset(ParentClass, PackageName, AssetName, BlueprintType, ErrorMessage);
		if (!NewBlueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Apply additional settings
		ApplyBlueprintSettings(NewBlueprint, Arguments);

		// Save the asset
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(NewBlueprint->GetOutermost());
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false))
		{
			FN2CLogger::Get().LogWarning(TEXT("Failed to save Blueprint package"));
		}

		// Open in editor if requested
		if (bOpenInEditor)
		{
			OpenBlueprintInEditor(NewBlueprint, bOpenInFullEditor);
		}

		// Show notification
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("NodeToCode", "BlueprintCreated", "Blueprint '{0}' created successfully"),
			FText::FromString(AssetName)
		));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		// Build success result
		TSharedPtr<FJsonObject> Result = BuildSuccessResult(NewBlueprint, PackageName, AssetName);

		// Convert result to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString);
	});
}

bool FN2CMcpCreateBlueprintClassTool::ValidateParentClass(const FString& ClassPath, UClass*& OutClass, FString& OutErrorMessage) const
{
	// Try to find the class
	OutClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!OutClass)
	{
		// Try loading it
		OutClass = LoadObject<UClass>(nullptr, *ClassPath);
	}

	if (!OutClass)
	{
		OutErrorMessage = FString::Printf(TEXT("Parent class not found: %s"), *ClassPath);
		return false;
	}

	// Check if we can create a Blueprint from this class
	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(OutClass))
	{
		OutErrorMessage = FString::Printf(TEXT("Cannot create Blueprint from class: %s. Class may not be Blueprintable."), *ClassPath);
		return false;
	}

	return true;
}

bool FN2CMcpCreateBlueprintClassTool::ValidateAndPrepareAssetPath(const FString& AssetPath, const FString& BlueprintName, 
	FString& OutPackageName, FString& OutAssetName, FString& OutErrorMessage) const
{
	// Normalize the asset path
	FString NormalizedPath = FN2CMcpContentBrowserUtils::NormalizeContentPath(AssetPath);

	// Ensure the directory exists
	FString DirectoryError;
	if (!FN2CMcpContentBrowserUtils::CreateContentFolder(NormalizedPath, DirectoryError))
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to create directory: %s"), *DirectoryError);
		return false;
	}

	// Add BP_ prefix if not already present
	OutAssetName = BlueprintName;
	if (!OutAssetName.StartsWith(TEXT("BP_")))
	{
		OutAssetName = FString::Printf(TEXT("BP_%s"), *OutAssetName);
	}

	// Build the full package path
	FString BasePackageName = NormalizedPath / OutAssetName;

	// Get unique asset name
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();
	
	FString UnusedSuffix;
	AssetTools.CreateUniqueAssetName(BasePackageName, UnusedSuffix, OutPackageName, OutAssetName);

	// Check if asset already exists
	if (FPackageName::DoesPackageExist(OutPackageName))
	{
		OutErrorMessage = FString::Printf(TEXT("Asset already exists at: %s"), *OutPackageName);
		return false;
	}

	return true;
}

EBlueprintType FN2CMcpCreateBlueprintClassTool::DetermineBlueprintType(const UClass* ParentClass) const
{
	if (!ParentClass)
	{
		return BPTYPE_Normal;
	}

	// Check if it's an interface
	if (ParentClass->HasAnyClassFlags(CLASS_Interface))
	{
		return BPTYPE_Interface;
	}

	// Default to normal Blueprint
	return BPTYPE_Normal;
}

UBlueprint* FN2CMcpCreateBlueprintClassTool::CreateBlueprintAsset(UClass* ParentClass, const FString& PackageName, 
	const FString& AssetName, EBlueprintType BlueprintType, FString& OutErrorMessage) const
{
	// Create the package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutErrorMessage = TEXT("Failed to create package");
		return nullptr;
	}

	// Create the Blueprint
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		*AssetName,
		BlueprintType,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);

	if (!NewBlueprint)
	{
		OutErrorMessage = TEXT("Failed to create Blueprint");
		return nullptr;
	}

	// Mark package as dirty
	Package->MarkPackageDirty();

	// Compile the Blueprint
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	// Register with asset registry
	FAssetRegistryModule::AssetCreated(NewBlueprint);

	return NewBlueprint;
}

void FN2CMcpCreateBlueprintClassTool::ApplyBlueprintSettings(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Arguments) const
{
	if (!Blueprint)
	{
		return;
	}

	// Set description if provided
	FString Description;
	if (Arguments->TryGetStringField(TEXT("description"), Description))
	{
		Blueprint->BlueprintDescription = Description;
	}

	// Generate construction script for Actor-based Blueprints
	bool bGenerateConstructionScript = true;
	Arguments->TryGetBoolField(TEXT("generateConstructionScript"), bGenerateConstructionScript);

	if (bGenerateConstructionScript && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
	{
		// Ensure the Blueprint has a SimpleConstructionScript
		if (!Blueprint->SimpleConstructionScript)
		{
			Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(Blueprint);
		}

		// The construction script is automatically created with the Blueprint
		// Additional setup can be done here if needed
	}

	// Refresh the Blueprint
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
}

void FN2CMcpCreateBlueprintClassTool::OpenBlueprintInEditor(UBlueprint* Blueprint, bool bOpenInFullEditor) const
{
	if (!Blueprint)
	{
		return;
	}

	if (bOpenInFullEditor)
	{
		// Open in full Blueprint editor
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Blueprint);
	}
	else
	{
		// Open in simplified editor (asset editor)
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
	}
}

TSharedPtr<FJsonObject> FN2CMcpCreateBlueprintClassTool::BuildSuccessResult(const UBlueprint* Blueprint, 
	const FString& PackageName, const FString& AssetName) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprintName"), AssetName);
	Result->SetStringField(TEXT("packagePath"), PackageName);
	Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());

	// Add Blueprint info
	TSharedPtr<FJsonObject> BlueprintInfo = MakeShareable(new FJsonObject);
	BlueprintInfo->SetStringField(TEXT("className"), Blueprint->GetName());
	BlueprintInfo->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));
	BlueprintInfo->SetStringField(TEXT("blueprintType"), [](EBlueprintType Type) -> FString
	{
		switch (Type)
		{
			case BPTYPE_Normal: return TEXT("Normal");
			case BPTYPE_Const: return TEXT("Const");
			case BPTYPE_Interface: return TEXT("Interface");
			case BPTYPE_FunctionLibrary: return TEXT("FunctionLibrary");
			default: return TEXT("Unknown");
		}
	}(Blueprint->BlueprintType));

	// Add generated class info
	if (Blueprint->GeneratedClass)
	{
		BlueprintInfo->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
	}

	Result->SetObjectField(TEXT("blueprintInfo"), BlueprintInfo);

	// Add helpful next steps
	TArray<TSharedPtr<FJsonValue>> NextSteps;
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'create-blueprint-function' to add functions"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'create-variable' to add member variables"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'add-bp-node-to-active-graph' to add nodes"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'translate-focused-blueprint' to generate code"))));
	Result->SetArrayField(TEXT("nextSteps"), NextSteps);

	return Result;
}