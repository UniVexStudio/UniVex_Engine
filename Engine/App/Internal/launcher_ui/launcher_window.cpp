// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "launcher_window.h"

#include <cstdio>
#include <exception>
#include <iostream>
#include <span>

#include "editor_host.h"
#include "engine_core.h"
#include "glfw_window_handle.h"
#include "logging_macros.h"
#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Theme.h"
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
    config.nativeWindowHandleUVE = desc.nativeSurface;
    if (!desc.shaderDirectory.empty()) {
        config.shaderSourceRealDirectoryUVE = desc.shaderDirectory;
    }
#if defined(__ANDROID__)
    config.renderBackendPreferenceUVE = Core::RenderBackendPreferenceUVE::OpenGLUVE;
#endif

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
        if (!host.InitializeUVE(Window::GlfwWindowFromNativeHandleUVE(nativeWindow), assets)) {
            std::cerr << desc.title << ": the interface could not start\n";
            core.Shutdown();
            return kLauncherStartupFailedExitCodeUVE;
        }
        // The safe area pads the content; the frame's background still fills the whole surface.
        auto safeFrame = prism::make<prism::Border>();
        safeFrame->setBackground(&prism::Theme::panel);
        safeFrame->setChild(buildRoot());
        prism::Margin safeArea;
        if (desc.windowChrome) {
            prism::WindowChromeOptions chrome;
            chrome.title = desc.title;
            chrome.showIdentity = desc.showTitleInChrome;
            host.SetRootUVE(prism::wrapWithWindowChrome(safeFrame, Editor::EditorHostUVE::kMainWindowUVE, chrome));
        } else {
            host.SetRootUVE(safeFrame);
        }
        core.SetPostRenderCallbackUVE([&host, &tick, &desc, &safeFrame, &safeArea] {
            if (desc.safeArea) {
                const prism::Margin area = desc.safeArea();
                if (area.l != safeArea.l || area.t != safeArea.t || area.r != safeArea.r || area.b != safeArea.b) {
                    safeArea = area;
                    safeFrame->setPadding(area);
                }
            }
            if (tick) {
                tick();
            }
            host.BeginFrameUVE();
            host.RenderUVE(std::span<const Editor::EditorHostUVE::ViewImageUVE>{});
        });

        int framesRun = 0;
        while (!host.QuitRequestedUVE() && !core.GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() &&
               (!desc.frameLimit.has_value() || framesRun < *desc.frameLimit)) {
            if (desc.beforeFrame && !desc.beforeFrame(core)) {
                break;
            }
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
