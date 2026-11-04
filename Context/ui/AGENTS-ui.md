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
| `SN2CMainWindow` | The main window: the Get Started card above the Tag Manager, with the Translation Viewer and batch progress overlays |
| `SN2CGetStartedCard` | First run (card 0269): provider, API key (`UN2CSettings::SetApiKey`), connection test (`FN2CConnectionTest`, `Core/Services`), target language, the N2C bar picture (`NodeToCode.GetStarted.Overlay`, `Resources/get_started_overlay.png`, a copy of the docs shot `translate-entry/graph-overlay.png`). Shows while `UN2CUserSettings::bShowGetStartedCard` is on and `GetGraphsWithTranslation()` is empty. Spec `NodeToCode.UI.GetStarted` (also the tree and viewer empty states and the disabled selection buttons) |
| `SN2CGraphOverlay` | The N2C bar, injected at the bottom left of a graph editor (`HAlign_Left`, `VAlign_Bottom` in `FN2CGraphOverlayInjector::InjectOverlay`): translate (an Overlay session of `UN2CTranslationSubsystem`, which works with the window closed: a notification shows it, card 0254), tag, open the window. Its Translate greys out on the subsystem's `OnBusyChanged` |
| `SN2CTranslationViewer` | A translation: every graph of it (card 0257: `LoadResponse`, `LoadRunManifest` from the run's manifest.json, `LoadTranslation(FN2CTagInfo)` loads the whole run; entry list shown with more than one, root first, roles nested/struct/enum), generated files in `SN2CCodeEditor` (read-only until the Edit toggle; per entry and tab working copies that Copy copies), the N2C JSON, the verify status (card 0313), the header bar. Spec `NodeToCode.UI.TranslationViewer.Entries` |
| `SN2CTagManager`, `SN2CTagCategoryTree`, `SN2CTaggedGraphsList`, `SN2CGraphListRow` | The Tag Manager ([tags and graph state](../tags-graph-state/AGENTS-tags-graph-state.md)) |
| `SN2CBatchProgressModal` | Progress of a batch translation |
| `SN2CContextWindowVisualizer` | The batch summary: model, tokens for the selection, estimated cost (per graph: `SN2CGraphListRow`) |
| `SN2CPanel`, `SN2CIconButton`, `SN2CHeaderBar`, `SN2CCircularProgressBar` | Building blocks: a styled border panel, button variants mapped to `N2CStyle` button styles, a header, a progress ring |

`UN2CMainWindowWidget` and `UN2CTagManagerWidget` are 1.x UMG wrappers kept for old projects; new UI is Slate.

## Colours and tokens (`Source/Public/Core/Style/`)

- `N2CDesignTokens.h`: `FN2CSpacing` (XXS to XXL, 2 to 20), `FN2CFonts` (Small to Large, bold variants, `Mono`,
  `MonoSmall`; nothing under 8pt), `FN2CSizing` (icon sizes, separator thickness). Header only, `constexpr` values: use
  them instead of literals. Readability rules (card 0271, `NodeToCode.UI.DpiContrastIcons`): muted text keeps 4.5:1 on
  `BgPanel` and `BgInput`; icon-only buttons have a tooltip; status and close marks are `FAppStyle` icons, not Unicode
  glyphs; the logo brushes are SVG (`Resources/button_icon.svg`, 20 px and 16 px); custom painting (`SN2CCircularProgressBar`) draws in local space with `AllottedGeometry.ToPaintGeometry()`,
  passes the draw effects and multiplies in the widget tint; UI click logs are `Verbose`.
- `N2CUIColors.h`: `FN2CUIColors`, the configurable colours (backgrounds, buttons, borders, text, accents, editor), with
  `ToLinear`/`ToLinearWithAlpha`. `N2C::UI::Colors()` returns `GetDefault<UN2CUserSettings>()->UIColors`
  (`Source/Private/Core/Style/N2CUIColors.cpp`, the only style file that includes a settings header);
  `N2C::UI::Bind(&FN2CUIColors::Member)` gives a `TAttribute` read on every paint, so a widget bound with it follows a
  colour edit at once.
- `N2CStyle.h`: `N2CStyle`, the plugin's `FSlateStyleSet` (`Initialize`, `Shutdown`, `Reinitialize`, `Get`), with style
  getters (`GetButtonStyle`, `GetPanelBorderBrush`, `GetComboBoxStyle`, `GetTableRowStyle`, `GetScrollBarStyle`, ...)
  and `UpdateButtonStyles`, `UpdateBorderBrushes`, `UpdateComboBoxStyle`, `UpdateWidgetStyles`. Its content root is the
  plugin's `Resources` folder.
- The settings: `CodeEditorTheme` in `UN2CSettings` (`Source/Public/Core/N2CSettings.h`, category
  "Node to Code | Theming"), whose `PostEditChangeProperty` calls `FN2CCodeEditorStyle::Reinitialize()` on a theme
  edit; `UIColors` in `UN2CUserSettings` (per user since card 0188, Editor Preferences > Plugins > Node to Code (User) >
  Theming), whose `PostEditChangeProperty` calls the four `N2CStyle::Update*` functions on a committed colour edit.

## Specs

`NodeToCode.Style.UIColors`, `NodeToCode.UI.TagManager`, `NodeToCode.UI.TranslationViewer`,
`NodeToCode.UI.TranslationViewerJson`, `NodeToCode.UI.CodeEditor`, `NodeToCode.UI.CodeEditorThemeFallback`,
`NodeToCode.UI.SyntaxHighlighter` (all in `Source/Private/Tests/Core/`), `NodeToCode.Windowed.Integration`,
`NodeToCode.Editor.OverlayInjection`, `NodeToCode.Editor.Focus`, `NodeToCode.Editor.OverlayPerGraph`,
`NodeToCode.Robustness.LegacyWidgetWrappers`, `NodeToCode.Robustness.CodeEditor`. `NodeToCode.Windowed.Integration`
needs a rendering editor: under `-nullrhi` (`build.ps1 -Test`) it reports that it was skipped, so run it in a windowed
editor with `-ExecCmds="Automation RunTests NodeToCode.Windowed"`.
