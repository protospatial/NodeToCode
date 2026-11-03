// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Subsystems/N2CGraphStateSubsystem.h"

#include "Core/N2CGraphStateManager.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"

void UN2CGraphStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Loads the state file (or migrates the legacy tags) at once, then delays the saves that follow
	Manager = NewObject<UN2CGraphStateManager>(this, UN2CGraphStateManager::StaticClass(), NAME_None, RF_Transient);
	Manager->Initialize();
	Manager->SetSaveDelay(EditorSaveDelaySeconds);
	FN2CLogger::Get().Log(TEXT("N2C Graph State Subsystem initialized"), EN2CLogSeverity::Info);
}

void UN2CGraphStateSubsystem::Deinitialize()
{
	if (Manager)
	{
		// Writes a change still waiting for its delayed save
		Manager->Shutdown();
		Manager = nullptr;
	}
	Override.Reset();
	Super::Deinitialize();
}

UN2CGraphStateSubsystem* UN2CGraphStateSubsystem::Get()
{
	return GEditor ? GEditor->GetEditorSubsystem<UN2CGraphStateSubsystem>() : nullptr;
}

UN2CGraphStateManager* UN2CGraphStateSubsystem::GetManager() const
{
	if (UN2CGraphStateManager* Replacement = Override.Get())
	{
		return Replacement;
	}
	return Manager;
}

bool UN2CGraphStateSubsystem::SaveNow()
{
	UN2CGraphStateManager* Store = GetManager();
	if (!Store)
	{
		return false;
	}
	return !Store->IsDirty() || Store->SaveState();
}

bool UN2CGraphStateSubsystem::HasUnsavedChanges() const
{
	const UN2CGraphStateManager* Store = GetManager();
	return Store && Store->IsDirty();
}

FString UN2CGraphStateSubsystem::GetStateFilePath() const
{
	const UN2CGraphStateManager* Store = GetManager();
	return Store ? Store->GetStateFilePath() : FString();
}

int32 UN2CGraphStateSubsystem::GetTagCount() const
{
	const UN2CGraphStateManager* Store = GetManager();
	return Store ? Store->GetAllTags().Num() : 0;
}
