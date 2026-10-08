#include "uve/platform/editor_project_package_uve.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <exception>
#include <string_view>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Platform {
namespace {

using JsonUVE = nlohmann::json;

[[nodiscard]] EditorProjectPackageResultUVE MakeResultUVE(
    const EditorProjectPackageCodeUVE code, std::string message) {
    return {code, std::move(message)};
}

[[nodiscard]] bool IsBoundedTextUVE(const std::string& value, const std::size_t maximumBytes,
                                    const bool requireNonEmpty) noexcept {
    return (!requireNonEmpty || !value.empty()) && value.size() <= maximumBytes &&
           value.find('\0') == std::string::npos;
}

[[nodiscard]] bool IsProjectIdUVE(const std::string& value) noexcept {
    if (!IsBoundedTextUVE(value, kMaximumEditorProjectIdBytesUVE, true)) {
        return false;
    }
    for (const char rawCharacter : value) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        if (std::isalnum(character) == 0 && character != '-' && character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsRelativePathUVE(const std::filesystem::path& path) noexcept {
    const std::string value = path.generic_string();
    if (value.empty() || value.size() > kMaximumEditorProjectPathBytesUVE ||
        value.contains('\0') || value.contains('\\') ||
        path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "." || component == ".." || component.empty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsSupportedApplicationTargetUVE(const std::string_view target) noexcept {
    constexpr std::array<std::string_view, 6U> kTargets{"windows", "linux", "macos", "ios", "android", "web"};
    return std::find(kTargets.begin(), kTargets.end(), target) != kTargets.end();
}

[[nodiscard]] bool IsReverseDomainIdentifierUVE(const std::string& value) noexcept {
    if (!IsBoundedTextUVE(value, kMaximumEditorApplicationIdentifierBytesUVE, true)) {
        return false;
    }
    std::size_t labelCount = 0U;
    std::size_t labelStart = 0U;
    while (labelStart < value.size()) {
        const std::size_t separator = value.find('.', labelStart);
        const std::size_t labelEnd = separator == std::string::npos ? value.size() : separator;
        if (labelEnd == labelStart) {
            return false;
        }
        const auto isAlphaNumeric = [](const unsigned char character) { return std::isalnum(character) != 0; };
        if (!isAlphaNumeric(static_cast<unsigned char>(value[labelStart])) ||
            !isAlphaNumeric(static_cast<unsigned char>(value[labelEnd - 1U]))) {
            return false;
        }
        for (std::size_t index = labelStart + 1U; index + 1U < labelEnd; ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            if (!isAlphaNumeric(character) && character != '-') {
                return false;
            }
        }
        ++labelCount;
        if (separator == std::string::npos) {
            break;
        }
        if (separator + 1U == value.size()) {
            return false;
        }
        labelStart = separator + 1U;
    }
    return labelCount >= 2U;
}

[[nodiscard]] bool IsUserDataDirectoryNameUVE(const std::string& value) noexcept {
    if (!IsBoundedTextUVE(value, 64U, false)) {
        return false;
    }
    for (const char rawCharacter : value) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        if (std::isalnum(character) == 0 && character != '-' && character != '_') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsHttpsEndpointUVE(const std::string& value) noexcept {
    if (!IsBoundedTextUVE(value, kMaximumEditorSymbolUploadEndpointBytesUVE, false)) {
        return false;
    }
    if (value.empty()) {
        return true;
    }
    constexpr std::string_view kHttpsPrefix = "https://";
    if (!value.starts_with(kHttpsPrefix)) {
        return false;
    }
    const std::string_view authority{value.data() + kHttpsPrefix.size(), value.size() - kHttpsPrefix.size()};
    const std::size_t authorityEnd = authority.find_first_of("/?#");
    return !authority.empty() && authority.front() != '/' &&
           (authorityEnd == std::string_view::npos || authorityEnd != 0U);
}

[[nodiscard]] std::string LowercaseExtensionUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

[[nodiscard]] bool IsSupportedIconExtensionUVE(const std::filesystem::path& path) {
    const std::string extension = LowercaseExtensionUVE(path);
    return extension == ".png" || extension == ".ico" || extension == ".icns";
}

[[nodiscard]] bool IsPackagePathUVE(const std::filesystem::path& path) noexcept {
    return path.extension() == ".uvproject" && !path.filename().empty();
}

[[nodiscard]] std::optional<EditorProjectPackageUVE> DecodePackageUVE(const JsonUVE& json) {
    if (!json.is_object() || json.value("format", "") != "uvproject") {
        return std::nullopt;
    }

    EditorProjectPackageUVE package;
    package.schemaVersion = json.at("schemaVersion").get<std::uint32_t>();
    package.revision = json.at("revision").get<std::uint64_t>();
    package.projectId = json.at("projectId").get<std::string>();
    package.displayName = json.at("displayName").get<std::string>();
    const JsonUVE& engineVersion = json.at("engineVersion");
    package.engineVersion.major = engineVersion.at("major").get<std::uint32_t>();
    package.engineVersion.minor = engineVersion.at("minor").get<std::uint32_t>();
    package.engineVersion.patch = engineVersion.at("patch").get<std::uint32_t>();
    package.engineVersion.build = engineVersion.at("build").get<std::uint32_t>();
    package.contentRoot = json.at("contentRoot").get<std::string>();
    package.assetDatabasePath = json.at("assetDatabasePath").get<std::string>();
    package.settingsPath = json.at("settingsPath").get<std::string>();
    // Product metadata was added after the original project package schema. Keep the default
    // product version and empty optional name/description fields for older files.
    if (json.contains("productMetadata")) {
        const JsonUVE& product = json.at("productMetadata");
        if (!product.is_object()) {
            return std::nullopt;
        }
        package.productMetadata.name = product.value("name", package.productMetadata.name);
        package.productMetadata.shortName = product.value("shortName", package.productMetadata.shortName);
        package.productMetadata.description = product.value("description", package.productMetadata.description);
        package.productMetadata.version = product.value("version", package.productMetadata.version);
        package.productMetadata.buildNumber = product.value("buildNumber", package.productMetadata.buildNumber);
    }
    if (json.contains("applicationSettings")) {
        const JsonUVE& application = json.at("applicationSettings");
        if (!application.is_object()) {
            return std::nullopt;
        }
        package.applicationSettings.publisherName = application.value("publisherName", package.applicationSettings.publisherName);
        package.applicationSettings.copyrightLine = application.value("copyrightLine", package.applicationSettings.copyrightLine);
        package.applicationSettings.splashImagePath =
            application.value("splashImagePath", package.applicationSettings.splashImagePath.generic_string());
        package.applicationSettings.splashFadeSeconds =
            application.value("splashFadeSeconds", package.applicationSettings.splashFadeSeconds);
        package.applicationSettings.splashMinimumDisplaySeconds =
            application.value("splashMinimumDisplaySeconds", package.applicationSettings.splashMinimumDisplaySeconds);
        package.applicationSettings.splashSkippable =
            application.value("splashSkippable", package.applicationSettings.splashSkippable);
        package.applicationSettings.skipSplashInEditorPlayMode =
            application.value("skipSplashInEditorPlayMode", package.applicationSettings.skipSplashInEditorPlayMode);
        package.applicationSettings.quitOnLastWindowClosed =
            application.value("quitOnLastWindowClosed", package.applicationSettings.quitOnLastWindowClosed);
        package.applicationSettings.enforceSingleInstance =
            application.value("enforceSingleInstance", package.applicationSettings.enforceSingleInstance);
        const auto readEnum = [&application](const char* key, const std::string_view defaultName,
                                             const auto parse) {
            return parse(application.value(key, std::string{defaultName}));
        };
        const auto windowMode = readEnum("windowMode", GetWindowModeNameUVE(package.applicationSettings.windowMode),
                                         ParseWindowModeUVE);
        const auto stretchMode = readEnum("stretchMode", GetStretchModeNameUVE(package.applicationSettings.stretchMode),
                                          ParseStretchModeUVE);
        const auto aspectPolicy = readEnum("aspectPolicy", GetAspectPolicyNameUVE(package.applicationSettings.aspectPolicy),
                                           ParseAspectPolicyUVE);
        const auto orientation = readEnum("orientation", GetDisplayOrientationNameUVE(package.applicationSettings.orientation),
                                          ParseDisplayOrientationUVE);
        const auto vsyncMode = readEnum("vsyncMode", GetVSyncModeNameUVE(package.applicationSettings.vsyncMode),
                                        ParseVSyncModeUVE);
        if (!windowMode || !stretchMode || !aspectPolicy || !orientation || !vsyncMode) {
            return std::nullopt;
        }
        package.applicationSettings.windowMode = *windowMode;
        package.applicationSettings.stretchMode = *stretchMode;
        package.applicationSettings.aspectPolicy = *aspectPolicy;
        package.applicationSettings.orientation = *orientation;
        package.applicationSettings.vsyncMode = *vsyncMode;
        package.applicationSettings.windowWidth = application.value("windowWidth", package.applicationSettings.windowWidth);
        package.applicationSettings.windowHeight = application.value("windowHeight", package.applicationSettings.windowHeight);
        package.applicationSettings.windowResizable = application.value("windowResizable", package.applicationSettings.windowResizable);
        package.applicationSettings.windowBorderless = application.value("windowBorderless", package.applicationSettings.windowBorderless);
        package.applicationSettings.windowAlwaysOnTop = application.value("windowAlwaysOnTop", package.applicationSettings.windowAlwaysOnTop);
        package.applicationSettings.windowTransparent = application.value("windowTransparent", package.applicationSettings.windowTransparent);
        package.applicationSettings.minimumWindowWidth = application.value("minimumWindowWidth", package.applicationSettings.minimumWindowWidth);
        package.applicationSettings.minimumWindowHeight = application.value("minimumWindowHeight", package.applicationSettings.minimumWindowHeight);
        package.applicationSettings.maximumWindowWidth = application.value("maximumWindowWidth", package.applicationSettings.maximumWindowWidth);
        package.applicationSettings.maximumWindowHeight = application.value("maximumWindowHeight", package.applicationSettings.maximumWindowHeight);
        package.applicationSettings.initialWindowPositionSpecified = application.value("initialWindowPositionSpecified", package.applicationSettings.initialWindowPositionSpecified);
        package.applicationSettings.initialWindowPositionX = application.value("initialWindowPositionX", package.applicationSettings.initialWindowPositionX);
        package.applicationSettings.initialWindowPositionY = application.value("initialWindowPositionY", package.applicationSettings.initialWindowPositionY);
        package.applicationSettings.initialMonitorName = application.value("initialMonitorName", package.applicationSettings.initialMonitorName);
        package.applicationSettings.highDpiAware = application.value("highDpiAware", package.applicationSettings.highDpiAware);
        package.applicationSettings.perMonitorScaling = application.value("perMonitorScaling", package.applicationSettings.perMonitorScaling);
        package.applicationSettings.contentScaleOverride = application.value("contentScaleOverride", package.applicationSettings.contentScaleOverride);
        package.applicationSettings.integerOnlyScaling = application.value("integerOnlyScaling", package.applicationSettings.integerOnlyScaling);
        package.applicationSettings.focusedFrameRateCap = application.value("focusedFrameRateCap", package.applicationSettings.focusedFrameRateCap);
        package.applicationSettings.unfocusedFrameRateCap = application.value("unfocusedFrameRateCap", package.applicationSettings.unfocusedFrameRateCap);
        package.applicationSettings.allowDisplaySleep = application.value("allowDisplaySleep", package.applicationSettings.allowDisplaySleep);
        package.applicationSettings.cursorImagePath = application.value("cursorImagePath", package.applicationSettings.cursorImagePath.generic_string());
        package.applicationSettings.cursorHotspotX = application.value("cursorHotspotX", package.applicationSettings.cursorHotspotX);
        package.applicationSettings.cursorHotspotY = application.value("cursorHotspotY", package.applicationSettings.cursorHotspotY);
        package.applicationSettings.cursorVisible = application.value("cursorVisible", package.applicationSettings.cursorVisible);
        package.applicationSettings.cursorConfinedToWindow = application.value("cursorConfinedToWindow", package.applicationSettings.cursorConfinedToWindow);
        package.applicationSettings.windowTitleFormat = application.value("windowTitleFormat", package.applicationSettings.windowTitleFormat);
        package.applicationSettings.appendSceneNameInEditorPlayMode = application.value("appendSceneNameInEditorPlayMode", package.applicationSettings.appendSceneNameInEditorPlayMode);
        if (application.contains("allowedOrientations")) {
            const JsonUVE& allowed = application.at("allowedOrientations");
            if (!allowed.is_array() || allowed.empty() || allowed.size() > 6U) {
                return std::nullopt;
            }
            package.applicationSettings.allowedOrientations.clear();
            for (const JsonUVE& orientationName : allowed) {
                if (!orientationName.is_string()) {
                    return std::nullopt;
                }
                const auto parsed = ParseDisplayOrientationUVE(orientationName.get<std::string>());
                if (!parsed || *parsed == DisplayOrientationUVE::Auto) {
                    return std::nullopt;
                }
                package.applicationSettings.allowedOrientations.push_back(*parsed);
            }
        }
        package.applicationSettings.userDataDirectoryName =
            application.value("userDataDirectoryName", package.applicationSettings.userDataDirectoryName);
        package.applicationSettings.portableUserData =
            application.value("portableUserData", package.applicationSettings.portableUserData);
        package.applicationSettings.crashHandlerEnabled =
            application.value("crashHandlerEnabled", package.applicationSettings.crashHandlerEnabled);
        package.applicationSettings.crashDumpDirectory =
            application.value("crashDumpDirectory", package.applicationSettings.crashDumpDirectory.generic_string());
        package.applicationSettings.symbolUploadEndpoint =
            application.value("symbolUploadEndpoint", package.applicationSettings.symbolUploadEndpoint);

        if (application.contains("splashBackgroundColor")) {
            const JsonUVE& color = application.at("splashBackgroundColor");
            if (!color.is_array() || color.size() != package.applicationSettings.splashBackgroundColor.size()) {
                return std::nullopt;
            }
            for (std::size_t index = 0U; index < package.applicationSettings.splashBackgroundColor.size(); ++index) {
                package.applicationSettings.splashBackgroundColor[index] = color.at(index).get<float>();
            }
        }
        if (application.contains("applicationIdentifiersByTarget")) {
            const JsonUVE& identifiers = application.at("applicationIdentifiersByTarget");
            if (!identifiers.is_object()) {
                return std::nullopt;
            }
            for (const auto& [target, identifier] : identifiers.items()) {
                package.applicationSettings.applicationIdentifiersByTarget.emplace(target, identifier.get<std::string>());
            }
        }
        if (application.contains("iconPathsByTarget")) {
            const JsonUVE& iconTargets = application.at("iconPathsByTarget");
            if (!iconTargets.is_object()) {
                return std::nullopt;
            }
            for (const auto& [target, iconSizes] : iconTargets.items()) {
                if (!iconSizes.is_object()) {
                    return std::nullopt;
                }
                auto& targetIcons = package.applicationSettings.iconPathsByTarget[target];
                for (const auto& [sizeText, iconPath] : iconSizes.items()) {
                    std::uint32_t pixelSize = 0U;
                    const auto [end, error] =
                        std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), pixelSize);
                    if (error != std::errc{} || end != sizeText.data() + sizeText.size()) {
                        return std::nullopt;
                    }
                    targetIcons.emplace(pixelSize, iconPath.get<std::string>());
                }
            }
        }
    }
    // value(...) rather than at(...): older .uvproject files predate this field and have no
    // "startupScenePath" key at all - they must still load, just with no startup scene configured.
    package.startupScenePath = json.value("startupScenePath", std::string{});
    return package;
}

[[nodiscard]] JsonUVE EncodePackageUVE(const EditorProjectPackageUVE& package) {
    JsonUVE applicationSettings{{"publisherName", package.applicationSettings.publisherName},
                                {"copyrightLine", package.applicationSettings.copyrightLine},
                                {"splashImagePath", package.applicationSettings.splashImagePath.generic_string()},
                                {"splashBackgroundColor", package.applicationSettings.splashBackgroundColor},
                                {"splashFadeSeconds", package.applicationSettings.splashFadeSeconds},
                                {"splashMinimumDisplaySeconds", package.applicationSettings.splashMinimumDisplaySeconds},
                                {"splashSkippable", package.applicationSettings.splashSkippable},
                                {"skipSplashInEditorPlayMode", package.applicationSettings.skipSplashInEditorPlayMode},
                                {"quitOnLastWindowClosed", package.applicationSettings.quitOnLastWindowClosed},
                                {"enforceSingleInstance", package.applicationSettings.enforceSingleInstance},
                                {"userDataDirectoryName", package.applicationSettings.userDataDirectoryName},
                                {"portableUserData", package.applicationSettings.portableUserData},
                                {"crashHandlerEnabled", package.applicationSettings.crashHandlerEnabled},
                                {"crashDumpDirectory", package.applicationSettings.crashDumpDirectory.generic_string()},
                                {"symbolUploadEndpoint", package.applicationSettings.symbolUploadEndpoint},
                                {"windowWidth", package.applicationSettings.windowWidth},
                                {"windowHeight", package.applicationSettings.windowHeight},
                                {"windowMode", std::string{GetWindowModeNameUVE(package.applicationSettings.windowMode)}},
                                {"windowResizable", package.applicationSettings.windowResizable},
                                {"windowBorderless", package.applicationSettings.windowBorderless},
                                {"windowAlwaysOnTop", package.applicationSettings.windowAlwaysOnTop},
                                {"windowTransparent", package.applicationSettings.windowTransparent},
                                {"minimumWindowWidth", package.applicationSettings.minimumWindowWidth},
                                {"minimumWindowHeight", package.applicationSettings.minimumWindowHeight},
                                {"maximumWindowWidth", package.applicationSettings.maximumWindowWidth},
                                {"maximumWindowHeight", package.applicationSettings.maximumWindowHeight},
                                {"initialWindowPositionSpecified", package.applicationSettings.initialWindowPositionSpecified},
                                {"initialWindowPositionX", package.applicationSettings.initialWindowPositionX},
                                {"initialWindowPositionY", package.applicationSettings.initialWindowPositionY},
                                {"initialMonitorName", package.applicationSettings.initialMonitorName},
                                {"highDpiAware", package.applicationSettings.highDpiAware},
                                {"perMonitorScaling", package.applicationSettings.perMonitorScaling},
                                {"contentScaleOverride", package.applicationSettings.contentScaleOverride},
                                {"stretchMode", std::string{GetStretchModeNameUVE(package.applicationSettings.stretchMode)}},
                                {"aspectPolicy", std::string{GetAspectPolicyNameUVE(package.applicationSettings.aspectPolicy)}},
                                {"integerOnlyScaling", package.applicationSettings.integerOnlyScaling},
                                {"orientation", std::string{GetDisplayOrientationNameUVE(package.applicationSettings.orientation)}},
                                {"vsyncMode", std::string{GetVSyncModeNameUVE(package.applicationSettings.vsyncMode)}},
                                {"focusedFrameRateCap", package.applicationSettings.focusedFrameRateCap},
                                {"unfocusedFrameRateCap", package.applicationSettings.unfocusedFrameRateCap},
                                {"allowDisplaySleep", package.applicationSettings.allowDisplaySleep},
                                {"cursorImagePath", package.applicationSettings.cursorImagePath.generic_string()},
                                {"cursorHotspotX", package.applicationSettings.cursorHotspotX},
                                {"cursorHotspotY", package.applicationSettings.cursorHotspotY},
                                {"cursorVisible", package.applicationSettings.cursorVisible},
                                {"cursorConfinedToWindow", package.applicationSettings.cursorConfinedToWindow},
                                {"windowTitleFormat", package.applicationSettings.windowTitleFormat},
                                {"appendSceneNameInEditorPlayMode", package.applicationSettings.appendSceneNameInEditorPlayMode}};
    JsonUVE allowedOrientations = JsonUVE::array();
    for (const DisplayOrientationUVE orientation : package.applicationSettings.allowedOrientations) {
        allowedOrientations.push_back(std::string{GetDisplayOrientationNameUVE(orientation)});
    }
    applicationSettings["allowedOrientations"] = std::move(allowedOrientations);
    JsonUVE identifiers = JsonUVE::object();
    for (const auto& [target, identifier] : package.applicationSettings.applicationIdentifiersByTarget) {
        identifiers[target] = identifier;
    }
    JsonUVE iconTargets = JsonUVE::object();
    for (const auto& [target, icons] : package.applicationSettings.iconPathsByTarget) {
        JsonUVE iconSizes = JsonUVE::object();
        for (const auto& [pixelSize, iconPath] : icons) {
            iconSizes[std::to_string(pixelSize)] = iconPath.generic_string();
        }
        iconTargets[target] = std::move(iconSizes);
    }
    applicationSettings["applicationIdentifiersByTarget"] = std::move(identifiers);
    applicationSettings["iconPathsByTarget"] = std::move(iconTargets);

    return JsonUVE{{"format", "uvproject"},
                   {"schemaVersion", package.schemaVersion},
                   {"revision", package.revision},
                   {"projectId", package.projectId},
                   {"displayName", package.displayName},
                   {"engineVersion", {{"major", package.engineVersion.major},
                                       {"minor", package.engineVersion.minor},
                                       {"patch", package.engineVersion.patch},
                                       {"build", package.engineVersion.build}}},
                   {"productMetadata", {{"name", package.productMetadata.name},
                                        {"shortName", package.productMetadata.shortName},
                                        {"description", package.productMetadata.description},
                                        {"version", package.productMetadata.version},
                                        {"buildNumber", package.productMetadata.buildNumber}}},
                   {"applicationSettings", std::move(applicationSettings)},
                   {"contentRoot", package.contentRoot.generic_string()},
                   {"assetDatabasePath", package.assetDatabasePath.generic_string()},
                   {"settingsPath", package.settingsPath.generic_string()},
                   {"startupScenePath", package.startupScenePath.generic_string()}};
}

[[nodiscard]] EditorProjectPackageResultUVE WriteJsonAtomicallyUVE(
    const std::filesystem::path& packagePath, const JsonUVE& json) {
    std::error_code error;
    const std::filesystem::path parent = packagePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::WriteFailed,
                                 "Unable to create the .uvproject parent directory.");
        }
    }

    const std::filesystem::path temporaryPath = packagePath.string() + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::WriteFailed,
                                 "Unable to open the temporary .uvproject file.");
        }
        output << json.dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporaryPath, error);
            return MakeResultUVE(EditorProjectPackageCodeUVE::WriteFailed,
                                 "Unable to write the complete temporary .uvproject file.");
        }
    }

    error.clear();
    std::filesystem::rename(temporaryPath, packagePath, error);
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        return MakeResultUVE(EditorProjectPackageCodeUVE::WriteFailed,
                             "Unable to atomically publish the .uvproject file.");
    }
    return MakeResultUVE(EditorProjectPackageCodeUVE::Applied, "The .uvproject package was published.");
}

} // namespace

EditorProjectPackageResultUVE EditorProjectPackageCodecUVE::ValidateUVE(
    const EditorProjectPackageUVE& package) noexcept {
    if (package.schemaVersion != kCurrentEditorProjectSchemaVersionUVE) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::UnsupportedSchema,
                             "The .uvproject schema version is unsupported.");
    }
    if (package.revision == 0U) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                             "The .uvproject revision must be nonzero.");
    }
    if (!IsProjectIdUVE(package.projectId)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                             "The .uvproject project ID is empty or contains unsupported characters.");
    }
    if (!IsBoundedTextUVE(package.displayName, kMaximumEditorProjectNameBytesUVE, true)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                             "The .uvproject display name is empty or exceeds its bound.");
    }
    if (!IsBoundedTextUVE(package.productMetadata.name, kMaximumEditorProductNameBytesUVE, false) ||
        !IsBoundedTextUVE(package.productMetadata.shortName, kMaximumEditorProductShortNameBytesUVE, false) ||
        !IsBoundedTextUVE(package.productMetadata.description, kMaximumEditorProductDescriptionBytesUVE, false) ||
        !IsBoundedTextUVE(package.productMetadata.version, kMaximumEditorProductVersionBytesUVE, true)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                             "The .uvproject product metadata exceeds its bounds, contains a null byte, or "
                             "has an empty version.");
    }
    if (!IsRelativePathUVE(package.contentRoot) || !IsRelativePathUVE(package.assetDatabasePath) ||
        !IsRelativePathUVE(package.settingsPath)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                             "The .uvproject paths must be bounded, relative, normalized, and traversal-free.");
    }
    const EditorProjectApplicationSettingsUVE& application = package.applicationSettings;
    if (!IsBoundedTextUVE(application.publisherName, kMaximumEditorPublisherNameBytesUVE, false) ||
        !IsBoundedTextUVE(application.copyrightLine, kMaximumEditorCopyrightLineBytesUVE, false) ||
        !IsUserDataDirectoryNameUVE(application.userDataDirectoryName) ||
        !IsHttpsEndpointUVE(application.symbolUploadEndpoint) ||
        !std::isfinite(application.splashFadeSeconds) || application.splashFadeSeconds < 0.0 ||
        application.splashFadeSeconds > 60.0 || !std::isfinite(application.splashMinimumDisplaySeconds) ||
        application.splashMinimumDisplaySeconds < 0.0 || application.splashMinimumDisplaySeconds > 600.0 ||
        application.windowWidth == 0U || application.windowWidth > 16384U || application.windowHeight == 0U ||
        application.windowHeight > 16384U || application.minimumWindowWidth > 16384U ||
        application.minimumWindowHeight > 16384U || application.maximumWindowWidth > 16384U ||
        application.maximumWindowHeight > 16384U ||
        (application.minimumWindowWidth != 0U && application.maximumWindowWidth != 0U &&
         application.minimumWindowWidth > application.maximumWindowWidth) ||
        (application.minimumWindowHeight != 0U && application.maximumWindowHeight != 0U &&
         application.minimumWindowHeight > application.maximumWindowHeight) ||
        application.initialWindowPositionX < -131072 || application.initialWindowPositionX > 131072 ||
        application.initialWindowPositionY < -131072 || application.initialWindowPositionY > 131072 ||
        !IsBoundedTextUVE(application.initialMonitorName, 256U, false) ||
        !std::isfinite(application.contentScaleOverride) || application.contentScaleOverride < 0.0 ||
        application.contentScaleOverride > kMaximumContentScaleOverrideUVE || application.focusedFrameRateCap > 1000U ||
        application.unfocusedFrameRateCap > 1000U || application.cursorHotspotX > 8192U ||
        application.cursorHotspotY > 8192U || !IsBoundedTextUVE(application.windowTitleFormat, 256U, true) ||
        GetWindowModeNameUVE(application.windowMode).empty() || GetVSyncModeNameUVE(application.vsyncMode).empty() ||
        GetStretchModeNameUVE(application.stretchMode).empty() ||
        GetAspectPolicyNameUVE(application.aspectPolicy).empty() ||
        GetDisplayOrientationNameUVE(application.orientation).empty() || application.allowedOrientations.empty() ||
        application.allowedOrientations.size() > 6U) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                             "The .uvproject application metadata exceeds its bounds or contains an invalid value.");
    }
    std::array<bool, 6U> orientationSeen{};
    bool requestedOrientationAllowed = application.orientation == DisplayOrientationUVE::Auto;
    for (const DisplayOrientationUVE orientation : application.allowedOrientations) {
        const std::string_view orientationName = GetDisplayOrientationNameUVE(orientation);
        const auto parsedOrientation = ParseDisplayOrientationUVE(orientationName);
        if (!parsedOrientation || *parsedOrientation == DisplayOrientationUVE::Auto) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                                 "The allowed orientation list contains an unsupported value.");
        }
        const std::size_t orientationIndex = static_cast<std::size_t>(*parsedOrientation);
        if (orientationIndex >= orientationSeen.size() || orientationSeen[orientationIndex]) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                                 "The allowed orientation list must contain unique concrete orientations.");
        }
        orientationSeen[orientationIndex] = true;
        requestedOrientationAllowed = requestedOrientationAllowed || *parsedOrientation == application.orientation;
    }
    if (!requestedOrientationAllowed) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                             "The requested orientation must be included in the allowed orientation list.");
    }
    for (const float channel : application.splashBackgroundColor) {
        if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                                 "The .uvproject splash background color must contain finite channels in [0, 1].");
        }
    }
    if (!application.splashImagePath.empty() && !IsRelativePathUVE(application.splashImagePath)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                             "The .uvproject splash image path must be relative, normalized, and traversal-free.");
    }
    if (!application.cursorImagePath.empty() &&
        (!IsRelativePathUVE(application.cursorImagePath) ||
         LowercaseExtensionUVE(application.cursorImagePath) != ".png")) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                             "The .uvproject cursor image must be a supported PNG under Content.");
    }
    if (!IsRelativePathUVE(application.crashDumpDirectory)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                             "The .uvproject crash dump directory must be relative, normalized, and traversal-free.");
    }
    for (const auto& [target, identifier] : application.applicationIdentifiersByTarget) {
        if (!IsSupportedApplicationTargetUVE(target) || !IsReverseDomainIdentifierUVE(identifier)) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                                 "The .uvproject application identifiers must use supported targets and "
                                 "reverse-domain names.");
        }
    }
    for (const auto& [target, icons] : application.iconPathsByTarget) {
        if (!IsSupportedApplicationTargetUVE(target) || icons.empty() ||
            icons.size() > kMaximumEditorIconSizesPerTargetUVE) {
            return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPackage,
                                 "The .uvproject icon set has an unsupported target or size count.");
        }
        for (const auto& [pixelSize, iconPath] : icons) {
            if (pixelSize == 0U || pixelSize > 8192U || !IsRelativePathUVE(iconPath) ||
                !IsSupportedIconExtensionUVE(iconPath)) {
                return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                                     "The .uvproject icon paths must be bounded supported image files under Content.");
            }
        }
    }
    // startupScenePath is allowed to be empty (no startup scene configured yet), unlike the three
    // paths above which every project always has - but once set, it must be just as safe.
    if (!package.startupScenePath.empty() && !IsRelativePathUVE(package.startupScenePath)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                             "The .uvproject startup scene path must be bounded, relative, normalized, and "
                             "traversal-free.");
    }
    return MakeResultUVE(EditorProjectPackageCodeUVE::Applied, "The .uvproject package is valid.");
}

EditorProjectPackageLoadResultUVE EditorProjectPackageCodecUVE::LoadUVE(
    const std::filesystem::path& packagePath) {
    if (!IsPackagePathUVE(packagePath)) {
        return {MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                              "The project package path must use the .uvproject extension."), std::nullopt};
    }

    // A project saved before the rename is still "Name.uveditor": take it over under the new name.
    std::error_code migrateError;
    if (!std::filesystem::exists(packagePath, migrateError)) {
        std::filesystem::path legacyPath = packagePath;
        legacyPath.replace_extension(".uveditor");
        if (std::filesystem::is_regular_file(legacyPath, migrateError)) {
            std::filesystem::rename(legacyPath, packagePath, migrateError);
        }
    }

    std::ifstream input(packagePath, std::ios::binary);
    if (!input.is_open()) {
        return {MakeResultUVE(EditorProjectPackageCodeUVE::ReadFailed,
                              "Unable to open the .uvproject package."), std::nullopt};
    }

    try {
        const JsonUVE json = JsonUVE::parse(input);
        const std::optional<EditorProjectPackageUVE> package = DecodePackageUVE(json);
        if (!package.has_value()) {
            return {MakeResultUVE(EditorProjectPackageCodeUVE::ParseFailed,
                                  "The .uvproject package has an invalid format marker."), std::nullopt};
        }
        const EditorProjectPackageResultUVE validation = ValidateUVE(*package);
        if (!validation.IsAcceptedUVE()) {
            return {validation, std::nullopt};
        }
        return {validation, package};
    } catch (const std::exception&) {
        return {MakeResultUVE(EditorProjectPackageCodeUVE::ParseFailed,
                              "The .uvproject package is malformed or missing required fields."), std::nullopt};
    }
}

EditorProjectPackageResultUVE EditorProjectPackageCodecUVE::SaveUVE(
    const std::filesystem::path& packagePath, const EditorProjectPackageUVE& package) {
    if (!IsPackagePathUVE(packagePath)) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::InvalidPath,
                             "The project package path must use the .uvproject extension.");
    }
    const EditorProjectPackageResultUVE validation = ValidateUVE(package);
    if (!validation.IsAcceptedUVE()) {
        return validation;
    }
    return WriteJsonAtomicallyUVE(packagePath, EncodePackageUVE(package));
}

EditorProjectPackageResultUVE EditorProjectPackageCodecUVE::ApplyUpdateUVE(
    const std::filesystem::path& packagePath, const std::uint64_t expectedRevision,
    const EditorProjectPackageUVE& replacement) {
    const EditorProjectPackageLoadResultUVE current = LoadUVE(packagePath);
    if (!current.IsAcceptedUVE()) {
        return current.result;
    }
    if (current.package->revision != expectedRevision) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::RevisionConflict,
                             "The .uvproject update expected a different current revision.");
    }
    if (current.package->projectId != replacement.projectId) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::ProjectIdentityConflict,
                             "The .uvproject update belongs to a different project.");
    }
    if (replacement.revision <= current.package->revision) {
        return MakeResultUVE(EditorProjectPackageCodeUVE::RevisionConflict,
                             "The .uvproject replacement revision must be strictly newer.");
    }
    const EditorProjectPackageResultUVE validation = ValidateUVE(replacement);
    if (!validation.IsAcceptedUVE()) {
        return validation;
    }
    return WriteJsonAtomicallyUVE(packagePath, EncodePackageUVE(replacement));
}

} // namespace UVE::Platform
