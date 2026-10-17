// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "uve/config/settings_registry_uve.h"

namespace UVE::Editor {

/// The ids of the editor's own settings in the settings file, each declared once by
/// RegisterEditorSettingsUVE. The strings are the keys the file has always used, so existing
/// settings files keep working.
namespace EditorSettingIdUVE {
// Session state the editor remembers for itself: Hidden, never offered as a preference.
inline constexpr std::string_view kScenePanelVisibleUVE = "editor.panels.sceneVisible";
inline constexpr std::string_view kViewportPanelVisibleUVE = "editor.panels.viewportVisible";
inline constexpr std::string_view kInspectorPanelVisibleUVE = "editor.panels.inspectorVisible";
inline constexpr std::string_view kBottomDockVisibleUVE = "editor.panels.bottomDockVisible";
inline constexpr std::string_view kBottomDockHeightUVE = "editor.panels.bottomDockHeight";
inline constexpr std::string_view kContentBrowserViewModeUVE = "editor.contentBrowser.viewMode";
inline constexpr std::string_view kContentBrowserModeUVE = "editor.contentBrowser.mode";
inline constexpr std::string_view kActiveWorkspaceUVE = "editor.workspace.active";
inline constexpr std::string_view kActiveRightPanelTabUVE = "editor.rightPanel.activeTab";
inline constexpr std::string_view kActiveBottomDockUVE = "editor.bottomDock.active";
inline constexpr std::string_view kColorPickerAdvancedOpenUVE = "editor.colorPicker.advancedOpen";
inline constexpr std::string_view kColorPickerSavedUVE = "editor.colorPicker.saved";
inline constexpr std::string_view kColorPickerRecentUVE = "editor.colorPicker.recent";
inline constexpr std::string_view kInspectorFoldsUVE = "editor.inspector.folds";
inline constexpr std::string_view kFavoriteProjectsUVE = "editor.favorites";
inline constexpr std::string_view kViewportAxisColorXUVE = "editor.viewport.axisColors.x";
inline constexpr std::string_view kViewportAxisColorYUVE = "editor.viewport.axisColors.y";
inline constexpr std::string_view kViewportAxisColorZUVE = "editor.viewport.axisColors.z";
// Preferences.
inline constexpr std::string_view kSnapEnabledUVE = "editor.viewport.snap.enabled";
inline constexpr std::string_view kSnapTranslateStepUVE = "editor.viewport.snap.translateStep";
inline constexpr std::string_view kSnapRotateStepDegreesUVE = "editor.viewport.snap.rotateStepDegrees";
inline constexpr std::string_view kSnapScaleStepUVE = "editor.viewport.snap.scaleStep";
inline constexpr std::string_view kGridVisibleUVE = "editor.viewport.grid.visible";
inline constexpr std::string_view kGridOpacityUVE = "editor.viewport.grid.opacity";
inline constexpr std::string_view kGridCellSizeUVE = "editor.viewport.grid.cellSize";
inline constexpr std::string_view kSelectionOutlineVisibleUVE = "editor.viewport.selectionOutline.visible";
/// A colour, so its channels sit at `.r`, `.g` and `.b` beneath this id.
inline constexpr std::string_view kSelectionOutlineColorUVE = "editor.viewport.selectionOutline";
inline constexpr std::string_view kSelectionOutlineThicknessUVE = "editor.viewport.selectionOutline.thickness";
inline constexpr std::string_view kNewObjectsUnderSelectionUVE = "editor.objects.addUnderSelection";
inline constexpr std::string_view kNewObjectPlacementUVE = "editor.objects.placement";
inline constexpr std::string_view kPlayPauseOnStartUVE = "editor.play.pauseOnStart";
inline constexpr std::string_view kPlaySaveSceneFirstUVE = "editor.play.saveSceneFirst";
inline constexpr std::string_view kPlaySwitchToGameUVE = "editor.play.switchToGame";
inline constexpr std::string_view kPlayTintEnabledUVE = "editor.play.tint.enabled";
inline constexpr std::string_view kPlayTintColorUVE = "editor.play.tint.color";
inline constexpr std::string_view kPlayTintStrengthUVE = "editor.play.tint.strength";
inline constexpr std::string_view kHierarchyRevealSelectionUVE = "editor.hierarchy.revealSelection";
inline constexpr std::string_view kHierarchyShowIconsUVE = "editor.hierarchy.showIcons";
inline constexpr std::string_view kHierarchyShowTypeNameUVE = "editor.hierarchy.showTypeName";
inline constexpr std::string_view kHierarchyVisibilityColumnUVE = "editor.hierarchy.visibilityColumn";
inline constexpr std::string_view kHierarchyDoubleClickUVE = "editor.hierarchy.doubleClick";
inline constexpr std::string_view kHierarchyDragToReparentUVE = "editor.hierarchy.dragToReparent";
inline constexpr std::string_view kHierarchyTreeLinesUVE = "editor.hierarchy.treeLines";
inline constexpr std::string_view kHierarchyIndentWidthUVE = "editor.hierarchy.indentWidth";
// Part 2.1 Interface & Theme
inline constexpr std::string_view kThemePresetUVE = "editor.interface.themePreset";
inline constexpr std::string_view kCustomThemeFileUVE = "editor.interface.customThemeFile";
inline constexpr std::string_view kThemeBaseColorUVE = "editor.interface.baseColor";
inline constexpr std::string_view kThemeAccentColorUVE = "editor.interface.accentColor";
inline constexpr std::string_view kThemeContrastUVE = "editor.interface.contrast";
inline constexpr std::string_view kUiScaleModeUVE = "editor.interface.uiScaleMode";
inline constexpr std::string_view kUiScaleFactorUVE = "editor.interface.uiScaleFactor";
inline constexpr std::string_view kFontSizePxUVE = "editor.interface.fontSizePx";
inline constexpr std::string_view kFontHintingModeUVE = "editor.interface.fontHinting";
inline constexpr std::string_view kFontOversamplingUVE = "editor.interface.fontOversampling";
inline constexpr std::string_view kIconSaturationUVE = "editor.interface.iconSaturation";
inline constexpr std::string_view kCornerRadiusPxUVE = "editor.interface.cornerRadiusPx";
inline constexpr std::string_view kSpacingDensityUVE = "editor.interface.spacingDensity";
inline constexpr std::string_view kUiAnimationSpeedUVE = "editor.interface.animationSpeed";
inline constexpr std::string_view kReduceMotionUVE = "editor.interface.reduceMotion";
inline constexpr std::string_view kTooltipDelaySecondsUVE = "editor.interface.tooltipDelaySeconds";
inline constexpr std::string_view kEditorLanguageUVE = "editor.interface.language";
inline constexpr std::string_view kTouchFriendlyTargetsUVE = "editor.interface.touchFriendlyTargets";
// Part 2.2 Layout, Docking & Workspaces
inline constexpr std::string_view kPersistDockLayoutUVE = "editor.layout.persistDockLayout";
inline constexpr std::string_view kLeftSplitRatioUVE = "editor.layout.leftSplitRatio";
inline constexpr std::string_view kRightSplitRatioUVE = "editor.layout.rightSplitRatio";
inline constexpr std::string_view kAllowFloatingWindowsUVE = "editor.layout.allowFloatingWindows";
inline constexpr std::string_view kTabPositionUVE = "editor.layout.tabPosition";
inline constexpr std::string_view kDistractionFreeModeUVE = "editor.layout.distractionFreeMode";
// Part 2.3 Hierarchy Extended
inline constexpr std::string_view kHierarchyExpandDepthLimitUVE = "editor.hierarchy.expandDepthLimit";
inline constexpr std::string_view kHierarchyPersistExpansionUVE = "editor.hierarchy.persistExpansion";
inline constexpr std::string_view kHierarchyRowHeightPxUVE = "editor.hierarchy.rowHeightPx";
inline constexpr std::string_view kHierarchyColorCodeIconsUVE = "editor.hierarchy.colorCodeIcons";
inline constexpr std::string_view kHierarchyShowLockColumnUVE = "editor.hierarchy.showLockColumn";
inline constexpr std::string_view kHierarchyFilterModeUVE = "editor.hierarchy.filterMode";
inline constexpr std::string_view kHierarchySortModeUVE = "editor.hierarchy.sortMode";
inline constexpr std::string_view kHierarchyTintByTagColorUVE = "editor.hierarchy.tintByTagColor";
inline constexpr std::string_view kHierarchyRenameModeUVE = "editor.hierarchy.renameMode";
// Part 2.4 Context Menu
inline constexpr std::string_view kContextDuplicateSuffixUVE = "editor.contextMenu.duplicateSuffix";
inline constexpr std::string_view kContextReparentKeepGlobalUVE = "editor.contextMenu.reparentKeepGlobal";
inline constexpr std::string_view kContextSelectOnOpenUVE = "editor.contextMenu.selectOnOpen";
inline constexpr std::string_view kContextTouchHoldSecondsUVE = "editor.contextMenu.touchHoldSeconds";
inline constexpr std::string_view kContextConfirmDeleteSubtreeUVE = "editor.contextMenu.confirmDeleteSubtree";
// Part 2.5 Commands & Menu Bar
inline constexpr std::string_view kCommandPaletteFuzzyModeUVE = "editor.commands.paletteFuzzyMode";
inline constexpr std::string_view kCommandPaletteRecentDepthUVE = "editor.commands.paletteRecentDepth";
inline constexpr std::string_view kShortcutProfileUVE = "editor.commands.shortcutProfile";
inline constexpr std::string_view kRecentFilesLimitUVE = "editor.commands.recentFilesLimit";
inline constexpr std::string_view kToolbarButtonSizePxUVE = "editor.commands.toolbarButtonSizePx";
inline constexpr std::string_view kAutoSavePreferencesUVE = "editor.commands.autoSavePreferences";
// Part 2.6 Viewport Extended
inline constexpr std::string_view kGridSubdivisionsUVE = "editor.viewport.grid.subdivisions";
inline constexpr std::string_view kGridFadeDistanceUVE = "editor.viewport.grid.fadeDistance";
inline constexpr std::string_view kGridFollowsCameraUVE = "editor.viewport.grid.followsCamera";
inline constexpr std::string_view kGizmoSizePxUVE = "editor.viewport.gizmo.sizePx";
inline constexpr std::string_view kGizmoOpacityUVE = "editor.viewport.gizmo.opacity";
inline constexpr std::string_view kRememberGizmoModeUVE = "editor.viewport.gizmo.rememberMode";
inline constexpr std::string_view kDefaultTransformSpaceUVE = "editor.viewport.defaultTransformSpace";
inline constexpr std::string_view kPivotModeUVE = "editor.viewport.pivotMode";
inline constexpr std::string_view kNavigationSchemeUVE = "editor.viewport.navigationScheme";
inline constexpr std::string_view kOrbitSensitivityUVE = "editor.viewport.orbitSensitivity";
inline constexpr std::string_view kPanSensitivityUVE = "editor.viewport.panSensitivity";
inline constexpr std::string_view kZoomSensitivityUVE = "editor.viewport.zoomSensitivity";
inline constexpr std::string_view kCameraInertiaSmoothingUVE = "editor.viewport.cameraInertia";
inline constexpr std::string_view kFreelookBaseSpeedUVE = "editor.viewport.freelookBaseSpeed";
inline constexpr std::string_view kDefaultCameraFovUVE = "editor.viewport.defaultCameraFovDegrees";
inline constexpr std::string_view kBoxSelectModeUVE = "editor.viewport.boxSelectMode";
inline constexpr std::string_view kDefaultViewModeUVE = "editor.viewport.defaultViewMode";
inline constexpr std::string_view kViewportRenderScaleUVE = "editor.viewport.renderScale";
inline constexpr std::string_view kViewportPaneCountUVE = "editor.viewport.paneCount";
// Part 2.7 Inspector Extended
inline constexpr std::string_view kInspectorLabelRatioUVE = "editor.inspector.labelRatio";
inline constexpr std::string_view kInspectorFloatPrecisionUVE = "editor.inspector.floatPrecision";
inline constexpr std::string_view kInspectorAngleUnitUVE = "editor.inspector.angleUnit";
inline constexpr std::string_view kInspectorShowAdvancedUVE = "editor.inspector.showAdvanced";
inline constexpr std::string_view kInspectorColorPickerShapeUVE = "editor.inspector.colorPickerShape";
inline constexpr std::string_view kInspectorColorPickerModeUVE = "editor.inspector.colorPickerMode";
inline constexpr std::string_view kInspectorOpenResourcesModeUVE = "editor.inspector.openResourcesMode";
inline constexpr std::string_view kInspectorPlayRefreshHzUVE = "editor.inspector.playRefreshHz";
// Part 2.8 Script & Shader Editor
inline constexpr std::string_view kScriptFontSizePxUVE = "editor.scriptEditor.fontSizePx";
inline constexpr std::string_view kScriptLineNumbersUVE = "editor.scriptEditor.showLineNumbers";
inline constexpr std::string_view kScriptTabSizeUVE = "editor.scriptEditor.tabSize";
inline constexpr std::string_view kScriptWordWrapModeUVE = "editor.scriptEditor.wordWrapMode";
inline constexpr std::string_view kScriptSyntaxThemeUVE = "editor.scriptEditor.syntaxTheme";
inline constexpr std::string_view kScriptCodeCompletionUVE = "editor.scriptEditor.codeCompletion";
inline constexpr std::string_view kScriptMinimapEnabledUVE = "editor.scriptEditor.minimapEnabled";
inline constexpr std::string_view kScriptFormatOnSaveUVE = "editor.scriptEditor.formatOnSave";
inline constexpr std::string_view kScriptExternalEditorPathUVE = "editor.scriptEditor.externalExecutable";
inline constexpr std::string_view kNodeGraphWireStyleUVE = "editor.scriptEditor.nodeGraphWireStyle";
// Part 2.9 Asset Browser
inline constexpr std::string_view kAssetThumbnailSizePxUVE = "editor.assetBrowser.thumbnailSizePx";
inline constexpr std::string_view kAssetSortModeUVE = "editor.assetBrowser.sortMode";
inline constexpr std::string_view kAssetShowHiddenFilesUVE = "editor.assetBrowser.showHiddenFiles";
inline constexpr std::string_view kAssetSplitRatioUVE = "editor.assetBrowser.splitRatio";
inline constexpr std::string_view kAssetAutoReimportUVE = "editor.assetBrowser.autoReimport";
inline constexpr std::string_view kAssetDeleteToTrashUVE = "editor.assetBrowser.deleteToTrash";
// Part 2.10 Console, Log & Debug
inline constexpr std::string_view kConsoleCollapseDuplicatesUVE = "editor.console.collapseDuplicates";
inline constexpr std::string_view kConsoleMaxLinesUVE = "editor.console.maxLines";
inline constexpr std::string_view kConsoleClearOnPlayUVE = "editor.console.clearOnPlay";
inline constexpr std::string_view kConsoleShowStackTracesUVE = "editor.console.showStackTraces";
inline constexpr std::string_view kConsoleHistoryDepthUVE = "editor.console.historyDepth";
inline constexpr std::string_view kConsoleRemotePortUVE = "editor.console.remotePort";
inline constexpr std::string_view kDebugPhysicsDrawUVE = "editor.debug.physicsDraw";
inline constexpr std::string_view kDebugNavigationDrawUVE = "editor.debug.navigationDraw";
inline constexpr std::string_view kDebugRendererBoundsUVE = "editor.debug.rendererBounds";
inline constexpr std::string_view kDebugAudioRadiiUVE = "editor.debug.audioRadii";
inline constexpr std::string_view kDebugAssertionBehaviorUVE = "editor.debug.assertionBehavior";
// Part 2.11 Play Mode Extended
inline constexpr std::string_view kPlayWindowModeUVE = "editor.play.windowMode";
inline constexpr std::string_view kPlayWindowSizeUVE = "editor.play.windowSize";
inline constexpr std::string_view kPlaySceneSourceUVE = "editor.play.sceneSource";
inline constexpr std::string_view kPlayPauseOnErrorUVE = "editor.play.pauseOnError";
inline constexpr std::string_view kPlayLiveReloadUVE = "editor.play.liveReload";
inline constexpr std::string_view kPlayTimeScaleUVE = "editor.play.timeScale";
// Part 2.12 Autosave, Recovery & Performance
inline constexpr std::string_view kAutosaveEnabledUVE = "editor.autosave.enabled";
inline constexpr std::string_view kAutosaveIntervalSecondsUVE = "editor.autosave.intervalSeconds";
inline constexpr std::string_view kCrashRecoveryEnabledUVE = "editor.autosave.crashRecovery";
inline constexpr std::string_view kBackupFileCountUVE = "editor.autosave.backupCount";
inline constexpr std::string_view kUndoHistoryDepthUVE = "editor.autosave.undoHistoryDepth";
inline constexpr std::string_view kLowProcessorModeUVE = "editor.autosave.lowProcessorMode";
inline constexpr std::string_view kBackgroundWorkerThreadsUVE = "editor.autosave.backgroundWorkerThreads";
inline constexpr std::string_view kImportCacheLimitMbUVE = "editor.autosave.importCacheLimitMb";
// Part 2.13 Version Control & External Tools
inline constexpr std::string_view kVcsEnabledUVE = "editor.vcs.enabled";
inline constexpr std::string_view kVcsProviderUVE = "editor.vcs.provider";
inline constexpr std::string_view kVcsShowStatusBadgesUVE = "editor.vcs.showStatusBadges";
inline constexpr std::string_view kVcsDiffToolPathUVE = "editor.vcs.diffToolPath";
inline constexpr std::string_view kExternalImageEditorPathUVE = "editor.externalTools.imageEditorPath";
inline constexpr std::string_view kExternal3dToolPathUVE = "editor.externalTools.model3dToolPath";
} // namespace EditorSettingIdUVE

/// Cohesive editor user preferences covering Part 2 (2.1 Interface/Theme through 2.13 Version Control).
struct EditorExtendedPreferencesUVE final {
    std::int64_t themePreset = 0; // 0=Dark, 1=Light, 2=HighContrast, 3=Custom
    std::string customThemeFile;
    Config::SettingColorUVE themeBaseColor{0.14F, 0.15F, 0.18F, 1.0F};
    Config::SettingColorUVE themeAccentColor{0.22F, 0.56F, 0.96F, 1.0F};
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
    Config::SettingVector2UVE playWindowSize{1280.0, 720.0};
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
struct RenamedSettingIdUVE final {
    std::string_view oldId;
    std::string_view newId;
};

/// The renames so far. `editor.nodes.*` became `editor.objects.*` when the engine's word for a
/// scene node became object.
inline constexpr RenamedSettingIdUVE kRenamedSettingIdsUVE[] = {
    {"editor.nodes.addUnderSelection", EditorSettingIdUVE::kNewObjectsUnderSelectionUVE},
    {"editor.nodes.placement", EditorSettingIdUVE::kNewObjectPlacementUVE},
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
[[nodiscard]] bool RegisterEditorSettingsUVE(Config::SettingsRegistryUVE& registry);

// What the preferences window is made of, kept free of UI so it can be tested.

/// Whether `descriptor` matches the search `query`: every space-separated word in it appears,
/// ignoring case, in the setting's name, category, id or tooltip. An empty query matches all.
[[nodiscard]] bool MatchesSettingSearchUVE(const Config::SettingDescriptorUVE& descriptor, std::string_view query);

/// Whether a setting in `category` sits at or beneath the category tree object `path`.
[[nodiscard]] bool IsInSettingCategoryUVE(std::string_view category, std::string_view path) noexcept;

/// One object of the category tree: "Editor/Viewport/Grid" is `name` "Grid" at `depth` 2.
struct SettingCategoryObjectUVE final {
    std::string path;
    std::string name;
    int depth = 0;
};

/// The category tree of `descriptors`, every ancestor included, flattened depth first: a parent
/// always comes right before its children, and siblings keep the order they first appear in.
[[nodiscard]] std::vector<SettingCategoryObjectUVE> BuildSettingCategoryTreeUVE(
    const std::vector<const Config::SettingDescriptorUVE*>& descriptors);

/// `value` as a person reads it: On or Off, a number without trailing zeros, an enum entry's
/// label, a colour's hex code.
[[nodiscard]] std::string FormatSettingValueUVE(const Config::SettingDescriptorUVE& descriptor,
                                                const Config::SettingValueUVE& value);

} // namespace UVE::Editor
