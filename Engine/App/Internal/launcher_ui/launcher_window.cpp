// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "launcher_window.h"

#include <cstdio>
#include <exception>
#include <iostream>
#include <span>

#include "editor_host.h"
#include "engine_core.h"
#include "logging_macros.h"
#include "prism/Application.h"
#include "prism/WindowChrome.h"

namespace UVE::LauncherUI {
namespace {

/// Frame-rate caps: a launcher is a quiet window and needs no more.
inline constexpr std::uint32_t kFocusedFrameRateCapUVE = 60U;
inline constexpr std::uint32_t kUnfocusedFrameRateCapUVE = 10U;

} // namespace

void RequestLauncherCloseUVE() {
    if (prism::Application::exists()) {
        prism::Application::get().quit();
    }
}

int RunLauncherWindowUVE(const LauncherWindowDescUVE& desc,
                         const std::function<prism::Ref<prism::Widget>()>& buildRoot,
                         const std::function<void()>& tick) {
    Core::EngineConfigUVE config{};
    config.windowTitle = desc.title;
    config.windowWidth = desc.width;
    config.windowHeight = desc.height;
    // The window draws its own title bar (PRISM window chrome) in place of the OS frame.
    config.windowBorderlessUVE = true;
    config.logFilePath = desc.logFile;
    config.focusedFrameRateCapUVE = kFocusedFrameRateCapUVE;
    config.unfocusedFrameRateCapUVE = kUnfocusedFrameRateCapUVE;
    config.commandLineArgs = desc.commandLine;

    Core::EngineCoreUVE core(config);
    try {
        core.Init();
        if (!core.Load()) {
            core.Shutdown();
            return kLauncherStartupFailedExitCodeUVE;
        }
        void* const nativeWindow = core.GetServicesUVE().GetWindowManagerUVE().GetNativeWindowHandleUVE();
        if (nativeWindow == nullptr) {
            std::cerr << desc.title << ": no window could be created\n";
            core.Shutdown();
            return kLauncherStartupFailedExitCodeUVE;
        }
        const std::filesystem::path& assets = desc.prismAssetsDirectory;
        Editor::EditorHostUVE host;
        if (!host.InitializeUVE(static_cast<GLFWwindow*>(nativeWindow), assets)) {
            std::cerr << desc.title << ": the interface could not start\n";
            core.Shutdown();
            return kLauncherStartupFailedExitCodeUVE;
        }
        prism::WindowChromeOptions chrome;
        chrome.title = desc.title;
        chrome.showIdentity = desc.showTitleInChrome;
        host.SetRootUVE(prism::wrapWithWindowChrome(buildRoot(), Editor::EditorHostUVE::kMainWindowUVE, chrome));
        core.SetPostRenderCallbackUVE([&host, &tick] {
            if (tick) {
                tick();
            }
            host.BeginFrameUVE();
            host.RenderUVE(std::span<const Editor::EditorHostUVE::ViewImageUVE>{});
        });

        int framesRun = 0;
        while (!host.QuitRequestedUVE() && !core.GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() &&
               (!desc.frameLimit.has_value() || framesRun < *desc.frameLimit)) {
            core.TickFrameUVE();
            ++framesRun;
        }
        if (!desc.screenshot.empty() && !host.SaveScreenshotUVE(desc.screenshot)) {
            std::fprintf(stderr, "%s: could not save the screenshot\n", desc.title.c_str());
        }
        core.SetPostRenderCallbackUVE({});
        host.ShutdownUVE();
        core.Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("launcher: unhandled exception - shutting down: {}", exception.what());
    } catch (...) {
        UVE_FATAL("launcher: unhandled non-std::exception - shutting down");
    }
    if (core.GetStateUVE() == Core::EngineStateUVE::Running) {
        try {
            core.Shutdown();
        } catch (...) {
            UVE_FATAL("launcher: shutting down after an exception failed as well");
        }
    }
    return Core::EngineCoreUVE::kUnhandledExceptionExitCodeUVE;
}

} // namespace UVE::LauncherUI
