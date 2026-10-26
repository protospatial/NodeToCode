// Copyright (c) 2025 Protospatial. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

class UBlueprint;
class UPackage;
class FJsonObject;

/**
 * What save-blueprint knows about a package's file before it writes it (card 0290): on disk, and in source control
 * when a provider is enabled and available. Read without prompting; the source-control fields stay false otherwise.
 */
struct FN2CSaveFileState
{
    /** Absolute file name, or empty when the package has never been saved */
    FString Filename;
    bool bFileExists = false;
    bool bReadOnly = false;

    /** A provider is enabled and available, and the fields below come from it */
    bool bSourceControlAvailable = false;
    FString Provider;
    bool bUsesLocalReadOnlyState = false;
    bool bHasState = false;
    bool bSourceControlled = false;
    bool bCheckedOut = false;
    bool bAdded = false;
    bool bDeleted = false;
    bool bCanCheckIn = false;
    bool bCanCheckOut = false;
    bool bIgnored = false;
    bool bUnknown = false;
    bool bCurrent = true;
    bool bCheckedOutByOther = false;
    FString OtherUser;

    /** save-blueprint checked the file out itself, without a dialog */
    bool bCheckedOutNow = false;
};

/** What save-blueprint does with a file in a given state */
struct FN2CSaveDecision
{
    enum class EAction : uint8
    {
        Save,
        CheckOut,
        Refuse
    };

    EAction Action = EAction::Save;
    /** CHECKOUT_REQUIRED, CHECKED_OUT_BY_OTHER or READ_ONLY when refused */
    FString ErrorCode;
    FString Message;
};

/**
 * MCP tool for saving Blueprints to disk
 * Provides functionality to save a Blueprint asset, marking the package as saved. Never prompts: a file that needs a
 * checkout is checked out without a dialog, and one that cannot be written is refused with an error code (card 0290).
 */
class FN2CMcpSaveBlueprint : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

    /**
     * What to do with a file in State, following the editor's own rules for when a package needs a checkout
     * (FEditorFileUtils::AddCheckoutPackageItems), but refusing where the editor would show a dialog
     */
    static FN2CSaveDecision Decide(const FN2CSaveFileState& State);

    /** Reads the package's file state from disk and, when a provider is available, from source control (a status update, no dialog) */
    static FN2CSaveFileState ReadFileState(const UPackage* Package);

    /** The sourceControl object of a result */
    static TSharedPtr<FJsonObject> FileStateToJson(const FN2CSaveFileState& State);

private:
    /**
     * Saves the Blueprint asset to disk without any dialog: checks the file out first when source control needs it
     * @param Blueprint The Blueprint to save
     * @param OutState The file state, updated by the checkout
     * @param OutError "CODE: message" if the save is refused or fails
     * @return True if save succeeded, false otherwise
     */
    bool SaveBlueprintAsset(UBlueprint* Blueprint, FN2CSaveFileState& OutState, FString& OutError);

    /**
     * Builds the save result JSON object
     * @param Blueprint The saved Blueprint
     * @param bSuccess Whether the save succeeded
     * @param Message Status message
     * @return JSON object with save results
     */
    TSharedPtr<FJsonObject> BuildSaveResult(
        UBlueprint* Blueprint,
        bool bSuccess,
        const FString& Message
    );
};
