// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "settings_registry.h"

namespace UVE::Editor {

/// The ids of the editor's own settings in the settings file, each declared once by
/// RegisterEditorSettings. The strings are the keys the file has always used, so existing
/// settings files keep working.
namespace EditorSettingId {
// Session state the editor remembers for itself: Hidden, never offered as a preference.
inline constexpr std::string_view kScenePanelVisible = "editor.panels.sceneVisible";
inline constexpr std::string_view kInspectorPanelVisible = "editor.panels.inspectorVisible";
inline constexpr std::string_view kBottomDockVisible = "editor.panels.bottomDockVisible";
inline constexpr std::string_view kBottomDockHeight = "editor.panels.bottomDockHeight";
/// The widths the author dragged the two side panels to. Persisted for the same reason the dock's
/// height is: a layout the author tuned is part of how they work, and a width that survives only
/// until the next launch is a width they have to keep re-setting. A value of 0 means "never
/// dragged", and the layout falls back to its default ratio.
inline constexpr std::string_view kScenePanelWidth = "editor.panels.sceneWidth";
inline constexpr std::string_view kInspectorPanelWidth = "editor.panels.inspectorWidth";
inline constexpr std::string_view kAssetTableViewMode = "editor.assetTable.viewMode";
inline constexpr std::string_view kAssetTableMode = "editor.assetTable.mode";
inline constexpr std::string_view kActiveWorkspace = "editor.workspace.active";
inline constexpr std::string_view kActiveRightPanelTab = "editor.rightPanel.activeTab";
inline constexpr std::string_view kActiveBottomDock = "editor.bottomDock.active";
inline constexpr std::string_view kColorPickerAdvancedOpen = "editor.colorPicker.advancedOpen";
inline constexpr std::string_view kColorPickerSaved = "editor.colorPicker.saved";
inline constexpr std::string_view kColorPickerRecent = "editor.colorPicker.recent";
inline constexpr std::string_view kInspectorFolds = "editor.inspector.folds";
inline constexpr std::string_view kFavoriteProjects = "editor.favorites";
inline constexpr std::string_view kViewportAxisColorX = "editor.viewport.axisColors.x";
inline constexpr std::string_view kViewportAxisColorY = "editor.viewport.axisColors.y";
inline constexpr std::string_view kViewportAxisColorZ = "editor.viewport.axisColors.z";
// Preferences.
inline constexpr std::string_view kSnapEnabled = "editor.viewport.snap.enabled";
inline constexpr std::string_view kSnapTranslateStep = "editor.viewport.snap.translateStep";
inline constexpr std::string_view kSnapRotateStepDegrees = "editor.viewport.snap.rotateStepDegrees";
inline constexpr std::string_view kSnapScaleStep = "editor.viewport.snap.scaleStep";
inline constexpr std::string_view kSnapScaleEnabled = "editor.viewport.snap.scaleEnabled";
inline constexpr std::string_view kGridVisible = "editor.viewport.grid.visible";
inline constexpr std::string_view kGridOpacity = "editor.viewport.grid.opacity";
inline constexpr std::string_view kGridCellSize = "editor.viewport.grid.cellSize";
inline constexpr std::string_view kSelectionOutlineVisible = "editor.viewport.selectionOutline.visible";
/// A colour, so its channels sit at `.r`, `.g` and `.b` beneath this id.
inline constexpr std::string_view kSelectionOutlineColor = "editor.viewport.selectionOutline";
inline constexpr std::string_view kSelectionOutlineThickness = "editor.viewport.selectionOutline.thickness";
inline constexpr std::string_view kNewObjectsUnderSelection = "editor.objects.addUnderSelection";
inline constexpr std::string_view kNewObjectPlacement = "editor.objects.placement";
inline constexpr std::string_view kPlayPauseOnStart = "editor.play.pauseOnStart";
inline constexpr std::string_view kPlaySaveSceneFirst = "editor.play.saveSceneFirst";
inline constexpr std::string_view kPlaySwitchToGame = "editor.play.switchToGame";
inline constexpr std::string_view kPlayTintEnabled = "editor.play.tint.enabled";
inline constexpr std::string_view kPlayTintColor = "editor.play.tint.color";
inline constexpr std::string_view kPlayTintStrength = "editor.play.tint.strength";
inline constexpr std::string_view kHierarchyRevealSelection = "editor.hierarchy.revealSelection";
inline constexpr std::string_view kHierarchyShowIcons = "editor.hierarchy.showIcons";
inline constexpr std::string_view kHierarchyShowTypeName = "editor.hierarchy.showTypeName";
inline constexpr std::string_view kHierarchyVisibilityColumn = "editor.hierarchy.visibilityColumn";
inline constexpr std::string_view kHierarchyDoubleClick = "editor.hierarchy.doubleClick";
inline constexpr std::string_view kHierarchyDragToReparent = "editor.hierarchy.dragToReparent";
inline constexpr std::string_view kHierarchyTreeLines = "editor.hierarchy.treeLines";
inline constexpr std::string_view kHierarchyIndentWidth = "editor.hierarchy.indentWidth";
// Part 2.1 Interface & Theme
inline constexpr std::string_view kThemePreset = "editor.interface.themePreset";
inline constexpr std::string_view kCustomThemeFile = "editor.interface.customThemeFile";
inline constexpr std::string_view kThemeBaseColor = "editor.interface.baseColor";
inline constexpr std::string_view kThemeAccentColor = "editor.interface.accentColor";
inline constexpr std::string_view kThemeContrast = "editor.interface.contrast";
inline constexpr std::string_view kUiScaleMode = "editor.interface.uiScaleMode";
inline constexpr std::string_view kUiScaleFactor = "editor.interface.uiScaleFactor";
inline constexpr std::string_view kFontSizePx = "editor.interface.fontSizePx";
inline constexpr std::string_view kFontHintingMode = "editor.interface.fontHinting";
inline constexpr std::string_view kFontOversampling = "editor.interface.fontOversampling";
inline constexpr std::string_view kIconSaturation = "editor.interface.iconSaturation";
inline constexpr std::string_view kCornerRadiusPx = "editor.interface.cornerRadiusPx";
inline constexpr std::string_view kSpacingDensity = "editor.interface.spacingDensity";
inline constexpr std::string_view kUiAnimationSpeed = "editor.interface.animationSpeed";
inline constexpr std::string_view kReduceMotion = "editor.interface.reduceMotion";
inline constexpr std::string_view kTooltipDelaySeconds = "editor.interface.tooltipDelaySeconds";
inline constexpr std::string_view kEditorLanguage = "editor.interface.language";
inline constexpr std::string_view kTouchFriendlyTargets = "editor.interface.touchFriendlyTargets";
// Part 2.2 Layout, Docking & Workspaces
inline constexpr std::string_view kPersistDockLayout = "editor.layout.persistDockLayout";
inline constexpr std::string_view kLeftSplitRatio = "editor.layout.leftSplitRatio";
inline constexpr std::string_view kRightSplitRatio = "editor.layout.rightSplitRatio";
inline constexpr std::string_view kAllowFloatingWindows = "editor.layout.allowFloatingWindows";
inline constexpr std::string_view kTabPosition = "editor.layout.tabPosition";
inline constexpr std::string_view kDistractionFreeMode = "editor.layout.distractionFreeMode";
// Part 2.3 Hierarchy Extended
inline constexpr std::string_view kHierarchyExpandDepthLimit = "editor.hierarchy.expandDepthLimit";
inline constexpr std::string_view kHierarchyPersistExpansion = "editor.hierarchy.persistExpansion";
inline constexpr std::string_view kHierarchyRowHeightPx = "editor.hierarchy.rowHeightPx";
inline constexpr std::string_view kHierarchyColorCodeIcons = "editor.hierarchy.colorCodeIcons";
inline constexpr std::string_view kHierarchyShowLockColumn = "editor.hierarchy.showLockColumn";
inline constexpr std::string_view kHierarchyFilterMode = "editor.hierarchy.filterMode";
inline constexpr std::string_view kHierarchySortMode = "editor.hierarchy.sortMode";
inline constexpr std::string_view kHierarchyTintByTagColor = "editor.hierarchy.tintByTagColor";
inline constexpr std::string_view kHierarchyRenameMode = "editor.hierarchy.renameMode";
// Part 2.4 Context Menu
inline constexpr std::string_view kContextDuplicateSuffix = "editor.contextMenu.duplicateSuffix";
inline constexpr std::string_view kContextReparentKeepGlobal = "editor.contextMenu.reparentKeepGlobal";
inline constexpr std::string_view kContextSelectOnOpen = "editor.contextMenu.selectOnOpen";
inline constexpr std::string_view kContextTouchHoldSeconds = "editor.contextMenu.touchHoldSeconds";
inline constexpr std::string_view kContextConfirmDeleteSubtree = "editor.contextMenu.confirmDeleteSubtree";
// Part 2.5 Commands & Menu Bar
inline constexpr std::string_view kCommandPaletteFuzzyMode = "editor.commands.paletteFuzzyMode";
inline constexpr std::string_view kCommandPaletteRecentDepth = "editor.commands.paletteRecentDepth";
inline constexpr std::string_view kShortcutProfile = "editor.commands.shortcutProfile";
inline constexpr std::string_view kRecentFilesLimit = "editor.commands.recentFilesLimit";
inline constexpr std::string_view kToolbarButtonSizePx = "editor.commands.toolbarButtonSizePx";
inline constexpr std::string_view kAutoSavePreferences = "editor.commands.autoSavePreferences";
// Part 2.6 Viewport Extended
inline constexpr std::string_view kGridSubdivisions = "editor.viewport.grid.subdivisions";
inline constexpr std::string_view kGridFadeDistance = "editor.viewport.grid.fadeDistance";
inline constexpr std::string_view kGridFollowsCamera = "editor.viewport.grid.followsCamera";
inline constexpr std::string_view kGizmoSizePx = "editor.viewport.gizmo.sizePx";
inline constexpr std::string_view kGizmoOpacity = "editor.viewport.gizmo.opacity";
inline constexpr std::string_view kRememberGizmoMode = "editor.viewport.gizmo.rememberMode";
inline constexpr std::string_view kDefaultTransformSpace = "editor.viewport.defaultTransformSpace";
inline constexpr std::string_view kPivotMode = "editor.viewport.pivotMode";
inline constexpr std::string_view kNavigationScheme = "editor.viewport.navigationScheme";
inline constexpr std::string_view kOrbitSensitivity = "editor.viewport.orbitSensitivity";
inline constexpr std::string_view kPanSensitivity = "editor.viewport.panSensitivity";
inline constexpr std::string_view kZoomSensitivity = "editor.viewport.zoomSensitivity";
inline constexpr std::string_view kCameraInertiaSmoothing = "editor.viewport.cameraInertia";
inline constexpr std::string_view kFreelookBaseSpeed = "editor.viewport.freelookBaseSpeed";
inline constexpr std::string_view kDefaultCameraFov = "editor.viewport.defaultCameraFovDegrees";
inline constexpr std::string_view kBoxSelectMode = "editor.viewport.boxSelectMode";
inline constexpr std::string_view kDefaultViewMode = "editor.viewport.defaultViewMode";
inline constexpr std::string_view kViewportRenderScale = "editor.viewport.renderScale";
inline constexpr std::string_view kViewportPaneCount = "editor.viewport.paneCount";
// Part 2.7 Inspector Extended
inline constexpr std::string_view kInspectorLabelRatio = "editor.inspector.labelRatio";
inline constexpr std::string_view kInspectorFloatPrecision = "editor.inspector.floatPrecision";
inline constexpr std::string_view kInspectorAngleUnit = "editor.inspector.angleUnit";
inline constexpr std::string_view kInspectorShowAdvanced = "editor.inspector.showAdvanced";
inline constexpr std::string_view kInspectorColorPickerShape = "editor.inspector.colorPickerShape";
inline constexpr std::string_view kInspectorColorPickerMode = "editor.inspector.colorPickerMode";
inline constexpr std::string_view kInspectorOpenResourcesMode = "editor.inspector.openResourcesMode";
inline constexpr std::string_view kInspectorPlayRefreshHz = "editor.inspector.playRefreshHz";
// Part 2.8 Script & Shader Editor
inline constexpr std::string_view kScriptFontSizePx = "editor.scriptEditor.fontSizePx";
inline constexpr std::string_view kScriptLineNumbers = "editor.scriptEditor.showLineNumbers";
inline constexpr std::string_view kScriptTabSize = "editor.scriptEditor.tabSize";
inline constexpr std::string_view kScriptWordWrapMode = "editor.scriptEditor.wordWrapMode";
inline constexpr std::string_view kScriptSyntaxTheme = "editor.scriptEditor.syntaxTheme";
inline constexpr std::string_view kScriptCodeCompletion = "editor.scriptEditor.codeCompletion";
inline constexpr std::string_view kScriptMinimapEnabled = "editor.scriptEditor.minimapEnabled";
inline constexpr std::string_view kScriptFormatOnSave = "editor.scriptEditor.formatOnSave";
inline constexpr std::string_view kScriptExternalEditorPath = "editor.scriptEditor.externalExecutable";
inline constexpr std::string_view kNodeGraphWireStyle = "editor.scriptEditor.nodeGraphWireStyle";
// Part 2.9 Asset Browser
inline constexpr std::string_view kAssetThumbnailSizePx = "editor.assetBrowser.thumbnailSizePx";
inline constexpr std::string_view kAssetSortMode = "editor.assetBrowser.sortMode";
inline constexpr std::string_view kAssetShowHiddenFiles = "editor.assetBrowser.showHiddenFiles";
inline constexpr std::string_view kAssetSplitRatio = "editor.assetBrowser.splitRatio";
inline constexpr std::string_view kAssetAutoReimport = "editor.assetBrowser.autoReimport";
inline constexpr std::string_view kAssetDeleteToTrash = "editor.assetBrowser.deleteToTrash";
// Part 2.10 Console, Log & Debug
inline constexpr std::string_view kConsoleCollapseDuplicates = "editor.console.collapseDuplicates";
inline constexpr std::string_view kConsoleMaxLines = "editor.console.maxLines";
inline constexpr std::string_view kConsoleClearOnPlay = "editor.console.clearOnPlay";
inline constexpr std::string_view kConsoleShowStackTraces = "editor.console.showStackTraces";
inline constexpr std::string_view kConsoleHistoryDepth = "editor.console.historyDepth";
inline constexpr std::string_view kConsoleRemotePort = "editor.console.remotePort";
inline constexpr std::string_view kDebugPhysicsDraw = "editor.debug.physicsDraw";
inline constexpr std::string_view kDebugNavigationDraw = "editor.debug.navigationDraw";
inline constexpr std::string_view kDebugRendererBounds = "editor.debug.rendererBounds";
inline constexpr std::string_view kDebugAudioRadii = "editor.debug.audioRadii";
inline constexpr std::string_view kDebugAssertionBehavior = "editor.debug.assertionBehavior";
// Part 2.11 Play Mode Extended
inline constexpr std::string_view kPlayWindowMode = "editor.play.windowMode";
inline constexpr std::string_view kPlayWindowSize = "editor.play.windowSize";
inline constexpr std::string_view kPlaySceneSource = "editor.play.sceneSource";
inline constexpr std::string_view kPlayPauseOnError = "editor.play.pauseOnError";
inline constexpr std::string_view kPlayLiveReload = "editor.play.liveReload";
inline constexpr std::string_view kPlayTimeScale = "editor.play.timeScale";
// Part 2.12 Autosave, Recovery & Performance
inline constexpr std::string_view kAutosaveEnabled = "editor.autosave.enabled";
inline constexpr std::string_view kAutosaveIntervalSeconds = "editor.autosave.intervalSeconds";
inline constexpr std::string_view kCrashRecoveryEnabled = "editor.autosave.crashRecovery";
inline constexpr std::string_view kBackupFileCount = "editor.autosave.backupCount";
inline constexpr std::string_view kUndoHistoryDepth = "editor.autosave.undoHistoryDepth";
inline constexpr std::string_view kLowProcessorMode = "editor.autosave.lowProcessorMode";
inline constexpr std::string_view kBackgroundWorkerThreads = "editor.autosave.backgroundWorkerThreads";
inline constexpr std::string_view kImportCacheLimitMb = "editor.autosave.importCacheLimitMb";
// Part 2.13 Version Control & External Tools
inline constexpr std::string_view kVcsEnabled = "editor.vcs.enabled";
inline constexpr std::string_view kVcsProvider = "editor.vcs.provider";
inline constexpr std::string_view kVcsShowStatusBadges = "editor.vcs.showStatusBadges";
inline constexpr std::string_view kVcsDiffToolPath = "editor.vcs.diffToolPath";
inline constexpr std::string_view kExternalImageEditorPath = "editor.externalTools.imageEditorPath";
inline constexpr std::string_view kExternal3dToolPath = "editor.externalTools.model3dToolPath";
} // namespace EditorSettingId

/// Cohesive editor user preferences covering Part 2 (2.1 Interface/Theme through 2.13 Version Control).
struct EditorExtendedPreferences final {
    std::int64_t themePreset = 0; // 0=Dark, 1=Light, 2=HighContrast, 3=Custom
    std::string customThemeFile;
    Config::SettingColor themeBaseColor{0.14F, 0.15F, 0.18F, 1.0F};
    Config::SettingColor themeAccentColor{0.22F, 0.56F, 0.96F, 1.0F};
    double themeContrast = 1.0;
    std::int64_t uiScaleMode = 0; // 0=AutoFromDpi, 1=Manual
    double uiScaleFactor = 1.0;
    std::int64_t fontSizePx = 14;
    std::int64_t fontHintingMode = 2; // 0=None, 1=Slight, 2=Normal, 3=Subpixel
    std::int64_t fontOversampling = 2;
    double iconSaturation = 1.0;
    double cornerRadiusPx = 4.0;
    std::int64_t spacingDensity = 1; // 0=Compact, 1=Comfortable
    double uiAnimationSpeed = 1.0;
    bool reduceMotion = false;
    double tooltipDelaySeconds = 0.35;
    std::string editorLanguage = "en";
    bool touchFriendlyTargets = false;

    bool persistDockLayout = true;
    double leftSplitRatio = 0.2;
    double rightSplitRatio = 0.25;
    bool allowFloatingWindows = true;
    std::int64_t tabPosition = 0; // 0=Top, 1=Bottom
    bool distractionFreeMode = false;

    std::int64_t hierarchyExpandDepthLimit = 16;
    bool hierarchyPersistExpansion = true;
    double hierarchyRowHeightPx = 22.0;
    bool hierarchyColorCodeIcons = true;
    bool hierarchyShowLockColumn = true;
    std::int64_t hierarchyFilterMode = 2; // 0=Name, 1=Name+Type, 2=Name+Type+Component
    std::int64_t hierarchySortMode = 0; // 0=SceneOrder, 1=Alphabetical, 2=ByType
    bool hierarchyTintByTagColor = true;
    std::int64_t hierarchyRenameMode = 0; // 0=Inline, 1=Dialog

    std::string contextDuplicateSuffix = "_{n}";
    bool contextReparentKeepGlobal = true;
    bool contextSelectOnOpen = true;
    double contextTouchHoldSeconds = 0.5;
    bool contextConfirmDeleteSubtree = true;

    std::int64_t commandPaletteFuzzyMode = 0;
    std::int64_t commandPaletteRecentDepth = 8;
    std::string shortcutProfile = "Default";
    std::int64_t recentFilesLimit = 15;
    std::int64_t toolbarButtonSizePx = 24;
    bool autoSavePreferences = true;

    std::int64_t gridSubdivisions = 10;
    double gridFadeDistance = 200.0;
    bool gridFollowsCamera = true;
    double gizmoSizePx = 96.0;
    double gizmoOpacity = 0.95;
    bool rememberGizmoMode = true;
    std::int64_t defaultTransformSpace = 0; // 0=Local, 1=World
    std::int64_t pivotMode = 0; // 0=Median, 1=ActiveObject, 2=IndividualOrigins
    std::int64_t navigationScheme = 0; // 0=UniVex, 1=MayaAlt, 2=BlenderMiddle, 3=RightClickFly
    double orbitSensitivity = 1.0;
    double panSensitivity = 1.0;
    double zoomSensitivity = 1.0;
    double cameraInertiaSmoothing = 0.15;
    double freelookBaseSpeed = 8.0;
    double defaultCameraFovDegrees = 60.0;
    std::int64_t boxSelectMode = 0; // 0=Intersect, 1=Enclose
    std::int64_t defaultViewMode = 0; // 0=Lit, 1=Unshaded, 2=Wireframe, 3=Overdraw, 4=Normals
    double viewportRenderScale = 1.0;
    std::int64_t viewportPaneCount = 1;

    double inspectorLabelRatio = 0.38;
    std::int64_t inspectorFloatPrecision = 3;
    std::int64_t inspectorAngleUnit = 0; // 0=Degrees, 1=Radians
    bool inspectorShowAdvanced = false;
    std::int64_t inspectorColorPickerShape = 0; // 0=HueWheelDisc, 1=HueBarRect, 2=ValueHueCircle, 3=PerceptualCircle, 4=SlidersOnly
    std::int64_t inspectorColorPickerMode = 0; // 0=RGB_0_1, 1=RGB_0_255, 2=HSV, 3=OKLab
    std::int64_t inspectorOpenResourcesMode = 0; // 0=InlineSubInspector, 1=NewPanel
    double inspectorPlayRefreshHz = 15.0;

    std::int64_t scriptFontSizePx = 14;
    bool scriptShowLineNumbers = true;
    std::int64_t scriptTabSize = 4;
    std::int64_t scriptWordWrapMode = 0; // 0=Off, 1=ViewportWidth, 2=WrapColumn
    std::string scriptSyntaxTheme = "UniVex Dark";
    bool scriptCodeCompletion = true;
    bool scriptMinimapEnabled = true;
    bool scriptFormatOnSave = false;
    std::string scriptExternalExecutable;
    std::int64_t nodeGraphWireStyle = 0; // 0=Curved, 1=Straight, 2=Orthogonal

    std::int64_t assetThumbnailSizePx = 96;
    std::int64_t assetSortMode = 0; // 0=Name, 1=Type, 2=ModifiedTime, 3=Size
    bool assetShowHiddenFiles = false;
    double assetSplitRatio = 0.25;
    bool assetAutoReimport = true;
    bool assetDeleteToTrash = true;

    bool consoleCollapseDuplicates = true;
    std::int64_t consoleMaxLines = 5000;
    bool consoleClearOnPlay = false;
    bool consoleShowStackTraces = true;
    std::int64_t consoleHistoryDepth = 64;
    std::int64_t consoleRemotePort = 9229;
    bool debugPhysicsDraw = false;
    bool debugNavigationDraw = false;
    bool debugRendererBounds = false;
    bool debugAudioRadii = false;
    std::int64_t debugAssertionBehavior = 0; // 0=BreakIntoDebugger, 1=LogAndContinue, 2=Abort

    std::int64_t playWindowMode = 0; // 0=EmbeddedViewport, 1=MaximisedViewport, 2=SeparateWindow
    Config::SettingVector2 playWindowSize{1280.0, 720.0};
    std::int64_t playSceneSource = 0; // 0=CurrentScene, 1=ProjectMainScene, 2=CustomScene
    bool playPauseOnError = true;
    bool playLiveReload = true;
    double playTimeScale = 1.0;

    bool autosaveEnabled = true;
    double autosaveIntervalSeconds = 300.0;
    bool crashRecoveryEnabled = true;
    std::int64_t backupFileCount = 5;
    std::int64_t undoHistoryDepth = 128;
    bool lowProcessorMode = false;
    std::int64_t backgroundWorkerThreads = 4;
    std::int64_t importCacheLimitMb = 2048;

    bool vcsEnabled = true;
    std::int64_t vcsProvider = 1; // 0=None, 1=Git, 2=Perforce, 3=PlasticSCM
    bool vcsShowStatusBadges = true;
    std::string vcsDiffToolPath;
    std::string externalImageEditorPath;
    std::string external3dToolPath;
};

/// One setting id that was renamed, and the id it now has. A settings file written before the
/// rename still carries the old key; the registry's Deprecated alias migrates its value to the new
/// id on load, so the author keeps their choice.
struct RenamedSettingId final {
    std::string_view oldId;
    std::string_view newId;
};

/// The renames so far. `editor.nodes.*` became `editor.objects.*` when the engine's word for a
/// scene node became object.
inline constexpr RenamedSettingId kRenamedSettingIds[] = {
    {"editor.nodes.addUnderSelection", EditorSettingId::kNewObjectsUnderSelection},
    {"editor.nodes.placement", EditorSettingId::kNewObjectPlacement},
};

/// Declares every editor setting in `registry`, with defaults and legal ranges taken from the
/// editor's own defaults and setters, so a stored value the registry accepts is one the editor
/// accepts too. False if any declaration is refused - a programming error the editor's settings
/// test catches.
///
/// The colour picker's saved/recent colours, remembered inspector folds and favorite projects use
/// hidden StringList descriptors; the viewport axis colours use hidden Color descriptors but are
/// loaded and saved as one palette because their defaults belong to the host. Lists have no
/// editable generic panel row yet.
[[nodiscard]] bool RegisterEditorSettings(Config::SettingsRegistry& registry);

// What the preferences window is made of, kept free of UI so it can be tested.

/// Whether `descriptor` matches the search `query`: every space-separated word in it appears,
/// ignoring case, in the setting's name, category, id or tooltip. An empty query matches all.
[[nodiscard]] bool MatchesSettingSearch(const Config::SettingDescriptor& descriptor, std::string_view query);

/// Whether a setting in `category` sits at or beneath the category tree object `path`.
[[nodiscard]] bool IsInSettingCategory(std::string_view category, std::string_view path) noexcept;

/// One object of the category tree: "Editor/Viewport/Grid" is `name` "Grid" at `depth` 2.
struct SettingCategoryObject final {
    std::string path;
    std::string name;
    int depth = 0;
};

/// The category tree of `descriptors`, every ancestor included, flattened depth first: a parent
/// always comes right before its children, and siblings keep the order they first appear in.
[[nodiscard]] std::vector<SettingCategoryObject> BuildSettingCategoryTree(
    const std::vector<const Config::SettingDescriptor*>& descriptors);

/// `value` as a person reads it: On or Off, a number without trailing zeros, an enum entry's
/// label, a colour's hex code.
[[nodiscard]] std::string FormatSettingValue(const Config::SettingDescriptor& descriptor,
                                                const Config::SettingValue& value);

} // namespace UVE::Editor
