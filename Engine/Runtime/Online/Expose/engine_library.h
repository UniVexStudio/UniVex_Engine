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
inline constexpr std::string_view kEnginesDirectoryName = "Engines";
/// Every engine package carries this manifest at its root.
inline constexpr std::string_view kEngineManifestFileName = "engine.json";
inline constexpr std::string_view kEngineManifestFormat = "uvengine";
inline constexpr std::uint32_t kCurrentEngineManifestSchemaVersion = 1U;
/// Scratch space for downloads and half-finished installs, inside the engines directory.
inline constexpr std::string_view kEngineStagingDirectoryName = ".staging";

struct EngineManifest final {
    Core::Version version;
    EngineChannel channel = EngineChannel::Stable;
    std::string target;
};

[[nodiscard]] std::optional<EngineManifest> ReadEngineManifest(const std::filesystem::path& engineDirectory);
[[nodiscard]] bool WriteEngineManifest(const std::filesystem::path& engineDirectory, const EngineManifest& manifest);

/// An engine available on this machine: one the launcher downloaded, or a local build.
struct InstalledEngine final {
    Pack::EngineInstall install;
    EngineChannel channel = EngineChannel::Stable;
    /// An engine built from source and found beside the launcher (a development setup). The launcher
    /// did not install it and does not remove it.
    bool localBuild = false;
};

/// Every valid engine under `enginesRoot`, plus `localBuild` when there is one, newest version first.
/// A downloaded engine with the local build's version is not listed twice; the local build wins.
[[nodiscard]] std::vector<InstalledEngine> ListInstalledEngines(
    const std::filesystem::path& enginesRoot, const std::optional<InstalledEngine>& localBuild = std::nullopt);

/// Which installed engine opens a project. `choice` is a version the user picked for it ("" lets the
/// launcher decide): that exact engine if installed; otherwise the engine matching the project's
/// recorded version, then the newest that can open it without refusing.
[[nodiscard]] const InstalledEngine* ChooseEngineForProject(const std::vector<InstalledEngine>& engines,
                                                                  const Platform::EditorProjectVersion& projectVersion,
                                                                  std::string_view choice);

struct EngineInstallOutcome final {
    bool installed = false;
    std::string message;
    std::filesystem::path directory;
    /// The engine version that was installed (or that the package holds, when that is known).
    std::optional<Core::Version> version;
    /// True when the package was checked against a published SHA-256 before it was installed.
    bool verified = false;
};

/// Verifies `archive` against `expected`, extracts it beside `enginesRoot`'s final location and moves
/// it into place as `<enginesRoot>/<version>`. The package's own manifest decides the version, which
/// must match `expectedVersion`. Nothing is left behind on failure.
[[nodiscard]] EngineInstallOutcome InstallEnginePackage(const std::filesystem::path& archive,
                                                              const Sha256Digest& expected,
                                                              const Core::Version& expectedVersion,
                                                              const std::filesystem::path& enginesRoot);

/// Installs a package file the user chose by hand (an offline copy, or a dev build).
/// The version comes from the package's own manifest, so the file may have any name. When a checksum
/// file (<package>.sha256) sits beside it, the package must match it; without one the package is
/// installed unverified and the outcome says so. When `replaceExisting` is true (the default), an
/// existing install of the same version is atomically replaced so dev/beta builds update cleanly.
[[nodiscard]] EngineInstallOutcome InstallLocalEnginePackage(const std::filesystem::path& package,
                                                                   const std::filesystem::path& enginesRoot,
                                                                   bool replaceExisting = true);

/// Removes a downloaded engine. The bundled engine is never under `enginesRoot`.
[[nodiscard]] bool UninstallEngine(const std::filesystem::path& enginesRoot, const Core::Version& version);

/// Downloads and installs one release on a worker thread; the launcher polls it once a frame.
class EngineDownload final {
public:
    enum class State : std::uint8_t { Running, Installed, Failed, Cancelled };

    EngineDownload(std::unique_ptr<IHttpClient> client, EngineRelease release,
                      std::filesystem::path enginesRoot);
    EngineDownload(const EngineDownload&) = delete;
    EngineDownload& operator=(const EngineDownload&) = delete;
    /// Cancels a running transfer and waits for the worker.
    ~EngineDownload();

    [[nodiscard]] const EngineRelease& Release() const noexcept { return m_release; }
    [[nodiscard]] State GetState() const noexcept { return m_state.load(); }
    /// 0..1 while the package downloads; 1 while it installs.
    [[nodiscard]] float Progress() const noexcept;
    [[nodiscard]] std::string Message() const;
    void Cancel() noexcept { m_cancel.store(true); }

private:
    void Run();
    void Finish(State state, std::string message);

    std::unique_ptr<IHttpClient> m_client;
    EngineRelease m_release;
    std::filesystem::path m_enginesRoot;
    std::atomic<State> m_state{State::Running};
    std::atomic<std::uint64_t> m_received{0U};
    std::atomic<std::uint64_t> m_total{0U};
    std::atomic<bool> m_cancel{false};
    mutable std::mutex m_messageMutex;
    std::string m_message;
    std::thread m_worker;
};

} // namespace UVE::Online
