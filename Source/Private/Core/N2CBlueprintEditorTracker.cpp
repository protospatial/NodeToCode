// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CBlueprintEditorTracker.h"

#include "BlueprintEditor.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/ToolkitManager.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Docking/SDockTab.h"

namespace N2CBlueprintEditorTrackerPrivate
{
	/** How many activated editors the history keeps for the fallback when the focused one closes */
	constexpr int32 MaxHistory = 32;

	FString DescribeEditor(const TSharedPtr<FBlueprintEditor>& Editor)
	{
		const UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
		return Blueprint ? Blueprint->GetPathName() : FString(TEXT("(none)"));
	}
}

FN2CBlueprintEditorTracker& FN2CBlueprintEditorTracker::Get()
{
	static FN2CBlueprintEditorTracker Instance;
	return Instance;
}

void FN2CBlueprintEditorTracker::Initialize()
{
	if (!OnTabForegroundedHandle.IsValid())
	{
		// Bringing a tab to the front of its tab well: clicking an asset editor's tab or a graph document's tab
		OnTabForegroundedHandle = FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(
			FOnActiveTabChanged::FDelegate::CreateRaw(this, &FN2CBlueprintEditorTracker::OnTabForegrounded));
	}
	if (!OnActiveTabChangedHandle.IsValid())
	{
		// Clicking or focusing inside a tab (a graph, Details, My Blueprint) makes it the active tab
		OnActiveTabChangedHandle = FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(
			FOnActiveTabChanged::FDelegate::CreateRaw(this, &FN2CBlueprintEditorTracker::OnActiveTabChanged));
	}
	if (GEditor && !OnAssetOpenedInEditorHandle.IsValid())
	{
		// Fires for every editor opened, also when several assets open at once (OnAssetEditorOpened does not then)
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			OnAssetOpenedInEditorHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(
				this, &FN2CBlueprintEditorTracker::OnAssetOpenedInEditor);
		}
	}
}

void FN2CBlueprintEditorTracker::Shutdown()
{
	if (OnTabForegroundedHandle.IsValid())
	{
		FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(OnTabForegroundedHandle);
		OnTabForegroundedHandle.Reset();
	}
	if (OnActiveTabChangedHandle.IsValid())
	{
		FGlobalTabmanager::Get()->OnActiveTabChanged_Unsubscribe(OnActiveTabChangedHandle);
		OnActiveTabChangedHandle.Reset();
	}
	if (OnAssetOpenedInEditorHandle.IsValid())
	{
		if (GEditor)
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(OnAssetOpenedInEditorHandle);
			}
		}
		OnAssetOpenedInEditorHandle.Reset();
	}
	ActivationHistory.Empty();
}

TSharedPtr<FBlueprintEditor> FN2CBlueprintEditorTracker::GetEditorFor(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	const TSharedPtr<IToolkit> Toolkit = FToolkitManager::Get().FindEditorForAsset(Blueprint);
	if (!Toolkit.IsValid() || !Toolkit->IsBlueprintEditor())
	{
		return nullptr;
	}
	return StaticCastSharedPtr<FBlueprintEditor>(Toolkit);
}

TArray<TSharedPtr<FBlueprintEditor>> FN2CBlueprintEditorTracker::GetOpenBlueprintEditors()
{
	TArray<TSharedPtr<FBlueprintEditor>> Editors;
	if (!GEditor)
	{
		return Editors;
	}
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return Editors;
	}
	for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
	{
		if (const TSharedPtr<FBlueprintEditor> Editor = GetEditorFor(Cast<UBlueprint>(Asset)))
		{
			Editors.AddUnique(Editor);
		}
	}
	return Editors;
}

TSharedPtr<FBlueprintEditor> FN2CBlueprintEditorTracker::FindEditorForTab(const TSharedPtr<SDockTab>& Tab)
{
	if (!Tab.IsValid())
	{
		return nullptr;
	}

	// A graph document tab: the editor of the Blueprint that owns the graph it shows
	const TSharedRef<SWidget> Content = Tab->GetContent();
	if (Content->GetType() == FName(TEXT("SGraphEditor")))
	{
		if (UEdGraph* Graph = StaticCastSharedRef<SGraphEditor>(Content)->GetCurrentGraph())
		{
			if (const TSharedPtr<FBlueprintEditor> Editor = GetEditorFor(FBlueprintEditorUtils::FindBlueprintForGraph(Graph)))
			{
				return Editor;
			}
		}
	}

	// The editor's own asset tab, or any tab its tab manager holds (Details, My Blueprint, a graph not resolved above)
	const TSharedPtr<FTabManager> TabsManager = Tab->GetTabManagerPtr();
	for (const TSharedPtr<FBlueprintEditor>& Editor : GetOpenBlueprintEditors())
	{
		const TSharedPtr<FTabManager> EditorTabManager = Editor->GetTabManager();
		if (!EditorTabManager.IsValid())
		{
			continue;
		}
		if (EditorTabManager->GetOwnerTab() == Tab || (TabsManager.IsValid() && TabsManager == EditorTabManager))
		{
			return Editor;
		}
	}
	return nullptr;
}

void FN2CBlueprintEditorTracker::NotifyEditorActivated(const TSharedPtr<FBlueprintEditor>& Editor)
{
	if (!Editor.IsValid())
	{
		return;
	}

	const bool bChanged = ActivationHistory.IsEmpty() || ActivationHistory.Last().Pin() != Editor;
	ActivationHistory.RemoveAll([&Editor](const TWeakPtr<FBlueprintEditor>& Entry)
	{
		const TSharedPtr<FBlueprintEditor> Pinned = Entry.Pin();
		return !Pinned.IsValid() || Pinned == Editor;
	});
	ActivationHistory.Add(Editor);
	if (ActivationHistory.Num() > N2CBlueprintEditorTrackerPrivate::MaxHistory)
	{
		ActivationHistory.RemoveAt(0, ActivationHistory.Num() - N2CBlueprintEditorTrackerPrivate::MaxHistory);
	}

	if (bChanged)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Focused Blueprint editor: %s"),
			*N2CBlueprintEditorTrackerPrivate::DescribeEditor(Editor)), EN2CLogSeverity::Info);
	}
}

TSharedPtr<FBlueprintEditor> FN2CBlueprintEditorTracker::HandleTabActivated(const TSharedPtr<SDockTab>& Tab)
{
	const TSharedPtr<FBlueprintEditor> Editor = FindEditorForTab(Tab);
	if (!Editor.IsValid())
	{
		return nullptr; // not a Blueprint editor's tab: the focus stays where it was
	}

	// A tab inside an editor whose asset tab is behind another one is not what the user is looking at (a background
	// document refresh also raises these events)
	const TSharedPtr<FTabManager> EditorTabManager = Editor->GetTabManager();
	const TSharedPtr<SDockTab> OwnerTab = EditorTabManager.IsValid() ? EditorTabManager->GetOwnerTab() : nullptr;
	if (OwnerTab.IsValid() && OwnerTab != Tab && !OwnerTab->IsForeground())
	{
		return nullptr;
	}

	NotifyEditorActivated(Editor);
	return Editor;
}

bool FN2CBlueprintEditorTracker::IsOpen(const TSharedPtr<FBlueprintEditor>& Editor)
{
	return Editor.IsValid() && GetEditorFor(Editor->GetBlueprintObj()) == Editor;
}

TSharedPtr<FBlueprintEditor> FN2CBlueprintEditorTracker::GetFocusedEditor() const
{
	// The most recently activated editor that is still open
	for (int32 Index = ActivationHistory.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<FBlueprintEditor> Editor = ActivationHistory[Index].Pin();
		if (IsOpen(Editor))
		{
			return Editor;
		}
		ActivationHistory.RemoveAt(Index);
	}

	// None recorded is open (all closed, or opened before the tracker started): the latest activated open one
	TSharedPtr<FBlueprintEditor> Latest;
	double LatestTime = -1.0;
	for (const TSharedPtr<FBlueprintEditor>& Editor : GetOpenBlueprintEditors())
	{
		const double Time = Editor->GetLastActivationTime();
		if (Time > LatestTime)
		{
			LatestTime = Time;
			Latest = Editor;
		}
	}
	return Latest;
}

UEdGraph* FN2CBlueprintEditorTracker::GetFocusedGraph() const
{
	const TSharedPtr<FBlueprintEditor> Editor = GetFocusedEditor();
	return Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
}

void FN2CBlueprintEditorTracker::OnTabForegrounded(TSharedPtr<SDockTab> NewlyForegrounded, TSharedPtr<SDockTab> PreviouslyForegrounded)
{
	// FGlobalTabmanager broadcasts TabForegrounded(NewForegroundTab, BackgroundedTab): the new tab comes first
	HandleTabActivated(NewlyForegrounded);
}

void FN2CBlueprintEditorTracker::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActive)
{
	// FGlobalTabmanager broadcasts OnActiveTabChanged(CurrentlyActiveTab, NewActiveTab)
	HandleTabActivated(NewlyActive);
}

void FN2CBlueprintEditorTracker::OnAssetOpenedInEditor(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
	// A newly opened editor comes to the front
	NotifyEditorActivated(GetEditorFor(Cast<UBlueprint>(Asset)));
}
