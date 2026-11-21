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
#include "tar_gz_archive.h"

namespace UVE::Online {
namespace {

using JsonUVE = nlohmann::json;

inline constexpr std::string_view kOctetStreamMediaTypeUVE = "application/octet-stream";
inline constexpr std::string_view kDownloadSuffixUVE = ".download";
inline constexpr std::string_view kExtractSuffixUVE = ".extract";

[[nodiscard]] auto VersionKeyUVE(const Core::VersionUVE& version) {
    return std::tie(version.major, version.minor, version.patch, version.build);
}

[[nodiscard]] Platform::EditorProjectVersionUVE ToProjectVersionUVE(const Core::VersionUVE& version) {
    return {version.major, version.minor, version.patch, version.build};
}

/// A name unique to this attempt, so concurrent launchers never share scratch files.
[[nodiscard]] std::string AttemptNameUVE(const Core::VersionUVE& version) {
    return version.ToStringUVE() + "-" + std::to_string(Asset::GenerateAssetGuidUVE().value);
}

} // namespace

std::optional<EngineManifestUVE> ReadEngineManifestUVE(const std::filesystem::path& engineDirectory) {
    std::ifstream input(engineDirectory / kEngineManifestFileNameUVE, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kEngineManifestFormatUVE ||
        json.value("schemaVersion", 0U) != kCurrentEngineManifestSchemaVersionUVE) {
        return std::nullopt;
    }
    const std::optional<Core::VersionUVE> version = ParseEngineVersionTagUVE(json.value("version", std::string{}));
    const std::optional<EngineChannelUVE> channel = ParseEngineChannelUVE(json.value("channel", std::string{}));
    if (!version.has_value() || !channel.has_value()) {
        return std::nullopt;
    }
    return EngineManifestUVE{*version, *channel, json.value("target", std::string{})};
}

bool WriteEngineManifestUVE(const std::filesystem::path& engineDirectory, const EngineManifestUVE& manifest) {
    const JsonUVE json{{"format", kEngineManifestFormatUVE},
                       {"schemaVersion", kCurrentEngineManifestSchemaVersionUVE},
                       {"version", manifest.version.ToStringUVE()},
                       {"channel", EngineChannelNameUVE(manifest.channel)},
                       {"target", manifest.target}};
    std::ofstream output(engineDirectory / kEngineManifestFileNameUVE, std::ios::binary | std::ios::trunc);
    output << json.dump(2);
    return static_cast<bool>(output);
}

std::vector<InstalledEngineUVE> ListInstalledEnginesUVE(const std::filesystem::path& enginesRoot,
                                                        const std::optional<InstalledEngineUVE>& localBuild) {
    std::vector<InstalledEngineUVE> engines;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(enginesRoot, error)) {
        if (!entry.is_directory(error) || entry.path().filename().string().starts_with('.')) {
            continue;
        }
        const std::optional<EngineManifestUVE> manifest = ReadEngineManifestUVE(entry.path());
        if (!manifest.has_value() || (localBuild.has_value() && manifest->version == localBuild->install.version)) {
            continue;
        }
        Pack::EngineInstallResultUVE resolved = Pack::ResolveEngineInstallUVE(entry.path(), manifest->version);
        if (resolved.IsFoundUVE()) {
            engines.push_back({std::move(*resolved.install), manifest->channel, false});
        }
    }
    if (localBuild.has_value()) {
        engines.push_back(*localBuild);
    }
    std::sort(engines.begin(), engines.end(), [](const InstalledEngineUVE& lhs, const InstalledEngineUVE& rhs) {
        return VersionKeyUVE(lhs.install.version) > VersionKeyUVE(rhs.install.version);
    });
    return engines;
}

const InstalledEngineUVE* ChooseEngineForProjectUVE(const std::vector<InstalledEngineUVE>& engines,
                                                    const Platform::EditorProjectVersionUVE& projectVersion,
                                                    const std::string_view choice) {
    if (!choice.empty()) {
        for (const InstalledEngineUVE& engine : engines) {
            if (engine.install.version.ToStringUVE() == choice) {
                return &engine;
            }
        }
    }
    for (const InstalledEngineUVE& engine : engines) {
        if (ToProjectVersionUVE(engine.install.version) == projectVersion) {
            return &engine;
        }
    }
    // Newest first: the first engine that opens the project is the newest that can.
    for (const InstalledEngineUVE& engine : engines) {
        if (Pack::EvaluateProjectCompatibilityUVE(projectVersion, engine.install.version).CanOpenUVE()) {
            return &engine;
        }
    }
    return nullptr;
}

EngineInstallOutcomeUVE InstallEnginePackageUVE(const std::filesystem::path& archive, const Sha256DigestUVE& expected,
                                                const Core::VersionUVE& expectedVersion,
                                                const std::filesystem::path& enginesRoot) {
    const std::optional<Sha256DigestUVE> actual = HashFileSha256UVE(archive);
    if (!actual.has_value() || *actual != expected) {
        return {false, "The downloaded package does not match its published checksum.", {}};
    }
    const std::filesystem::path destination = enginesRoot / expectedVersion.ToStringUVE();
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        return {false, "Engine " + expectedVersion.ToStringUVE() + " is already installed.", destination};
    }
    const std::filesystem::path extract =
        enginesRoot / kEngineStagingDirectoryNameUVE / (AttemptNameUVE(expectedVersion) + std::string(kExtractSuffixUVE));
    std::filesystem::create_directories(extract, error);
    if (error) {
        return {false, "Unable to prepare the engines folder: " + error.message(), {}};
    }
    const auto discard = [&extract] {
        std::error_code ignored;
        std::filesystem::remove_all(extract, ignored);
    };
    const ArchiveExtractResultUVE extracted = ExtractTarGzUVE(archive, extract);
    if (!extracted.extracted) {
        discard();
        return {false, "The package could not be unpacked: " + extracted.message, {}};
    }
    const std::optional<EngineManifestUVE> manifest = ReadEngineManifestUVE(extract);
    if (!manifest.has_value() || manifest->version != expectedVersion ||
        manifest->target != Platform::GetApplicationTargetUVE()) {
        discard();
        return {false, "The package is not engine " + expectedVersion.ToStringUVE() + " for this platform.", {}};
    }
    if (!Pack::ResolveEngineInstallUVE(extract, manifest->version).IsFoundUVE()) {
        discard();
        return {false, "The package has no editor in it.", {}};
    }
    std::filesystem::rename(extract, destination, error);
    if (error) {
        discard();
        return {false, "Unable to move the engine into place: " + error.message(), {}};
    }
    return {true, "Installed engine " + expectedVersion.ToStringUVE() + ".", destination};
}

bool UninstallEngineUVE(const std::filesystem::path& enginesRoot, const Core::VersionUVE& version) {
    const std::filesystem::path directory = enginesRoot / version.ToStringUVE();
    if (!ReadEngineManifestUVE(directory).has_value()) {
        return false;
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return !error;
}

EngineDownloadUVE::EngineDownloadUVE(std::unique_ptr<IHttpClientUVE> client, EngineReleaseUVE release,
                                     std::filesystem::path enginesRoot)
    : m_client(std::move(client)), m_release(std::move(release)), m_enginesRoot(std::move(enginesRoot)) {
    m_total.store(m_release.packageBytes);
    m_worker = std::thread([this] { RunUVE(); });
}

EngineDownloadUVE::~EngineDownloadUVE() {
    m_cancel.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

float EngineDownloadUVE::ProgressUVE() const noexcept {
    const std::uint64_t total = m_total.load();
    if (m_state.load() != StateUVE::Running) {
        return 1.0F;
    }
    return total == 0U ? 0.0F
                       : std::min(1.0F, static_cast<float>(m_received.load()) / static_cast<float>(total));
}

std::string EngineDownloadUVE::MessageUVE() const {
    const std::lock_guard lock(m_messageMutex);
    return m_message;
}

void EngineDownloadUVE::FinishUVE(const StateUVE state, std::string message) {
    {
        const std::lock_guard lock(m_messageMutex);
        m_message = std::move(message);
    }
    m_state.store(state);
}

void EngineDownloadUVE::RunUVE() {
    const std::filesystem::path staging = m_enginesRoot / kEngineStagingDirectoryNameUVE;
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    const std::filesystem::path package =
        staging / (AttemptNameUVE(m_release.version) + std::string(kDownloadSuffixUVE));
    const auto cleanUp = [&package] {
        std::error_code ignored;
        std::filesystem::remove(package, ignored);
    };

    HttpRequestUVE checksumRequest;
    checksumRequest.url = m_release.checksumUrl;
    checksumRequest.headers = MakeFeedHeadersUVE(kOctetStreamMediaTypeUVE);
    const HttpResponseUVE checksumResponse = m_client->GetUVE(checksumRequest);
    // The checksum file is "<hex>" or "<hex>  <file name>".
    const std::optional<Sha256DigestUVE> expected =
        checksumResponse.IsSuccessUVE()
            ? ParseSha256HexUVE(std::string_view(checksumResponse.body).substr(0U, kSha256DigestBytesUVE * 2U))
            : std::nullopt;
    if (!expected.has_value()) {
        FinishUVE(StateUVE::Failed, checksumResponse.reached ? "The release has no usable checksum."
                                                             : "Offline: " + checksumResponse.error);
        return;
    }

    HttpRequestUVE packageRequest;
    packageRequest.url = m_release.packageUrl;
    packageRequest.headers = MakeFeedHeadersUVE(kOctetStreamMediaTypeUVE);
    const HttpResponseUVE downloaded = m_client->DownloadUVE(
        packageRequest, package, [this](const std::uint64_t received, const std::uint64_t total) {
            m_received.store(received);
            if (total != 0U) {
                m_total.store(total);
            }
            return !m_cancel.load();
        });
    if (m_cancel.load()) {
        cleanUp();
        FinishUVE(StateUVE::Cancelled, "Download cancelled.");
        return;
    }
    if (!downloaded.IsSuccessUVE()) {
        cleanUp();
        FinishUVE(StateUVE::Failed, downloaded.reached
                                        ? "The download answered with HTTP " + std::to_string(downloaded.status) + "."
                                        : "Offline: " + downloaded.error);
        return;
    }
    const EngineInstallOutcomeUVE outcome =
        InstallEnginePackageUVE(package, *expected, m_release.version, m_enginesRoot);
    cleanUp();
    FinishUVE(outcome.installed ? StateUVE::Installed : StateUVE::Failed, outcome.message);
}

} // namespace UVE::Online
