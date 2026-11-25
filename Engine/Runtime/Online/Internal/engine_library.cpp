// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_library.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <tuple>

#include <nlohmann/json.hpp>

#include "application_runtime.h"
#include "asset_guid.h"
#include "project_compatibility.h"
#include "pack_json.h"
#include "tar_gz_archive.h"

namespace UVE::Online {
namespace {

using Json = nlohmann::json;

inline constexpr std::string_view kOctetStreamMediaType = "application/octet-stream";
inline constexpr std::string_view kDownloadSuffix = ".download";
inline constexpr std::string_view kExtractSuffix = ".extract";

[[nodiscard]] auto VersionKey(const Core::Version& version) {
    return std::tie(version.major, version.minor, version.patch, version.build);
}

[[nodiscard]] Platform::EditorProjectVersion ToProjectVersion(const Core::Version& version) {
    return {version.major, version.minor, version.patch, version.build};
}

/// A name unique to this attempt, so concurrent launchers never share scratch files.
[[nodiscard]] std::string AttemptName(const std::string_view label) {
    return std::string(label) + "-" + std::to_string(Asset::GenerateAssetGuid().value);
}

/// Scratch-file label for a package whose version is not known before it is unpacked.
inline constexpr std::string_view kLocalPackageAttemptLabel = "local";
inline constexpr std::string_view kPreviousInstallSuffix = ".previous";

/// Restores owner write permissions on a directory tree so read-only binaries can be removed cleanly.
void MakeTreeWritableForRemoval(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        return;
    }
    std::filesystem::permissions(root, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied,
                                                       error)) {
        std::error_code permError;
        std::filesystem::permissions(entry.path(), std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, permError);
    }
}

/// Removes a directory tree after ensuring read-only engine files are writable.
bool RemoveEngineDirectory(const std::filesystem::path& directory) {
    MakeTreeWritableForRemoval(directory);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return !error;
}

/// Unpacks an already-verified `archive` into staging and moves it into place under `enginesRoot`.
/// When `expectedVersion` is set the package's manifest must name that version. When `replaceExisting`
/// is true, an existing install of the same version is atomically replaced via swap-aside staging.
[[nodiscard]] EngineInstallOutcome InstallVerifiedArchive(const std::filesystem::path& archive,
                                                                const std::optional<Core::Version>& expectedVersion,
                                                                const std::filesystem::path& enginesRoot,
                                                                const bool replaceExisting) {
    const std::string label = expectedVersion.has_value() ? expectedVersion->ToString()
                                                          : std::string(kLocalPackageAttemptLabel);
    const std::filesystem::path extract =
        enginesRoot / kEngineStagingDirectoryName / (AttemptName(label) + std::string(kExtractSuffix));
    std::error_code error;
    std::filesystem::create_directories(extract, error);
    if (error) {
        return {false, "Unable to prepare the engines folder: " + error.message(), {}};
    }
    const auto discard = [&extract] { static_cast<void>(RemoveEngineDirectory(extract)); };
    const ArchiveExtractResult extracted = ExtractTarGz(archive, extract);
    if (!extracted.extracted) {
        discard();
        return {false, "The package could not be unpacked: " + extracted.message, {}};
    }
    const std::optional<EngineManifest> manifest = ReadEngineManifest(extract);
    if (!manifest.has_value() || !Platform::IsCompatibleEnginePackageTarget(manifest->target) ||
        (expectedVersion.has_value() && manifest->version != *expectedVersion)) {
        const std::string wrongTarget = manifest.has_value() && !manifest->target.empty() ? manifest->target : std::string{};
        discard();
        if (!wrongTarget.empty() && !Platform::IsCompatibleEnginePackageTarget(wrongTarget)) {
            return {false,
                    "This package is for '" + wrongTarget + "', not '" +
                        std::string(Platform::GetEnginePackageTarget()) + "'.",
                    {}};
        }
        return {false,
                expectedVersion.has_value()
                    ? "The package is not engine " + expectedVersion->ToString() + " for this platform."
                    : std::string("The file is not an engine package for this platform."),
                {}};
    }
    const std::string version = manifest->version.ToString();
    const std::filesystem::path destination = enginesRoot / version;
    const bool alreadyExists = std::filesystem::exists(destination, error);
    if (alreadyExists && !replaceExisting) {
        discard();
        return {false, "Engine " + version + " is already installed.", destination, manifest->version};
    }
    const Pack::EngineInstallResult resolved = Pack::ResolveEngineInstall(extract, manifest->version);
    if (!resolved.IsFound()) {
        discard();
        return {false, "The package has no editor in it.", {}, manifest->version};
    }
#if defined(__ANDROID__)
    // Android 14+/17+ Safer Dynamic Code Loading expects dynamically loaded native libraries in
    // app-private storage to be read-only (non-writable) before dlopen().
    std::filesystem::permissions(
        resolved.install->editorExecutable,
        std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove, error);
    error.clear();
#endif

    std::filesystem::path previous;
    if (alreadyExists) {
        previous = enginesRoot / kEngineStagingDirectoryName /
                   (AttemptName(version) + std::string(kPreviousInstallSuffix));
        std::filesystem::rename(destination, previous, error);
        if (error) {
            discard();
            return {false, "Unable to replace the existing engine " + version + ": " + error.message(), destination,
                    manifest->version};
        }
    }
    std::filesystem::rename(extract, destination, error);
    if (error) {
        if (!previous.empty()) {
            std::error_code restoreError;
            std::filesystem::rename(previous, destination, restoreError);
        }
        discard();
        return {false, "Unable to move the engine into place: " + error.message(), {}, manifest->version};
    }
    if (!previous.empty()) {
        static_cast<void>(RemoveEngineDirectory(previous));
        return {true, "Updated engine " + version + ".", destination, manifest->version};
    }
    return {true, "Installed engine " + version + ".", destination, manifest->version};
}

} // namespace

std::optional<EngineManifest> ReadEngineManifest(const std::filesystem::path& engineDirectory) {
    std::ifstream input(engineDirectory / kEngineManifestFileName, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kEngineManifestFormat ||
        json.value("schemaVersion", 0U) != kCurrentEngineManifestSchemaVersion) {
        return std::nullopt;
    }
    const std::optional<Core::Version> version = ParseEngineVersionTag(json.value("version", std::string{}));
    const std::optional<EngineChannel> channel = ParseEngineChannel(json.value("channel", std::string{}));
    if (!version.has_value() || !channel.has_value()) {
        return std::nullopt;
    }
    return EngineManifest{*version, *channel, json.value("target", std::string{})};
}

bool WriteEngineManifest(const std::filesystem::path& engineDirectory, const EngineManifest& manifest) {
    const Json json{{"format", kEngineManifestFormat},
                       {"schemaVersion", kCurrentEngineManifestSchemaVersion},
                       {"version", manifest.version.ToString()},
                       {"channel", EngineChannelName(manifest.channel)},
                       {"target", manifest.target}};
    std::ofstream output(engineDirectory / kEngineManifestFileName, std::ios::binary | std::ios::trunc);
    output << json.dump(Pack::kPackJsonIndent);
    return static_cast<bool>(output);
}

std::vector<InstalledEngine> ListInstalledEngines(const std::filesystem::path& enginesRoot,
                                                        const std::optional<InstalledEngine>& localBuild) {
    std::vector<InstalledEngine> engines;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(enginesRoot, error)) {
        if (!entry.is_directory(error) || entry.path().filename().string().starts_with('.')) {
            continue;
        }
        const std::optional<EngineManifest> manifest = ReadEngineManifest(entry.path());
        if (!manifest.has_value() || (localBuild.has_value() && manifest->version == localBuild->install.version)) {
            continue;
        }
        Pack::EngineInstallResult resolved = Pack::ResolveEngineInstall(entry.path(), manifest->version);
        if (resolved.IsFound()) {
            engines.push_back({std::move(*resolved.install), manifest->channel, false});
        }
    }
    if (localBuild.has_value()) {
        engines.push_back(*localBuild);
    }
    std::sort(engines.begin(), engines.end(), [](const InstalledEngine& lhs, const InstalledEngine& rhs) {
        return VersionKey(lhs.install.version) > VersionKey(rhs.install.version);
    });
    return engines;
}

const InstalledEngine* ChooseEngineForProject(const std::vector<InstalledEngine>& engines,
                                                    const Platform::EditorProjectVersion& projectVersion,
                                                    const std::string_view choice) {
    if (!choice.empty()) {
        for (const InstalledEngine& engine : engines) {
            if (engine.install.version.ToString() == choice) {
                return &engine;
            }
        }
    }
    for (const InstalledEngine& engine : engines) {
        if (ToProjectVersion(engine.install.version) == projectVersion) {
            return &engine;
        }
    }
    // Newest first: the first engine that opens the project is the newest that can.
    for (const InstalledEngine& engine : engines) {
        if (Pack::EvaluateProjectCompatibility(projectVersion, engine.install.version).CanOpen()) {
            return &engine;
        }
    }
    return nullptr;
}

EngineInstallOutcome InstallEnginePackage(const std::filesystem::path& archive, const Sha256Digest& expected,
                                                const Core::Version& expectedVersion,
                                                const std::filesystem::path& enginesRoot) {
    const std::optional<Sha256Digest> actual = HashFileSha256(archive);
    if (!actual.has_value() || *actual != expected) {
        return {false, "The downloaded package does not match its published checksum.", {}};
    }
    std::error_code error;
    const std::filesystem::path destination = enginesRoot / expectedVersion.ToString();
    if (std::filesystem::exists(destination, error)) {
        return {false, "Engine " + expectedVersion.ToString() + " is already installed.", destination, expectedVersion};
    }
    EngineInstallOutcome outcome = InstallVerifiedArchive(archive, expectedVersion, enginesRoot, false);
    outcome.verified = outcome.installed;
    return outcome;
}

EngineInstallOutcome InstallLocalEnginePackage(const std::filesystem::path& package,
                                                     const std::filesystem::path& enginesRoot,
                                                     const bool replaceExisting) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(package, error)) {
        return {false, "There is no package file at " + package.string() + ".", {}};
    }
    std::filesystem::path checksumFile = package;
    checksumFile += kEngineChecksumSuffix;
    const bool hasChecksum = std::filesystem::is_regular_file(checksumFile, error);
    if (hasChecksum) {
        std::ifstream checksumInput(checksumFile);
        std::string checksumText;
        checksumInput >> checksumText;
        const std::optional<Sha256Digest> expected = ParseSha256Hex(checksumText);
        const std::optional<Sha256Digest> actual = HashFileSha256(package);
        if (!expected.has_value()) {
            return {false, "The checksum file beside the package cannot be read.", {}};
        }
        if (!actual.has_value() || *actual != *expected) {
            return {false, "The package does not match the checksum file beside it.", {}};
        }
    }
    EngineInstallOutcome outcome = InstallVerifiedArchive(package, std::nullopt, enginesRoot, replaceExisting);
    outcome.verified = outcome.installed && hasChecksum;
    if (outcome.installed && !hasChecksum) {
        outcome.message += " It had no checksum file, so it was not verified.";
    }
    return outcome;
}

bool UninstallEngine(const std::filesystem::path& enginesRoot, const Core::Version& version) {
    const std::filesystem::path directory = enginesRoot / version.ToString();
    if (!ReadEngineManifest(directory).has_value()) {
        return false;
    }
    return RemoveEngineDirectory(directory);
}

EngineDownload::EngineDownload(std::unique_ptr<IHttpClient> client, EngineRelease release,
                                     std::filesystem::path enginesRoot)
    : m_client(std::move(client)), m_release(std::move(release)), m_enginesRoot(std::move(enginesRoot)) {
    m_total.store(m_release.packageBytes);
    m_worker = std::thread([this] { Run(); });
}

EngineDownload::~EngineDownload() {
    m_cancel.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

float EngineDownload::Progress() const noexcept {
    const std::uint64_t total = m_total.load();
    if (m_state.load() != State::Running) {
        return 1.0F;
    }
    return total == 0U ? 0.0F
                       : std::min(1.0F, static_cast<float>(m_received.load()) / static_cast<float>(total));
}

std::string EngineDownload::Message() const {
    const std::lock_guard lock(m_messageMutex);
    return m_message;
}

void EngineDownload::Finish(const State state, std::string message) {
    {
        const std::lock_guard lock(m_messageMutex);
        m_message = std::move(message);
    }
    m_state.store(state);
}

void EngineDownload::Run() {
    const std::filesystem::path staging = m_enginesRoot / kEngineStagingDirectoryName;
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    const std::filesystem::path package =
        staging / (AttemptName(m_release.version.ToString()) + std::string(kDownloadSuffix));
    const auto cleanUp = [&package] {
        std::error_code ignored;
        std::filesystem::remove(package, ignored);
    };

    HttpRequest checksumRequest;
    checksumRequest.url = m_release.checksumUrl;
    checksumRequest.headers = MakeFeedHeaders(kOctetStreamMediaType);
    const HttpResponse checksumResponse = m_client->Get(checksumRequest);
    // The checksum file is "<hex>" or "<hex>  <file name>".
    const std::optional<Sha256Digest> expected =
        checksumResponse.IsSuccess()
            ? ParseSha256Hex(std::string_view(checksumResponse.body).substr(0U, kSha256DigestBytes * 2U))
            : std::nullopt;
    if (!expected.has_value()) {
        Finish(State::Failed, checksumResponse.reached ? "The release has no usable checksum."
                                                             : "Offline: " + checksumResponse.error);
        return;
    }

    HttpRequest packageRequest;
    packageRequest.url = m_release.packageUrl;
    packageRequest.headers = MakeFeedHeaders(kOctetStreamMediaType);
    const HttpResponse downloaded = m_client->Download(
        packageRequest, package, [this](const std::uint64_t received, const std::uint64_t total) {
            m_received.store(received);
            if (total != 0U) {
                m_total.store(total);
            }
            return !m_cancel.load();
        });
    if (m_cancel.load()) {
        cleanUp();
        Finish(State::Cancelled, "Download cancelled.");
        return;
    }
    if (!downloaded.IsSuccess()) {
        cleanUp();
        Finish(State::Failed, downloaded.reached
                                        ? "The download answered with HTTP " + std::to_string(downloaded.status) + "."
                                        : "Offline: " + downloaded.error);
        return;
    }
    const EngineInstallOutcome outcome =
        InstallEnginePackage(package, *expected, m_release.version, m_enginesRoot);
    cleanUp();
    Finish(outcome.installed ? State::Installed : State::Failed, outcome.message);
}

} // namespace UVE::Online
