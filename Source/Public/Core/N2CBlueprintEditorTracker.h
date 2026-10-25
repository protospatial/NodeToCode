// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"

class FBlueprintEditor;
class IAssetEditorInstance;
class SDockTab;
class UBlueprint;
class UEdGraph;

/**
 * The single definition of "the focused Blueprint" for NodeToCode (card 0150).
 *
 * The focused Blueprint editor is the Blueprint editor the user last activated: the one whose asset tab they brought
 * to the front, whose graph or panel they clicked into, or that was last opened (by the user, by open-blueprint-asset
 * or by the Python bridge). It follows the global tab manager's OnTabForegrounded and OnActiveTabChanged, the events a
 * click on an editor tab or inside an editor raises, and UAssetEditorSubsystem::OnAssetOpenedInEditor, which fires for
 * every editor opened (also when several open at once). Activating something that is not a Blueprint editor (the
 * Content Browser, the level editor, the N2C window) leaves the focus where it was. A tab in a Blueprint editor whose
 * asset tab is not in front (a background rebuild) does not take the focus.
 *
 * When the focused editor closes, the focus falls back to the most recently activated Blueprint editor still open, and
 * failing that to the open Blueprint editor with the latest IAssetEditorInstance::GetLastActivationTime().
 *
 * Only real Blueprint editors count: some UBlueprint subclasses (Control Rig with RigVM.UseNewEditor=1, third-party
 * types) open in toolkits that are not FBlueprintEditor, so every lookup checks IToolkit::IsBlueprintEditor().
 */
class FN2CBlueprintEditorTracker
{
public:
	static FN2CBlueprintEditorTracker& Get();

	/** Subscribe to the tab and asset editor events (editor start) */
	void Initialize();

	/** Unsubscribe (editor shutdown) */
	void Shutdown();

	/** The Blueprint editor open on Blueprint, or null when none is open or its editor is not a Blueprint editor. Never opens one. */
	static TSharedPtr<FBlueprintEditor> GetEditorFor(const UBlueprint* Blueprint);

	/** Every open Blueprint editor (Blueprint, Widget and Anim Blueprint editors alike), each once */
	static TArray<TSharedPtr<FBlueprintEditor>> GetOpenBlueprintEditors();

	/**
	 * The Blueprint editor a tab belongs to: the editor's own asset tab, or a tab inside it (a graph document, Details,
	 * My Blueprint). A graph tab is resolved through the graph it shows. Null for a tab outside every Blueprint editor.
	 */
	static TSharedPtr<FBlueprintEditor> FindEditorForTab(const TSharedPtr<SDockTab>& Tab);

	/** Records that the user (or a tool acting for them) activated Editor: it becomes the focused editor */
	void NotifyEditorActivated(const TSharedPtr<FBlueprintEditor>& Editor);

	/**
	 * Handles a tab being foregrounded or made active: when it belongs to a Blueprint editor whose asset tab is in
	 * front, that editor becomes the focused editor. Returns the editor that took the focus, or null when the focus
	 * did not change. The global tab manager events call this; tests call it to replay them.
	 */
	TSharedPtr<FBlueprintEditor> HandleTabActivated(const TSharedPtr<SDockTab>& Tab);

	/** The focused Blueprint editor (see the class comment), or null when no Blueprint editor is open */
	TSharedPtr<FBlueprintEditor> GetFocusedEditor() const;

	/** The graph the focused Blueprint editor shows, or null */
	UEdGraph* GetFocusedGraph() const;

private:
	FN2CBlueprintEditorTracker() = default;

	/** True when Editor is still open on its Blueprint (a closing editor can outlive its tab for a moment) */
	static bool IsOpen(const TSharedPtr<FBlueprintEditor>& Editor);

	void OnTabForegrounded(TSharedPtr<SDockTab> NewlyForegrounded, TSharedPtr<SDockTab> PreviouslyForegrounded);
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActive);
	void OnAssetOpenedInEditor(UObject* Asset, IAssetEditorInstance* EditorInstance);

	/** Activated Blueprint editors, most recent last; each editor once */
	mutable TArray<TWeakPtr<FBlueprintEditor>> ActivationHistory;

	FDelegateHandle OnTabForegroundedHandle;
	FDelegateHandle OnActiveTabChangedHandle;
	FDelegateHandle OnAssetOpenedInEditorHandle;
};
