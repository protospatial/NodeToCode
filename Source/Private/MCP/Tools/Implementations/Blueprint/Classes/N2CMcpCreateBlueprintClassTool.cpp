// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateBlueprintClassTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Animation/AnimInstance.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Factories/BlueprintFactory.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Notifications/SNotificationList.h"

REGISTER_MCP_TOOL(FN2CMcpCreateBlueprintClassTool)

namespace
{
	IAssetTools& GetAssetTools()
	{
		return FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	}

	FString BlueprintTypeToString(EBlueprintType Type)
	{
		switch (Type)
		{
			case BPTYPE_Normal: return TEXT("Normal");
			case BPTYPE_Const: return TEXT("Const");
			case BPTYPE_Interface: return TEXT("Interface");
			case BPTYPE_FunctionLibrary: return TEXT("FunctionLibrary");
			case BPTYPE_MacroLibrary: return TEXT("MacroLibrary");
			default: return TEXT("Unknown");
		}
	}

	/** An asset (saved, or only in memory) already has this package and object path */
	bool DoesAssetExist(const FString& PackageName, const FString& ObjectPath)
	{
		return FPackageName::DoesPackageExist(PackageName) || FindObject<UObject>(nullptr, *ObjectPath) != nullptr;
	}
}

FMcpToolDefinition FN2CMcpCreateBlueprintClassTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("create-blueprint-class"),
		TEXT("Creates a new Blueprint class with the specified parent class and settings, then saves it. A UserWidget parent gives a Widget Blueprint"),
		N2CMcpToolsets::BlueprintClass
	);

	// Build input schema manually
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// blueprintName property
	TSharedPtr<FJsonObject> BlueprintNameProp = MakeShareable(new FJsonObject);
	BlueprintNameProp->SetStringField(TEXT("type"), TEXT("string"));
	BlueprintNameProp->SetStringField(TEXT("description"), TEXT("Name for the new Blueprint asset. With addPrefix (the default) BP_ is put in front unless the name already starts with it"));
	Properties->SetObjectField(TEXT("blueprintName"), BlueprintNameProp);

	// parentClassPath property
	TSharedPtr<FJsonObject> ParentClassPathProp = MakeShareable(new FJsonObject);
	ParentClassPathProp->SetStringField(TEXT("type"), TEXT("string"));
	ParentClassPathProp->SetStringField(TEXT("description"), TEXT("Path to the parent class (e.g., '/Script/Engine.Actor', '/Script/UMG.UserWidget' for a Widget Blueprint, '/Script/CoreUObject.Interface' for a Blueprint interface). AnimInstance parents are refused"));
	Properties->SetObjectField(TEXT("parentClassPath"), ParentClassPathProp);

	// assetPath property
	TSharedPtr<FJsonObject> AssetPathProp = MakeShareable(new FJsonObject);
	AssetPathProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetPathProp->SetStringField(TEXT("description"), TEXT("Content folder for the new Blueprint (e.g., '/Game/Blueprints/Doors'). An existing folder is used; a missing one is created"));
	Properties->SetObjectField(TEXT("assetPath"), AssetPathProp);

	// ifExists property
	TSharedPtr<FJsonObject> IfExistsProp = MakeShareable(new FJsonObject);
	IfExistsProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> IfExistsEnum;
	IfExistsEnum.Add(MakeShareable(new FJsonValueString(TEXT("error"))));
	IfExistsEnum.Add(MakeShareable(new FJsonValueString(TEXT("unique"))));
	IfExistsProp->SetArrayField(TEXT("enum"), IfExistsEnum);
	IfExistsProp->SetStringField(TEXT("default"), TEXT("error"));
	IfExistsProp->SetStringField(TEXT("description"), TEXT("When an asset of that name already exists in the folder: error fails, unique picks a free name (BP_Door1) and returns it as blueprintName"));
	Properties->SetObjectField(TEXT("ifExists"), IfExistsProp);

	// addPrefix property
	TSharedPtr<FJsonObject> AddPrefixProp = MakeShareable(new FJsonObject);
	AddPrefixProp->SetStringField(TEXT("type"), TEXT("boolean"));
	AddPrefixProp->SetBoolField(TEXT("default"), true);
	AddPrefixProp->SetStringField(TEXT("description"), TEXT("Put BP_ in front of blueprintName unless it already starts with it; false uses blueprintName as given"));
	Properties->SetObjectField(TEXT("addPrefix"), AddPrefixProp);

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
	BlueprintTypeProp->SetStringField(TEXT("description"), TEXT("Blueprint type, in any case. auto gives interface for the Interface parent, otherwise normal. interface needs parentClassPath '/Script/CoreUObject.Interface', and an interface parent needs interface. Other values are refused"));
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
		// ---- Arguments: everything is checked before anything is created ----
		FString BlueprintName;
		if (!Arguments->TryGetStringField(TEXT("blueprintName"), BlueprintName))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: blueprintName"));
		}
		BlueprintName.TrimStartAndEndInline();

		FString ParentClassPath;
		if (!Arguments->TryGetStringField(TEXT("parentClassPath"), ParentClassPath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: parentClassPath"));
		}
		ParentClassPath.TrimStartAndEndInline();

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: assetPath"));
		}
		AssetPath.TrimStartAndEndInline();

		bool bOpenInEditor = true;
		Arguments->TryGetBoolField(TEXT("openInEditor"), bOpenInEditor);

		bool bOpenInFullEditor = true;
		Arguments->TryGetBoolField(TEXT("openInFullEditor"), bOpenInFullEditor);

		bool bAddPrefix = true;
		Arguments->TryGetBoolField(TEXT("addPrefix"), bAddPrefix);

		FString IfExists = TEXT("error");
		Arguments->TryGetStringField(TEXT("ifExists"), IfExists);
		IfExists.TrimStartAndEndInline();
		bool bUniqueIfExists = false;
		if (IfExists.Equals(TEXT("unique"), ESearchCase::IgnoreCase))
		{
			bUniqueIfExists = true;
		}
		else if (!IfExists.IsEmpty() && !IfExists.Equals(TEXT("error"), ESearchCase::IgnoreCase))
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("Unknown ifExists '%s': use error or unique"), *IfExists));
		}

		FString Description;
		const bool bHasDescription = Arguments->TryGetStringField(TEXT("description"), Description);

		FString BlueprintTypeText = TEXT("auto");
		Arguments->TryGetStringField(TEXT("blueprintType"), BlueprintTypeText);

		// Parent class and type
		UClass* ParentClass = nullptr;
		FString ErrorMessage;
		if (!ValidateParentClass(ParentClassPath, ParentClass, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		EBlueprintType BlueprintType = BPTYPE_Normal;
		if (!ResolveBlueprintType(BlueprintTypeText, ParentClass, BlueprintType, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Name
		if (BlueprintName.IsEmpty())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("blueprintName must not be empty"));
		}
		FString AssetName = BlueprintName;
		if (bAddPrefix && !AssetName.StartsWith(TEXT("BP_")))
		{
			AssetName = FString::Printf(TEXT("BP_%s"), *AssetName);
		}
		if (!ValidateBlueprintName(AssetName, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Folder, package and existing assets
		FString Folder, PackageName;
		if (!ValidateAndPrepareAssetPath(AssetPath, AssetName, bUniqueIfExists, Folder, PackageName, AssetName, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// ---- Create ----
		UBlueprint* NewBlueprint = CreateBlueprintAsset(ParentClass, Folder, AssetName, BlueprintType,
			bHasDescription ? &Description : nullptr, ErrorMessage);
		if (!NewBlueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Compile once, so the description reaches the class and the result carries
		// the compiler's diagnostics
		FN2CCompileDiagnostics Diagnostics;
		FN2CMcpBlueprintUtils::Compile(NewBlueprint, Diagnostics);

		// Save the asset
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(NewBlueprint->GetOutermost());
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);

		TSharedPtr<FJsonObject> Result = BuildResult(NewBlueprint, PackageName, AssetName, bSaved);
		if (!bSaved)
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("create-blueprint-class: failed to save %s"), *PackageName));
			Result->SetStringField(TEXT("error"), FString::Printf(
				TEXT("The Blueprint was created in memory but its package could not be saved: %s. Save it from the editor, or check that the file is writable and not locked by source control"),
				*PackageName));
		}
		else
		{
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
		}

		// Convert result to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString, /*bInIsError*/ !bSaved);
	});
}

bool FN2CMcpCreateBlueprintClassTool::ValidateParentClass(const FString& ClassPath, UClass*& OutClass, FString& OutErrorMessage) const
{
	if (ClassPath.IsEmpty())
	{
		OutErrorMessage = TEXT("parentClassPath must not be empty");
		return false;
	}

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

	// An Animation Blueprint needs a target skeleton, which this tool has no argument for: without one the asset is
	// created broken
	if (OutClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		OutErrorMessage = FString::Printf(
			TEXT("Cannot create an Animation Blueprint from %s: an AnimInstance parent needs a target skeleton, which create-blueprint-class cannot set. Create Animation Blueprints in the Content Browser"),
			*ClassPath);
		OutClass = nullptr;
		return false;
	}

	return true;
}

bool FN2CMcpCreateBlueprintClassTool::ResolveBlueprintType(const FString& TypeText, const UClass* ParentClass,
	EBlueprintType& OutType, FString& OutErrorMessage) const
{
	const FString Type = TypeText.TrimStartAndEnd();
	const bool bInterfaceParent = ParentClass && ParentClass->HasAnyClassFlags(CLASS_Interface);

	if (Type.IsEmpty() || Type.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		OutType = bInterfaceParent ? BPTYPE_Interface : BPTYPE_Normal;
	}
	else if (Type.Equals(TEXT("normal"), ESearchCase::IgnoreCase))
	{
		OutType = BPTYPE_Normal;
	}
	else if (Type.Equals(TEXT("const"), ESearchCase::IgnoreCase))
	{
		OutType = BPTYPE_Const;
	}
	else if (Type.Equals(TEXT("interface"), ESearchCase::IgnoreCase))
	{
		OutType = BPTYPE_Interface;
	}
	else
	{
		OutErrorMessage = FString::Printf(TEXT("Unknown blueprintType '%s': use auto, normal, const or interface"), *TypeText);
		return false;
	}

	if (OutType == BPTYPE_Interface && ParentClass != UInterface::StaticClass())
	{
		OutErrorMessage = FString::Printf(
			TEXT("blueprintType interface needs parentClassPath '/Script/CoreUObject.Interface', not %s"),
			ParentClass ? *ParentClass->GetPathName() : TEXT("None"));
		return false;
	}
	if (OutType != BPTYPE_Interface && bInterfaceParent)
	{
		OutErrorMessage = FString::Printf(
			TEXT("The interface parent %s needs blueprintType interface (or auto), not %s"),
			*ParentClass->GetPathName(), *TypeText);
		return false;
	}

	return true;
}

bool FN2CMcpCreateBlueprintClassTool::ValidateBlueprintName(const FString& AssetName, FString& OutErrorMessage) const
{
	// The editor's Blueprint name rules (length, characters, None). FKismetNameValidator dereferences the Blueprint it
	// is given, and a new asset has none yet: the class default UBlueprint is an empty scope, so only the name itself
	// is checked
	FKismetNameValidator NameValidator(GetDefault<UBlueprint>());
	const EValidatorResult NameResult = NameValidator.IsValid(AssetName);
	if (NameResult != EValidatorResult::Ok)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid blueprintName '%s': %s"), *AssetName,
			*INameValidatorInterface::GetErrorString(AssetName, NameResult));
		return false;
	}

	// The asset name is also an object name and the last part of a package name and a file name
	FText Reason;
	const FString InvalidCharacters = FString(INVALID_OBJECTNAME_CHARACTERS) + INVALID_LONGPACKAGE_CHARACTERS;
	if (!FName::IsValidXName(AssetName, InvalidCharacters, &Reason))
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid blueprintName '%s': %s"), *AssetName, *Reason.ToString());
		return false;
	}
	if (!FFileHelper::IsFilenameValidForSaving(AssetName, Reason))
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid blueprintName '%s': %s"), *AssetName, *Reason.ToString());
		return false;
	}
	if (!GetAssetTools().IsNameAllowed(AssetName, &Reason))
	{
		OutErrorMessage = FString::Printf(TEXT("blueprintName '%s' is not allowed: %s"), *AssetName, *Reason.ToString());
		return false;
	}

	return true;
}

bool FN2CMcpCreateBlueprintClassTool::ValidateAndPrepareAssetPath(const FString& AssetPath, const FString& AssetName,
	bool bUniqueIfExists, FString& OutFolder, FString& OutPackageName, FString& OutAssetName, FString& OutErrorMessage) const
{
	OutFolder = FN2CMcpContentBrowserUtils::NormalizeContentPath(AssetPath);

	FString PathError;
	if (!FN2CMcpContentBrowserUtils::ValidateContentPath(OutFolder, PathError))
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid assetPath: %s"), *PathError);
		return false;
	}
	// Only where MCP tools may write: /Game and the project's plugins (card 0182)
	if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(OutFolder, PathError))
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid assetPath: %s"), *PathError);
		return false;
	}

	OutAssetName = AssetName;
	OutPackageName = OutFolder / OutAssetName;

	FText Reason;
	if (!FPackageName::IsValidLongPackageName(OutPackageName, /*bIncludeReadOnlyRoots*/ false, &Reason))
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid package name %s: %s"), *OutPackageName, *Reason.ToString());
		return false;
	}

	// IAssetTools::CreateAsset would ask in a modal dialog whether to overwrite an existing asset: decide here instead
	FString ObjectPath = OutPackageName + TEXT(".") + OutAssetName;
	if (DoesAssetExist(OutPackageName, ObjectPath))
	{
		if (!bUniqueIfExists)
		{
			OutErrorMessage = FString::Printf(
				TEXT("An asset already exists at %s. Pass ifExists 'unique' for a free name, or choose another blueprintName"),
				*ObjectPath);
			return false;
		}

		GetAssetTools().CreateUniqueAssetName(OutFolder / AssetName, FString(), OutPackageName, OutAssetName);
		ObjectPath = OutPackageName + TEXT(".") + OutAssetName;
		if (DoesAssetExist(OutPackageName, ObjectPath))
		{
			OutErrorMessage = FString::Printf(TEXT("Could not find a free name for %s in %s"), *AssetName, *OutFolder);
			return false;
		}
		FString UniqueNameError;
		if (!ValidateBlueprintName(OutAssetName, UniqueNameError))
		{
			OutErrorMessage = UniqueNameError;
			return false;
		}
	}

	if (!GetAssetTools().IsObjectPathAllowed(OutPackageName, &Reason))
	{
		OutErrorMessage = FString::Printf(TEXT("Creating %s is not allowed: %s"), *OutPackageName, *Reason.ToString());
		return false;
	}

	return true;
}

UBlueprint* FN2CMcpCreateBlueprintClassTool::CreateBlueprintAsset(UClass* ParentClass, const FString& Folder,
	const FString& AssetName, EBlueprintType BlueprintType, const FString* Description, FString& OutErrorMessage) const
{
	// The folder may exist already; a missing one is created
	FString DirectoryError;
	if (!FN2CMcpContentBrowserUtils::EnsureDirectoryExists(Folder, DirectoryError))
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to create folder %s: %s"), *Folder, *DirectoryError);
		return nullptr;
	}

	// No transaction, as when the Content Browser creates an asset: undoing a transaction that created the Blueprint
	// destroys it in memory (an ensure in the Blueprint compilation manager) while its saved package stays on disk
	// (card 0166, NodeToCode.MCP.CreateBlueprintClass)

	// UBlueprintFactory asks the Kismet compiler which Blueprint and generated class the parent needs
	// (GetBlueprintTypesForClass), so a UserWidget parent gives a UWidgetBlueprint; IAssetTools registers the asset
	// and runs the editor's asset-created hooks
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	Factory->BlueprintType = BlueprintType;
	Factory->bSkipClassPicker = true;

	UObject* NewAsset = GetAssetTools().CreateAsset(AssetName, Folder, UBlueprint::StaticClass(), Factory, FName(TEXT("NodeToCode")));
	UBlueprint* NewBlueprint = Cast<UBlueprint>(NewAsset);
	if (!NewBlueprint)
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to create Blueprint %s in %s"), *AssetName, *Folder);
		return nullptr;
	}

	if (Description)
	{
		NewBlueprint->BlueprintDescription = *Description;
	}

	return NewBlueprint;
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

TSharedPtr<FJsonObject> FN2CMcpCreateBlueprintClassTool::BuildResult(const UBlueprint* Blueprint,
	const FString& PackageName, const FString& AssetName, bool bSaved) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	Result->SetBoolField(TEXT("success"), bSaved);
	Result->SetBoolField(TEXT("saved"), bSaved);
	Result->SetStringField(TEXT("blueprintName"), AssetName);
	Result->SetStringField(TEXT("packagePath"), PackageName);
	Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());

	// Add Blueprint info
	TSharedPtr<FJsonObject> BlueprintInfo = MakeShareable(new FJsonObject);
	BlueprintInfo->SetStringField(TEXT("className"), Blueprint->GetName());
	BlueprintInfo->SetStringField(TEXT("assetClass"), Blueprint->GetClass()->GetName());
	BlueprintInfo->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));
	BlueprintInfo->SetStringField(TEXT("blueprintType"), BlueprintTypeToString(Blueprint->BlueprintType));

	// Add generated class info
	if (Blueprint->GeneratedClass)
	{
		BlueprintInfo->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
	}

	Result->SetObjectField(TEXT("blueprintInfo"), BlueprintInfo);

	if (bSaved)
	{
		// Add helpful next steps
		TArray<TSharedPtr<FJsonValue>> NextSteps;
		NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'create-blueprint-function' to add functions"))));
		NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'create-variable' to add member variables"))));
		NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'add-bp-node-to-active-graph' to add nodes"))));
		NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'translate-focused-blueprint' to generate code"))));
		Result->SetArrayField(TEXT("nextSteps"), NextSteps);
	}

	return Result;
}
