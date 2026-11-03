// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "N2CGraphStateSubsystem.generated.h"

class UN2CGraphStateManager;

/**
 * @class UN2CGraphStateSubsystem
 * @brief Owns the editor's tag and graph state store for the editor's lifetime
 *
 * Initialize makes the store and loads BlueprintGraphState.json (migrating legacy tags when there is none);
 * Deinitialize writes any change still waiting. In the editor the store's saves are delayed by
 * EditorSaveDelaySeconds, so many changes in a row (a bulk tag, a migration) write the file once. From Python:
 * unreal.get_editor_subsystem(unreal.N2CGraphStateSubsystem).
 */
UCLASS(BlueprintType)
class NODETOCODE_API UN2CGraphStateSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The editor's instance; null without an editor (or after it shut down) */
	static UN2CGraphStateSubsystem* Get();

	/** The store: the one a spec put in place, else this subsystem's own; null once deinitialized */
	UN2CGraphStateManager* GetManager() const;

	/** Writes the graph state file now if it has changes not yet written. False when it could not be written. */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Graph State")
	bool SaveNow();

	/** True while a change has not been written to the graph state file yet */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Graph State")
	bool HasUnsavedChanges() const;

	/** Full path of BlueprintGraphState.json */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Graph State")
	FString GetStateFilePath() const;

	/** How many tags the store holds, over every graph */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Graph State")
	int32 GetTagCount() const;

	/**
	 * Specs: GetManager() (and so UN2CGraphStateManager::Get()) returns InManager instead of the subsystem's own, until
	 * this is called again with null. The caller keeps InManager alive (an isolated manager in a TStrongObjectPtr).
	 */
	void SetManagerOverride(UN2CGraphStateManager* InManager) { Override = InManager; }

	/** Seconds between a change and its write in the editor */
	static constexpr float EditorSaveDelaySeconds = 1.0f;

private:
	UPROPERTY()
	TObjectPtr<UN2CGraphStateManager> Manager;

	TWeakObjectPtr<UN2CGraphStateManager> Override;
};
