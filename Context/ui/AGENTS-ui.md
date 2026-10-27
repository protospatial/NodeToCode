# UI and design tokens

NodeToCode's editor UI is Slate: one nomad tab (the main window), an overlay in every Blueprint graph editor, and the
widgets they are built from. Colours are settings; spacing, fonts and sizes are compile-time tokens. The code viewer
has its own page: [code editor](../code-editor/AGENTS-code-editor.md). Styling is also summarized under "Styling" in
[architecture](../architecture/AGENTS-architecture.md). User pages: `wiki/Theme-&-Syntax-Highlighting.md`,
`wiki/Code-Editor.md`.

## Where the UI starts

- `FN2CEditorIntegration` (`Source/Public/Core/N2CEditorIntegration.h`): `Initialize`/`Shutdown` start and stop the
  tab spawner, `FN2CBlueprintEditorTracker` (which Blueprint editor and graph has focus) and the overlay injector.
  It also answers `GetActiveBlueprintEditor`, `GetFocusedGraphFromActiveEditor` and `ResolveTranslationOverrides`.
- `SN2CEditorWindow` (`Source/Public/Core/N2CEditorWindow.h`): registers the nomad tab `NodeToCodeEditor`
  ("Node to Code", hidden from the Window menu) and spawns at most one, bringing it to the front when it exists.
- `FN2CGraphOverlayInjector` (`Source/Public/Core/N2CGraphOverlayInjector.h`): puts one `SN2CGraphOverlay` in each
  graph editor (`FindGraphEditorFor`, `FindGraphEditorOverlay`) on timers: 0.5 s after an editor opens, 0.1 s after a
  graph tab comes to the front, and a 2 s poll (`n2c.OverlayPoll`) with backoff; shutdown removes them. The
  overlay opens the main window with `TryInvokeTab(SN2CEditorWindow::TabId)`. There is no toolbar button.

## Widgets (`Source/Public/Core/Widgets/`)

| Widget | Role |
|---|---|
| `SN2CMainWindow` | The main window: the Tag Manager with the Translation Viewer and batch progress overlays |
| `SN2CGraphOverlay` | The overlay in the top right of a graph editor: translate, tag, open the window |
| `SN2CTranslationViewer` | A translation: generated files in `SN2CCodeEditor`, the N2C JSON, the header bar |
| `SN2CTagManager`, `SN2CTagCategoryTree`, `SN2CTaggedGraphsList`, `SN2CGraphListRow` | The Tag Manager ([tags and graph state](../tags-graph-state/AGENTS-tags-graph-state.md)) |
| `SN2CBatchProgressModal` | Progress of a batch translation |
| `SN2CContextWindowVisualizer` | The batch summary: model, tokens for the selection, estimated cost (per graph: `SN2CGraphListRow`) |
| `SN2CPanel`, `SN2CIconButton`, `SN2CHeaderBar`, `SN2CCircularProgressBar` | Building blocks: a styled border panel, button variants mapped to `N2CStyle` button styles, a header, a progress ring |

`UN2CMainWindowWidget` and `UN2CTagManagerWidget` are 1.x UMG wrappers kept for old projects; new UI is Slate.

## Colours and tokens (`Source/Public/Core/Style/`)

- `N2CDesignTokens.h`: `FN2CSpacing` (XXS to XXL, 2 to 20), `FN2CFonts` (Tiny to Large, bold variants, `Mono`,
  `MonoSmall`), `FN2CSizing` (icon sizes, separator thickness). Header only, `constexpr` values: use them instead of
  literals.
- `N2CUIColors.h`: `FN2CUIColors`, the configurable colours (backgrounds, buttons, borders, text, accents, editor), with
  `ToLinear`/`ToLinearWithAlpha`. `N2C::UI::Colors()` returns `GetDefault<UN2CSettings>()->UIColors`
  (`Source/Private/Core/Style/N2CUIColors.cpp`, the only style file that includes the settings header);
  `N2C::UI::Bind(&FN2CUIColors::Member)` gives a `TAttribute` read on every paint, so a widget bound with it follows a
  colour edit at once.
- `N2CStyle.h`: `N2CStyle`, the plugin's `FSlateStyleSet` (`Initialize`, `Shutdown`, `Reinitialize`, `Get`), with style
  getters (`GetButtonStyle`, `GetPanelBorderBrush`, `GetComboBoxStyle`, `GetTableRowStyle`, `GetScrollBarStyle`, ...)
  and `UpdateButtonStyles`, `UpdateBorderBrushes`, `UpdateComboBoxStyle`, `UpdateWidgetStyles`. Its content root is the
  plugin's `Resources` folder.
- The settings: `UIColors` and `CodeEditorTheme` in `UN2CSettings` (`Source/Public/Core/N2CSettings.h`, category
  "Node to Code | Theming"). `UN2CSettings::PostEditChangeProperty` calls `FN2CCodeEditorStyle::Reinitialize()` on a
  theme edit and the four `N2CStyle::Update*` functions on a colour edit.

## Specs

`NodeToCode.Style.UIColors`, `NodeToCode.UI.TagManager`, `NodeToCode.UI.TranslationViewer`,
`NodeToCode.UI.TranslationViewerJson`, `NodeToCode.UI.CodeEditor`, `NodeToCode.UI.CodeEditorThemeFallback`,
`NodeToCode.UI.SyntaxHighlighter` (all in `Source/Private/Tests/Core/`), `NodeToCode.Windowed.Integration`,
`NodeToCode.Editor.OverlayInjection`, `NodeToCode.Editor.Focus`, `NodeToCode.Editor.OverlayPerGraph`,
`NodeToCode.Robustness.LegacyWidgetWrappers`, `NodeToCode.Robustness.CodeEditor`. `NodeToCode.Windowed.Integration`
needs a rendering editor: under `-nullrhi` (`build.ps1 -Test`) it reports that it was skipped, so run it in a windowed
editor with `-ExecCmds="Automation RunTests NodeToCode.Windowed"`.
