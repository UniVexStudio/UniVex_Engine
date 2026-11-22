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

/// An engine available on this machine: one the launcher downloaded, or a local build.
struct InstalledEngineUVE final {
    Pack::EngineInstallUVE install;
    EngineChannelUVE channel = EngineChannelUVE::Stable;
    /// An engine built from source and found beside the launcher (a development setup). The launcher
    /// did not install it and does not remove it.
    bool localBuild = false;
};

/// Every valid engine under `enginesRoot`, plus `localBuild` when there is one, newest version first.
/// A downloaded engine with the local build's version is not listed twice; the local build wins.
[[nodiscard]] std::vector<InstalledEngineUVE> ListInstalledEnginesUVE(
    const std::filesystem::path& enginesRoot, const std::optional<InstalledEngineUVE>& localBuild = std::nullopt);

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
    /// The engine version that was installed (or that the package holds, when that is known).
    std::optional<Core::VersionUVE> version;
    /// True when the package was checked against a published SHA-256 before it was installed.
    bool verified = false;
};

/// Verifies `archive` against `expected`, extracts it beside `enginesRoot`'s final location and moves
/// it into place as `<enginesRoot>/<version>`. The package's own manifest decides the version, which
/// must match `expectedVersion`. Nothing is left behind on failure.
[[nodiscard]] EngineInstallOutcomeUVE InstallEnginePackageUVE(const std::filesystem::path& archive,
                                                              const Sha256DigestUVE& expected,
                                                              const Core::VersionUVE& expectedVersion,
                                                              const std::filesystem::path& enginesRoot);

/// Installs a package file the user chose by hand (an offline copy, or a dev build).
/// The version comes from the package's own manifest, so the file may have any name. When a checksum
/// file (<package>.sha256) sits beside it, the package must match it; without one the package is
/// installed unverified and the outcome says so. When `replaceExisting` is true (the default), an
/// existing install of the same version is atomically replaced so dev/beta builds update cleanly.
[[nodiscard]] EngineInstallOutcomeUVE InstallLocalEnginePackageUVE(const std::filesystem::path& package,
                                                                   const std::filesystem::path& enginesRoot,
                                                                   bool replaceExisting = true);

/// Removes a downloaded engine. The bundled engine is never under `enginesRoot`.
[[nodiscard]] bool UninstallEngineUVE(const std::filesystem::path& enginesRoot, const Core::VersionUVE& version);

/// Downloads and installs one release on a worker thread; the launcher polls it once a frame.
class EngineDownloadUVE final {
public:
    enum class StateUVE : std::uint8_t { Running, Installed, Failed, Cancelled };

    EngineDownloadUVE(std::unique_ptr<IHttpClientUVE> client, EngineReleaseUVE release,
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
