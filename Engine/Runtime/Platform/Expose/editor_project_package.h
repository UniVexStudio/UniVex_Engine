#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "application_window_settings.h"

namespace UVE::Platform {

inline constexpr std::uint32_t kCurrentEditorProjectSchemaVersion = 1U;
inline constexpr std::size_t kMaximumEditorProjectIdBytes = 128U;
/// File extension of a project manifest.
inline constexpr std::string_view kEditorProjectFileExtension = ".uvproject";
inline constexpr std::size_t kMaximumEditorProjectNameBytes = 256U;
inline constexpr std::size_t kMaximumEditorProjectPathBytes = 256U;
inline constexpr std::size_t kMaximumEditorProductNameBytes = 256U;
inline constexpr std::size_t kMaximumEditorProductShortNameBytes = 64U;
inline constexpr std::size_t kMaximumEditorProductDescriptionBytes = 4096U;
inline constexpr std::size_t kMaximumEditorProductVersionBytes = 64U;
inline constexpr std::size_t kMaximumEditorPublisherNameBytes = 256U;
inline constexpr std::size_t kMaximumEditorCopyrightLineBytes = 512U;
inline constexpr std::size_t kMaximumEditorApplicationIdentifierBytes = 128U;
inline constexpr std::size_t kMaximumEditorSymbolUploadEndpointBytes = 2048U;
inline constexpr std::size_t kMaximumEditorIconSizesPerTarget = 16U;

struct EditorProjectVersion final {
    std::uint32_t major = 0U;
    std::uint32_t minor = 0U;
    std::uint32_t patch = 0U;
    std::uint32_t build = 0U;

    [[nodiscard]] bool operator==(const EditorProjectVersion&) const = default;
};

/// Shipped-product identity/version, distinct from the editor-facing project display name and
/// the engine build that last edited the project. Empty `name` and `shortName` keep older projects
/// compatible; consumers use the project display name when no product name is set.
struct EditorProjectProductMetadata final {
    std::string name;
    std::string shortName;
    std::string description;
    std::string version = "1.0.0";
    std::uint32_t buildNumber = 0U;

    [[nodiscard]] bool operator==(const EditorProjectProductMetadata&) const = default;
};

/// Runtime/application policy saved with a project. File paths in this block (icons, splash and
/// crash dumps) are project-relative or user-data-relative as their names document; no executable
/// code, credentials, or platform binaries are embedded in the manifest.
struct EditorProjectApplicationSettings final {
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
    WindowMode windowMode = WindowMode::Windowed;
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
    StretchMode stretchMode = StretchMode::Disabled;
    AspectPolicy aspectPolicy = AspectPolicy::Keep;
    bool integerOnlyScaling = false;
    DisplayOrientation orientation = DisplayOrientation::Auto;
    std::vector<DisplayOrientation> allowedOrientations{
        DisplayOrientation::Landscape, DisplayOrientation::Portrait};
    VSyncMode vsyncMode = VSyncMode::On;
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

    [[nodiscard]] bool operator==(const EditorProjectApplicationSettings&) const = default;
};

struct EditorProjectPackage final {
    std::uint32_t schemaVersion = kCurrentEditorProjectSchemaVersion;
    std::uint64_t revision = 1U;
    std::string projectId;
    std::string displayName;
    EditorProjectVersion engineVersion{};
    EditorProjectProductMetadata productMetadata{};
    EditorProjectApplicationSettings applicationSettings{};
    std::filesystem::path contentRoot;
    std::filesystem::path assetDatabasePath;
    std::filesystem::path settingsPath;

    /// Path, relative to `contentRoot`, of the scene a packaged/standalone run of this project
    /// should load and play first (roadmap item #7's own "manifest naming the startup scene").
    /// Empty means "not configured yet" - a project with no startup scene can still be authored
    /// and saved, it just cannot be packaged/launched standalone until one is set (see
    /// `ProjectPackager`/`LoadAndActivateProjectScene`). Absent from older `.uvproject` files
    /// written before this field existed; the codec defaults it to empty on load for those.
    std::filesystem::path startupScenePath;

    /// Project rendering profile chosen in UniVex Hub (`Compatibility`, `Mobile`, or `Forward+`).
    /// Absent in older `.uvproject` files; defaults to the host's recommended profile on load.
    RendererProfile rendererProfile = DefaultRendererProfileForHost();

    [[nodiscard]] bool operator==(const EditorProjectPackage&) const = default;
};

enum class EditorProjectPackageCode : std::uint8_t {
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

struct EditorProjectPackageResult final {
    EditorProjectPackageCode code = EditorProjectPackageCode::InvalidPackage;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == EditorProjectPackageCode::Applied;
    }
};

struct EditorProjectPackageLoadResult final {
    EditorProjectPackageResult result;
    std::optional<EditorProjectPackage> package;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return result.IsAccepted() && package.has_value();
    }
};

/// Stateless `.uvproject` project descriptor authority. The format is intentionally a portable
/// project/content manifest: it references existing relative content, asset-database, and settings
/// authorities but does not embed scene/assets, own credentials, or generate platform binaries.
/// Thread-safety: stateless and safe to call concurrently; each operation owns its local file data.
class EditorProjectPackageCodec final {
public:
    [[nodiscard]] static EditorProjectPackageResult Validate(
        const EditorProjectPackage& package) noexcept;

    [[nodiscard]] static EditorProjectPackageLoadResult Load(
        const std::filesystem::path& packagePath);

    [[nodiscard]] static EditorProjectPackageResult Save(
        const std::filesystem::path& packagePath,
        const EditorProjectPackage& package);

    /// Replaces an existing package only when the caller supplies the current revision, the project
    /// identity matches, the replacement validates, and its revision is strictly newer. Publication
    /// uses the same temporary-sibling rename as Save, so rejected updates never mutate the file.
    [[nodiscard]] static EditorProjectPackageResult ApplyUpdate(
        const std::filesystem::path& packagePath,
        std::uint64_t expectedRevision,
        const EditorProjectPackage& replacement);
};

} // namespace UVE::Platform
