#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Platform {

inline constexpr std::uint32_t kCurrentEditorProjectSchemaVersionUVE = 1U;
inline constexpr std::size_t kMaximumEditorProjectIdBytesUVE = 128U;
inline constexpr std::size_t kMaximumEditorProjectNameBytesUVE = 256U;
inline constexpr std::size_t kMaximumEditorProjectPathBytesUVE = 256U;
inline constexpr std::size_t kMaximumEditorProductNameBytesUVE = 256U;
inline constexpr std::size_t kMaximumEditorProductShortNameBytesUVE = 64U;
inline constexpr std::size_t kMaximumEditorProductDescriptionBytesUVE = 4096U;
inline constexpr std::size_t kMaximumEditorProductVersionBytesUVE = 64U;
inline constexpr std::size_t kMaximumEditorPublisherNameBytesUVE = 256U;
inline constexpr std::size_t kMaximumEditorCopyrightLineBytesUVE = 512U;
inline constexpr std::size_t kMaximumEditorApplicationIdentifierBytesUVE = 128U;
inline constexpr std::size_t kMaximumEditorSymbolUploadEndpointBytesUVE = 2048U;
inline constexpr std::size_t kMaximumEditorIconSizesPerTargetUVE = 16U;

struct EditorProjectVersionUVE final {
    std::uint32_t major = 0U;
    std::uint32_t minor = 0U;
    std::uint32_t patch = 0U;
    std::uint32_t build = 0U;

    [[nodiscard]] bool operator==(const EditorProjectVersionUVE&) const = default;
};

/// Shipped-product identity/version, distinct from the editor-facing project display name and
/// the engine build that last edited the project. Empty `name` and `shortName` keep older projects
/// compatible; consumers use the project display name when no product name is set.
struct EditorProjectProductMetadataUVE final {
    std::string name;
    std::string shortName;
    std::string description;
    std::string version = "1.0.0";
    std::uint32_t buildNumber = 0U;

    [[nodiscard]] bool operator==(const EditorProjectProductMetadataUVE&) const = default;
};

/// Runtime/application policy saved with a project. File paths in this block (icons, splash and
/// crash dumps) are project-relative or user-data-relative as their names document; no executable
/// code, credentials, or platform binaries are embedded in the manifest.
struct EditorProjectApplicationSettingsUVE final {
    std::string publisherName;
    std::string copyrightLine;

    /// Target names are `windows`, `linux`, `macos`, `ios`, `android` and `web`.
    std::map<std::string, std::string, std::less<>> applicationIdentifiersByTarget;
    /// Per-target, per-pixel-size icon paths. Paths are relative to the project's content root.
    std::map<std::string, std::map<std::uint32_t, std::filesystem::path>, std::less<>> iconPathsByTarget;

    /// Optional image relative to the content root. Desktop standalone runs import supported source
    /// images to the application cache and draw them over the configured boot background.
    std::filesystem::path splashImagePath;
    std::array<float, 4U> splashBackgroundColor{0.0F, 0.0F, 0.0F, 1.0F};
    double splashFadeSeconds = 0.25;
    double splashMinimumDisplaySeconds = 1.0;
    bool splashSkippable = true;
    bool skipSplashInEditorPlayMode = true;

    bool quitOnLastWindowClosed = true;
    bool enforceSingleInstance = false;

    /// Initial display and window policy. A zero maximum dimension means unbounded; a zero
    /// content-scale override delegates to the operating system. The initial window size also
    /// supplies the reference size for viewport stretching.
    std::uint32_t windowWidth = 1280U;
    std::uint32_t windowHeight = 720U;
    WindowModeUVE windowMode = WindowModeUVE::Windowed;
    bool windowResizable = true;
    bool windowBorderless = false;
    bool windowAlwaysOnTop = false;
    bool windowTransparent = false;
    std::uint32_t minimumWindowWidth = 0U;
    std::uint32_t minimumWindowHeight = 0U;
    std::uint32_t maximumWindowWidth = 0U;
    std::uint32_t maximumWindowHeight = 0U;
    bool initialWindowPositionSpecified = false;
    std::int32_t initialWindowPositionX = 0;
    std::int32_t initialWindowPositionY = 0;
    std::string initialMonitorName;
    bool highDpiAware = true;
    bool perMonitorScaling = true;
    double contentScaleOverride = 0.0;
    StretchModeUVE stretchMode = StretchModeUVE::Disabled;
    AspectPolicyUVE aspectPolicy = AspectPolicyUVE::Keep;
    bool integerOnlyScaling = false;
    DisplayOrientationUVE orientation = DisplayOrientationUVE::Auto;
    std::vector<DisplayOrientationUVE> allowedOrientations{
        DisplayOrientationUVE::Landscape, DisplayOrientationUVE::Portrait};
    VSyncModeUVE vsyncMode = VSyncModeUVE::On;
    std::uint32_t focusedFrameRateCap = 0U;
    std::uint32_t unfocusedFrameRateCap = 0U;
    bool allowDisplaySleep = true;
    std::filesystem::path cursorImagePath;
    std::uint32_t cursorHotspotX = 0U;
    std::uint32_t cursorHotspotY = 0U;
    bool cursorVisible = true;
    bool cursorConfinedToWindow = false;
    std::string windowTitleFormat = "{productName}";
    bool appendSceneNameInEditorPlayMode = false;

    /// Empty chooses the product short name, then the project ID. Portable data is placed beside
    /// the project; otherwise it is placed under the current user's standard data directory.
    std::string userDataDirectoryName;
    bool portableUserData = false;

    bool crashHandlerEnabled = true;
    std::filesystem::path crashDumpDirectory = "crash-dumps";
    /// Optional HTTPS endpoint reserved for a host-provided symbol/crash service.
    std::string symbolUploadEndpoint;

    [[nodiscard]] bool operator==(const EditorProjectApplicationSettingsUVE&) const = default;
};

struct EditorProjectPackageUVE final {
    std::uint32_t schemaVersion = kCurrentEditorProjectSchemaVersionUVE;
    std::uint64_t revision = 1U;
    std::string projectId;
    std::string displayName;
    EditorProjectVersionUVE engineVersion{};
    EditorProjectProductMetadataUVE productMetadata{};
    EditorProjectApplicationSettingsUVE applicationSettings{};
    std::filesystem::path contentRoot;
    std::filesystem::path assetDatabasePath;
    std::filesystem::path settingsPath;

    /// Path, relative to `contentRoot`, of the scene a packaged/standalone run of this project
    /// should load and play first (roadmap item #7's own "manifest naming the startup scene").
    /// Empty means "not configured yet" - a project with no startup scene can still be authored
    /// and saved, it just cannot be packaged/launched standalone until one is set (see
    /// `ProjectPackagerUVE`/`LoadAndActivateProjectSceneUVE`). Absent from older `.uvproject` files
    /// written before this field existed; the codec defaults it to empty on load for those.
    std::filesystem::path startupScenePath;

    [[nodiscard]] bool operator==(const EditorProjectPackageUVE&) const = default;
};

enum class EditorProjectPackageCodeUVE : std::uint8_t {
    Applied = 0,
    InvalidPath,
    InvalidPackage,
    UnsupportedSchema,
    ReadFailed,
    ParseFailed,
    WriteFailed,
    RevisionConflict,
    ProjectIdentityConflict,
};

struct EditorProjectPackageResultUVE final {
    EditorProjectPackageCodeUVE code = EditorProjectPackageCodeUVE::InvalidPackage;
    std::string message;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept {
        return code == EditorProjectPackageCodeUVE::Applied;
    }
};

struct EditorProjectPackageLoadResultUVE final {
    EditorProjectPackageResultUVE result;
    std::optional<EditorProjectPackageUVE> package;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept {
        return result.IsAcceptedUVE() && package.has_value();
    }
};

/// Stateless `.uvproject` project descriptor authority. The format is intentionally a portable
/// project/content manifest: it references existing relative content, asset-database, and settings
/// authorities but does not embed scene/assets, own credentials, or generate platform binaries.
/// Thread-safety: stateless and safe to call concurrently; each operation owns its local file data.
class EditorProjectPackageCodecUVE final {
public:
    [[nodiscard]] static EditorProjectPackageResultUVE ValidateUVE(
        const EditorProjectPackageUVE& package) noexcept;

    [[nodiscard]] static EditorProjectPackageLoadResultUVE LoadUVE(
        const std::filesystem::path& packagePath);

    [[nodiscard]] static EditorProjectPackageResultUVE SaveUVE(
        const std::filesystem::path& packagePath,
        const EditorProjectPackageUVE& package);

    /// Replaces an existing package only when the caller supplies the current revision, the project
    /// identity matches, the replacement validates, and its revision is strictly newer. Publication
    /// uses the same temporary-sibling rename as SaveUVE, so rejected updates never mutate the file.
    [[nodiscard]] static EditorProjectPackageResultUVE ApplyUpdateUVE(
        const std::filesystem::path& packagePath,
        std::uint64_t expectedRevision,
        const EditorProjectPackageUVE& replacement);
};

} // namespace UVE::Platform
