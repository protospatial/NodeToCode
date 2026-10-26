// Copyright (c) 2025 Protospatial. All Rights Reserved.

#include "N2CMcpSaveBlueprint.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SourceControlOperations.h"
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
        TEXT("Save a Blueprint asset to disk, writing the package file. Never prompts: a file under source control is checked out first, "
             "and a file that cannot be written returns CHECKOUT_REQUIRED, CHECKED_OUT_BY_OTHER or READ_ONLY."),
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

        // Only where MCP tools may write: /Game and the project's plugins, not an engine or another plugin's
        // Blueprint (card 0182)
        FString PathError;
        if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(Package->GetName(), PathError))
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("SAVE_REFUSED: %s"), *PathError));
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

        // Save the blueprint, checking it out first when source control needs it; never a dialog (card 0290)
        FN2CSaveFileState FileState;
        bool bSaveSucceeded = SaveBlueprintAsset(Blueprint, FileState, ErrorMessage);

        if (!bSaveSucceeded)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
        }

        // Show notification
        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "BlueprintSaved", "Blueprint '{0}' saved successfully"),
            FText::FromString(Blueprint->GetName())
        ));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);

        // Build and return result
        TSharedPtr<FJsonObject> Result = BuildSaveResult(Blueprint, true, FileState.bCheckedOutNow
            ? TEXT("Blueprint checked out and saved successfully")
            : TEXT("Blueprint saved successfully"));
        Result->SetObjectField(TEXT("sourceControl"), FileStateToJson(FileState));

        // Convert JSON object to string for text result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

FN2CSaveFileState FN2CMcpSaveBlueprint::ReadFileState(const UPackage* Package)
{
    FN2CSaveFileState State;
    if (!Package)
    {
        return State;
    }

    FString ExistingFilename;
    State.bFileExists = FPackageName::DoesPackageExist(Package->GetName(), &ExistingFilename);
    if (State.bFileExists)
    {
        State.Filename = FPaths::ConvertRelativePathToFull(ExistingFilename);
        State.bReadOnly = IFileManager::Get().IsReadOnly(*State.Filename);
    }

    ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
    if (!SourceControlModule.IsEnabled() || !SourceControlModule.GetProvider().IsAvailable())
    {
        return State;
    }
    ISourceControlProvider& Provider = SourceControlModule.GetProvider();
    State.bSourceControlAvailable = true;
    State.Provider = Provider.GetName().ToString();
    State.bUsesLocalReadOnlyState = Provider.UsesLocalReadOnlyState();
    if (!State.bFileExists)
    {
        // A new package: the save marks it for add
        return State;
    }

    // ForceUpdate runs an FUpdateStatus now, as the editor does before its checkout dialog, so a lock taken since the
    // cache was filled is seen
    const FSourceControlStatePtr SourceControlState = Provider.GetState(
        FN2CMcpContentBrowserUtils::SourceControlFilename(Package->GetName()), EStateCacheUsage::ForceUpdate);
    if (!SourceControlState.IsValid())
    {
        return State;
    }
    State.bHasState = true;
    State.bSourceControlled = SourceControlState->IsSourceControlled();
    State.bCheckedOut = SourceControlState->IsCheckedOut();
    State.bAdded = SourceControlState->IsAdded();
    State.bDeleted = SourceControlState->IsDeleted();
    State.bCanCheckIn = SourceControlState->CanCheckIn();
    State.bCanCheckOut = SourceControlState->CanCheckout();
    State.bIgnored = SourceControlState->IsIgnored();
    State.bUnknown = SourceControlState->IsUnknown();
    State.bCurrent = SourceControlState->IsCurrent();
    State.bCheckedOutByOther = SourceControlState->IsCheckedOutOther(&State.OtherUser);
    return State;
}

FN2CSaveDecision FN2CMcpSaveBlueprint::Decide(const FN2CSaveFileState& State)
{
    FN2CSaveDecision Decision;
    auto Refuse = [&Decision](const TCHAR* Code, const FString& Message)
    {
        Decision.Action = FN2CSaveDecision::EAction::Refuse;
        Decision.ErrorCode = Code;
        Decision.Message = Message;
        return Decision;
    };

    // Never saved: nothing to check out or make writable
    if (!State.bFileExists)
    {
        return Decision;
    }

    if (State.bSourceControlAvailable && State.bHasState)
    {
        // The editor's rule (FEditorFileUtils::AddCheckoutPackageItems): a file it can already edit needs no checkout
        const bool bCanEdit = (State.bCanCheckIn && !State.bDeleted) || State.bIgnored || State.bUnknown
            || (State.bUsesLocalReadOnlyState && !State.bReadOnly);
        if (!bCanEdit && State.bSourceControlled && !State.bCheckedOut)
        {
            if (State.bCheckedOutByOther)
            {
                return Refuse(TEXT("CHECKED_OUT_BY_OTHER"), FString::Printf(
                    TEXT("%s is checked out by %s in %s. Ask them to check it in, then call save-blueprint again."),
                    *State.Filename, State.OtherUser.IsEmpty() ? TEXT("another user") : *State.OtherUser, *State.Provider));
            }
            if (State.bDeleted)
            {
                return Refuse(TEXT("CHECKOUT_REQUIRED"), FString::Printf(
                    TEXT("%s is marked for delete in %s. Revert the delete in the editor, then call save-blueprint again."),
                    *State.Filename, *State.Provider));
            }
            if (!State.bCurrent)
            {
                return Refuse(TEXT("CHECKOUT_REQUIRED"), FString::Printf(
                    TEXT("%s is not at the latest revision in %s, so it cannot be checked out without losing someone else's change. Sync it in the editor, then call save-blueprint again."),
                    *State.Filename, *State.Provider));
            }
            if (State.bCanCheckOut)
            {
                Decision.Action = FN2CSaveDecision::EAction::CheckOut;
                return Decision;
            }
            return Refuse(TEXT("CHECKOUT_REQUIRED"), FString::Printf(
                TEXT("%s is under %s and cannot be checked out. Check it out in the editor, then call save-blueprint again."),
                *State.Filename, *State.Provider));
        }
    }

    if (State.bReadOnly)
    {
        return Refuse(TEXT("READ_ONLY"), FString::Printf(
            TEXT("%s is read-only on disk%s. Check it out or make it writable, then call save-blueprint again."),
            *State.Filename, State.bSourceControlAvailable ? TEXT("") : TEXT(" and no source control provider is connected")));
    }
    return Decision;
}

TSharedPtr<FJsonObject> FN2CMcpSaveBlueprint::FileStateToJson(const FN2CSaveFileState& State)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("enabled"), State.bSourceControlAvailable);
    Json->SetStringField(TEXT("provider"), State.bSourceControlAvailable ? State.Provider : FString());

    FString Status = TEXT("none");
    if (State.bSourceControlAvailable)
    {
        if (!State.bFileExists || !State.bHasState)
        {
            Status = TEXT("unknown");
        }
        if (State.bAdded)
        {
            Status = TEXT("markedForAdd");
        }
        else if (State.bCheckedOut)
        {
            Status = TEXT("checkedOut");
        }
        else if (State.bCheckedOutByOther)
        {
            Status = TEXT("checkedOutByOther");
        }
        else if (State.bHasState && State.bSourceControlled)
        {
            Status = TEXT("notCheckedOut");
        }
        else if (State.bHasState)
        {
            Status = TEXT("notControlled");
        }
    }
    Json->SetStringField(TEXT("status"), Status);
    Json->SetBoolField(TEXT("checkedOutNow"), State.bCheckedOutNow);
    Json->SetBoolField(TEXT("readOnly"), State.bReadOnly);
    return Json;
}

bool FN2CMcpSaveBlueprint::SaveBlueprintAsset(UBlueprint* Blueprint, FN2CSaveFileState& OutState, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("INTERNAL_ERROR: Invalid Blueprint");
        return false;
    }

    UPackage* Package = Blueprint->GetPackage();
    if (!Package)
    {
        OutError = TEXT("INTERNAL_ERROR: Blueprint has no package");
        return false;
    }

    // Log the save attempt
    FN2CLogger::Get().Log(FString::Printf(
        TEXT("Attempting to save Blueprint: %s"),
        *Blueprint->GetPathName()), EN2CLogSeverity::Info);

    // FEditorFileUtils::PromptForCheckoutAndSave would show the 'Check Out Assets' or make-writable dialog here, and
    // its failure dialog after: modal on the game thread, so an unattended call hangs until someone clicks (card 0290).
    // Check the file out ourselves, refuse what needs a person, and save without dialogs.
    OutState = ReadFileState(Package);
    FN2CSaveDecision Decision = Decide(OutState);

    if (Decision.Action == FN2CSaveDecision::EAction::CheckOut)
    {
        ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
        TSharedRef<FCheckOut, ESPMode::ThreadSafe> CheckOut = ISourceControlOperation::Create<FCheckOut>();
        const ECommandResult::Type CheckOutResult = Provider.Execute(CheckOut,
            FN2CMcpContentBrowserUtils::SourceControlFilename(Package->GetName()), EConcurrency::Synchronous);
        if (CheckOutResult != ECommandResult::Succeeded)
        {
            FString Reason;
            for (const FText& Error : CheckOut->GetResultInfo().ErrorMessages)
            {
                Reason += (Reason.IsEmpty() ? TEXT(" ") : TEXT("; ")) + Error.ToString();
            }
            OutError = FString::Printf(TEXT("CHECKOUT_REQUIRED: %s could not be checked out from %s.%s Check it out in the editor, then call save-blueprint again."),
                *OutState.Filename, *OutState.Provider, *Reason);
            return false;
        }

        // Read the file again: a provider that uses the read-only flag has made it writable
        OutState = ReadFileState(Package);
        OutState.bCheckedOutNow = true;
        Decision = Decide(OutState);
        if (Decision.Action == FN2CSaveDecision::EAction::CheckOut)
        {
            OutError = FString::Printf(TEXT("CHECKOUT_REQUIRED: %s still reports not checked out in %s after a checkout."),
                *OutState.Filename, *OutState.Provider);
            return false;
        }
    }

    if (Decision.Action == FN2CSaveDecision::EAction::Refuse)
    {
        OutError = FString::Printf(TEXT("%s: %s"), *Decision.ErrorCode, *Decision.Message);
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("save-blueprint did not save %s: %s"), *Blueprint->GetPathName(), *OutError));
        return false;
    }

    // The non-interactive editor save: no checkout prompt, no 'failed to save, retry?' dialog, and a new file is
    // marked for add when source control is on
    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty*/ false))
    {
        const FString Filename = OutState.Filename.IsEmpty()
            ? FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension())
            : OutState.Filename;
        OutError = FString::Printf(TEXT("SAVE_FAILED: The editor could not write %s; the Output Log (LogSavePackage) has the reason."), *Filename);
        return false;
    }

    // The file the save wrote, and its state now (for a new package: marked for add)
    const bool bCheckedOutNow = OutState.bCheckedOutNow;
    OutState = ReadFileState(Package);
    OutState.bCheckedOutNow = bCheckedOutNow;

    FN2CLogger::Get().Log(FString::Printf(
        TEXT("Successfully saved Blueprint: %s"),
        *Blueprint->GetPathName()), EN2CLogSeverity::Info);
    return true;
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
