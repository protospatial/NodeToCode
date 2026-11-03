// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CBlueprintEditorTracker.h"

#include "BlueprintEditor.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "IMaterialEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
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

	/** FMaterialEditor's toolkit name; FMaterialInstanceEditor (MaterialInstanceEditor) is also an IMaterialEditor but edits no graph */
	const FName MaterialEditorName(TEXT("MaterialEditor"));
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
	MaterialActivationHistory.Empty();
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

TSharedPtr<IMaterialEditor> FN2CBlueprintEditorTracker::GetMaterialEditorFor(const UObject* Asset)
{
	// A Material Function's editor also lists the transient material it previews the function on, and a Material
	// Editor's own working copy lives in the transient package: neither is the asset the editor was opened on
	if (!Asset || !(Asset->IsA<UMaterial>() || Asset->IsA<UMaterialFunction>()) || Asset->GetOutermost() == GetTransientPackage())
	{
		return nullptr;
	}
	const TSharedPtr<IToolkit> Toolkit = FToolkitManager::Get().FindEditorForAsset(Asset);
	if (!Toolkit.IsValid() || Toolkit->GetToolkitFName() != N2CBlueprintEditorTrackerPrivate::MaterialEditorName)
	{
		return nullptr;
	}
	// The toolkit name identifies FMaterialEditor, an IMaterialEditor (MaterialEditingLibrary casts the same way)
	return StaticCastSharedPtr<IMaterialEditor>(Toolkit);
}

TArray<UObject*> FN2CBlueprintEditorTracker::GetOpenMaterialEditorAssets()
{
	TArray<UObject*> Assets;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!AssetEditorSubsystem)
	{
		return Assets;
	}
	for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
	{
		if (GetMaterialEditorFor(Asset).IsValid())
		{
			Assets.AddUnique(Asset);
		}
	}
	return Assets;
}

UObject* FN2CBlueprintEditorTracker::FindMaterialAssetForTab(const TSharedPtr<SDockTab>& Tab)
{
	if (!Tab.IsValid())
	{
		return nullptr;
	}
	const TSharedPtr<FTabManager> TabsManager = Tab->GetTabManagerPtr();
	for (UObject* Asset : GetOpenMaterialEditorAssets())
	{
		const TSharedPtr<IMaterialEditor> Editor = GetMaterialEditorFor(Asset);
		const TSharedPtr<FTabManager> EditorTabManager = Editor.IsValid() ? Editor->GetTabManager() : nullptr;
		if (EditorTabManager.IsValid() && (EditorTabManager->GetOwnerTab() == Tab || (TabsManager.IsValid() && TabsManager == EditorTabManager)))
		{
			return Asset;
		}
	}
	return nullptr;
}

UObject* FN2CBlueprintEditorTracker::WorkingCopyOf(const IMaterialEditor& Editor)
{
	UMaterial* Preview = Cast<UMaterial>(Editor.GetMaterialInterface());
	if (!Preview)
	{
		return nullptr;
	}
	// A Material Function's editor previews it on a transient material; the function it edits is the graph's
	if (Preview->MaterialGraph && Preview->MaterialGraph->MaterialFunction)
	{
		return Preview->MaterialGraph->MaterialFunction;
	}
	return Preview;
}

void FN2CBlueprintEditorTracker::NotifyMaterialEditorActivated(UObject* Asset)
{
	if (!GetMaterialEditorFor(Asset).IsValid())
	{
		return;
	}
	const bool bChanged = MaterialActivationHistory.IsEmpty() || MaterialActivationHistory.Last().Get() != Asset;
	MaterialActivationHistory.RemoveAll([Asset](const TWeakObjectPtr<UObject>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Asset;
	});
	MaterialActivationHistory.Add(Asset);
	if (MaterialActivationHistory.Num() > N2CBlueprintEditorTrackerPrivate::MaxHistory)
	{
		MaterialActivationHistory.RemoveAt(0, MaterialActivationHistory.Num() - N2CBlueprintEditorTrackerPrivate::MaxHistory);
	}
	if (bChanged)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Focused Material Editor: %s"), *Asset->GetPathName()), EN2CLogSeverity::Info);
	}
}

UObject* FN2CBlueprintEditorTracker::GetFocusedMaterialAsset() const
{
	// The most recently activated Material Editor that is still open
	for (int32 Index = MaterialActivationHistory.Num() - 1; Index >= 0; --Index)
	{
		UObject* Asset = MaterialActivationHistory[Index].Get();
		if (GetMaterialEditorFor(Asset).IsValid())
		{
			return Asset;
		}
		MaterialActivationHistory.RemoveAt(Index);
	}

	// None recorded is open (all closed, or opened before the tracker started): the latest activated open one
	UObject* Latest = nullptr;
	double LatestTime = -1.0;
	for (UObject* Asset : GetOpenMaterialEditorAssets())
	{
		const double Time = GetMaterialEditorFor(Asset)->GetLastActivationTime();
		if (Time > LatestTime)
		{
			LatestTime = Time;
			Latest = Asset;
		}
	}
	return Latest;
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
		// A Material Editor's tab moves the Material Editor focus only (card 0463), under the same asset-tab rule
		if (UObject* MaterialAsset = FindMaterialAssetForTab(Tab))
		{
			const TSharedPtr<IMaterialEditor> MaterialEditor = GetMaterialEditorFor(MaterialAsset);
			const TSharedPtr<FTabManager> MaterialTabManager = MaterialEditor.IsValid() ? MaterialEditor->GetTabManager() : nullptr;
			const TSharedPtr<SDockTab> MaterialOwnerTab = MaterialTabManager.IsValid() ? MaterialTabManager->GetOwnerTab() : nullptr;
			if (!MaterialOwnerTab.IsValid() || MaterialOwnerTab == Tab || MaterialOwnerTab->IsForeground())
			{
				NotifyMaterialEditorActivated(MaterialAsset);
			}
		}
		return nullptr; // not a Blueprint editor's tab: the Blueprint focus stays where it was
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
	NotifyMaterialEditorActivated(Asset);
}
