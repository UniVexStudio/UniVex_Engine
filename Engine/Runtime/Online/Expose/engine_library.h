// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "editor_project_package.h"
#include "engine_install.h"
#include "engine_release_feed.h"
#include "http_client.h"
#include "sha256.h"

namespace UVE::Online {

/// Directory, under the per-user tools directory, holding one folder per installed engine version.
inline constexpr std::string_view kEnginesDirectoryNameUVE = "Engines";
/// Every engine package carries this manifest at its root.
inline constexpr std::string_view kEngineManifestFileNameUVE = "engine.json";
inline constexpr std::string_view kEngineManifestFormatUVE = "uvengine";
inline constexpr std::uint32_t kCurrentEngineManifestSchemaVersionUVE = 1U;
/// Scratch space for downloads and half-finished installs, inside the engines directory.
inline constexpr std::string_view kEngineStagingDirectoryNameUVE = ".staging";

struct EngineManifestUVE final {
    Core::VersionUVE version;
    EngineChannelUVE channel = EngineChannelUVE::Stable;
    std::string target;
};

[[nodiscard]] std::optional<EngineManifestUVE> ReadEngineManifestUVE(const std::filesystem::path& engineDirectory);
[[nodiscard]] bool WriteEngineManifestUVE(const std::filesystem::path& engineDirectory, const EngineManifestUVE& manifest);

/// An engine available on this machine: the one installed with the launcher, or one it downloaded.
struct InstalledEngineUVE final {
    Pack::EngineInstallUVE install;
    EngineChannelUVE channel = EngineChannelUVE::Stable;
    /// The engine shipped with the launcher; it cannot be removed from the launcher.
    bool bundled = false;
};

/// The bundled engine followed by every valid engine under `enginesRoot`, newest version first.
/// A downloaded engine with the bundled engine's version is not listed twice.
[[nodiscard]] std::vector<InstalledEngineUVE> ListInstalledEnginesUVE(const std::filesystem::path& enginesRoot,
                                                                      const InstalledEngineUVE& bundled);

/// Which installed engine opens a project. `choice` is a version the user picked for it ("" lets the
/// launcher decide): that exact engine if installed; otherwise the engine matching the project's
/// recorded version, then the newest that can open it without refusing.
[[nodiscard]] const InstalledEngineUVE* ChooseEngineForProjectUVE(const std::vector<InstalledEngineUVE>& engines,
                                                                  const Platform::EditorProjectVersionUVE& projectVersion,
                                                                  std::string_view choice);

struct EngineInstallOutcomeUVE final {
    bool installed = false;
    std::string message;
    std::filesystem::path directory;
};

/// Verifies `archive` against `expected`, extracts it beside `enginesRoot`'s final location and moves
/// it into place as `<enginesRoot>/<version>`. The package's own manifest decides the version, which
/// must match `expectedVersion`. Nothing is left behind on failure.
[[nodiscard]] EngineInstallOutcomeUVE InstallEnginePackageUVE(const std::filesystem::path& archive,
                                                              const Sha256DigestUVE& expected,
                                                              const Core::VersionUVE& expectedVersion,
                                                              const std::filesystem::path& enginesRoot);

/// Removes a downloaded engine. The bundled engine is never under `enginesRoot`.
[[nodiscard]] bool UninstallEngineUVE(const std::filesystem::path& enginesRoot, const Core::VersionUVE& version);

/// Downloads and installs one release on a worker thread; the launcher polls it once a frame.
class EngineDownloadUVE final {
public:
    enum class StateUVE : std::uint8_t { Running, Installed, Failed, Cancelled };

    EngineDownloadUVE(std::unique_ptr<IHttpClientUVE> client, EngineReleaseUVE release, std::string token,
                      std::filesystem::path enginesRoot);
    EngineDownloadUVE(const EngineDownloadUVE&) = delete;
    EngineDownloadUVE& operator=(const EngineDownloadUVE&) = delete;
    /// Cancels a running transfer and waits for the worker.
    ~EngineDownloadUVE();

    [[nodiscard]] const EngineReleaseUVE& ReleaseUVE() const noexcept { return m_release; }
    [[nodiscard]] StateUVE GetStateUVE() const noexcept { return m_state.load(); }
    /// 0..1 while the package downloads; 1 while it installs.
    [[nodiscard]] float ProgressUVE() const noexcept;
    [[nodiscard]] std::string MessageUVE() const;
    void CancelUVE() noexcept { m_cancel.store(true); }

private:
    void RunUVE();
    void FinishUVE(StateUVE state, std::string message);

    std::unique_ptr<IHttpClientUVE> m_client;
    EngineReleaseUVE m_release;
    std::string m_token;
    std::filesystem::path m_enginesRoot;
    std::atomic<StateUVE> m_state{StateUVE::Running};
    std::atomic<std::uint64_t> m_received{0U};
    std::atomic<std::uint64_t> m_total{0U};
    std::atomic<bool> m_cancel{false};
    mutable std::mutex m_messageMutex;
    std::string m_message;
    std::thread m_worker;
};

} // namespace UVE::Online
