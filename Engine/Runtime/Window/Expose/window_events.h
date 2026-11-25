// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

namespace UVE::Window {

/// Published through Events::IEventSystem when the user requests the window be closed (e.g.
/// clicking the OS close button). IWindowManager::IsCloseRequested() is the direct-poll
/// equivalent EngineCore drives its own quit sequence from; this event exists for any other
/// system that wants to observe the same moment (e.g. a future "save before quit?" prompt).
/// Publishing happens only from IWindowManager's own GLFW callback, never from inside GL code.
struct WindowCloseRequestedEvent {};

/// Published through Events::IEventSystem whenever the window's framebuffer size changes.
/// Deliberately carries no side effects of its own — the GLFW callback that publishes this only
/// publishes; it never touches GL state or recreates GPU resources. Consumers (GlRenderDevice)
/// poll IWindowManager::GetWidth()/GetHeight() once per frame instead of reacting to this
/// event synchronously, per the approved design.
struct WindowResizedEvent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Published through Events::IEventSystem whenever the window gains or loses OS input focus.
struct WindowFocusChangedEvent {
    bool focused = false;
};

} // namespace UVE::Window
