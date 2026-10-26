// Copyright (c) 2025 Protospatial. All Rights Reserved.

#include "N2CMcpSaveBlueprint.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpSaveBlueprint)

FMcpToolDefinition FN2CMcpSaveBlueprint::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("save-blueprint"),
        TEXT("Save a Blueprint asset to disk, writing the package file"),
        N2CMcpToolsets::BlueprintCompile
    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("blueprintPath"), TEXT("string"));
    Properties.Add(TEXT("saveOnlyIfDirty"), TEXT("boolean"));
    
    TArray<FString> Required; // No required fields - uses focused blueprint if path not provided
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpSaveBlueprint::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FN2CMcpArgumentParser Parser(Arguments);
        FString BlueprintPath = Parser.GetOptionalString(TEXT("blueprintPath"));
        bool bSaveOnlyIfDirty = Parser.GetOptionalBool(TEXT("saveOnlyIfDirty"), true); // Default to true for efficiency
        
        // Resolve Blueprint
        FString ErrorMessage;
        UBlueprint* Blueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ErrorMessage);
        
        if (!Blueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
        }
        
        // Get the package
        UPackage* Package = Blueprint->GetPackage();
        if (!Package)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("INTERNAL_ERROR: Blueprint has no package"));
        }
        
        // Check if we should skip saving
        if (bSaveOnlyIfDirty && !Package->IsDirty())
        {
            TSharedPtr<FJsonObject> Result = BuildSaveResult(Blueprint, true, TEXT("Blueprint is already saved (not dirty)"));
            
            FString OutputString;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
            FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
            
            return FMcpToolCallResult::CreateTextResult(OutputString);
        }
        
        // Save the blueprint
        bool bSaveSucceeded = SaveBlueprintAsset(Blueprint, ErrorMessage);
        
        if (!bSaveSucceeded)
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("SAVE_FAILED: %s"), *ErrorMessage));
        }
        
        // Show notification
        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "BlueprintSaved", "Blueprint '{0}' saved successfully"),
            FText::FromString(Blueprint->GetName())
        ));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Build and return result
        TSharedPtr<FJsonObject> Result = BuildSaveResult(Blueprint, true, TEXT("Blueprint saved successfully"));
        
        // Convert JSON object to string for text result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

bool FN2CMcpSaveBlueprint::SaveBlueprintAsset(UBlueprint* Blueprint, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Invalid Blueprint");
        return false;
    }
    
    UPackage* Package = Blueprint->GetPackage();
    if (!Package)
    {
        OutError = TEXT("Blueprint has no package");
        return false;
    }
    
    // Log the save attempt
    FN2CLogger::Get().Log(FString::Printf(
        TEXT("Attempting to save Blueprint: %s"), 
        *Blueprint->GetPathName()), EN2CLogSeverity::Info);
    
    // Use FEditorFileUtils to save the package
    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    
    // Set up save parameters
    FEditorFileUtils::FPromptForCheckoutAndSaveParams SaveParams;
    SaveParams.bCheckDirty = false; // We already checked if needed
    SaveParams.bPromptToSave = false; // Don't show dialog
    SaveParams.bAlreadyCheckedOut = false; // Let it handle checkout if needed
    SaveParams.bIsExplicitSave = true; // Mark as explicit save
    
    TArray<UPackage*> FailedPackages;
    SaveParams.OutFailedPackages = &FailedPackages;
    
    // Save the package
    FEditorFileUtils::EPromptReturnCode SaveResult = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, SaveParams);
    
    if (SaveResult == FEditorFileUtils::PR_Success)
    {
        FN2CLogger::Get().Log(FString::Printf(
            TEXT("Successfully saved Blueprint: %s"), 
            *Blueprint->GetPathName()), EN2CLogSeverity::Info);
        return true;
    }
    else if (SaveResult == FEditorFileUtils::PR_Cancelled)
    {
        OutError = TEXT("Save was cancelled");
        return false;
    }
    else if (SaveResult == FEditorFileUtils::PR_Declined)
    {
        OutError = TEXT("User declined to save");
        return false;
    }
    else // PR_Failure
    {
        if (FailedPackages.Num() > 0)
        {
            OutError = FString::Printf(TEXT("Failed to save package: %s"), *FailedPackages[0]->GetName());
        }
        else
        {
            OutError = TEXT("Failed to save package");
        }
        return false;
    }
}

TSharedPtr<FJsonObject> FN2CMcpSaveBlueprint::BuildSaveResult(
    UBlueprint* Blueprint,
    bool bSuccess,
    const FString& Message)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    
    Result->SetBoolField(TEXT("success"), bSuccess);
    Result->SetStringField(TEXT("message"), Message);
    Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
    Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
    
    // Add package info
    if (UPackage* Package = Blueprint->GetPackage())
    {
        TSharedPtr<FJsonObject> PackageInfo = MakeShareable(new FJsonObject);
        PackageInfo->SetStringField(TEXT("packageName"), Package->GetName());
        PackageInfo->SetBoolField(TEXT("isDirty"), Package->IsDirty());
        PackageInfo->SetStringField(TEXT("fileName"), Package->GetLoadedPath().GetLocalFullPath());
        Result->SetObjectField(TEXT("packageInfo"), PackageInfo);
    }
    
    // Add timestamp
    Result->SetStringField(TEXT("timestamp"), FDateTime::Now().ToString());
    
    return Result;
}