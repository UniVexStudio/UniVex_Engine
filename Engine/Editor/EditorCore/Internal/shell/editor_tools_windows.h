// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

namespace prism {
class Label;
} // namespace prism

namespace UVE::Editor {

class Editor;
class EditorHost;

/// Network Capture, drawn with PRISM in a window of its own: the captured packets (filtered by
/// direction, channel, malformed and text), the picked one's line, size and hex dump below.
class EditorNetworkCaptureWindow final {
public:
    EditorNetworkCaptureWindow(Editor& editor, EditorHost& host);
    ~EditorNetworkCaptureWindow();
    EditorNetworkCaptureWindow(const EditorNetworkCaptureWindow&) = delete;
    EditorNetworkCaptureWindow& operator=(const EditorNetworkCaptureWindow&) = delete;

    void Frame();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }
    [[nodiscard]] Editor& EditorRef() noexcept { return m_editor; }
    [[nodiscard]] const std::vector<std::size_t>& Rows() const noexcept { return m_rows; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();

    Editor& m_editor;
    EditorHost& m_host;
    int m_window = 0;
    bool m_closePending = false;
    std::vector<std::size_t> m_rows;
    std::shared_ptr<prism::Label> m_stats;
    std::vector<std::shared_ptr<prism::Widget>> m_live;   ///< repainted every frame
    std::chrono::steady_clock::time_point m_refreshedAt{};
};

/// The Audio Mixer, drawn with PRISM in a window of its own: every bus of the mix as a row - its
/// name (indented under its parent), Mute and Solo, its volume (drag; kept in the project's audio
/// settings when the bus has one) and its live level meter.
class EditorAudioMixerWindow final {
public:
    EditorAudioMixerWindow(Editor& editor, EditorHost& host);
    ~EditorAudioMixerWindow();
    EditorAudioMixerWindow(const EditorAudioMixerWindow&) = delete;
    EditorAudioMixerWindow& operator=(const EditorAudioMixerWindow&) = delete;

    void Frame();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();

    Editor& m_editor;
    EditorHost& m_host;
    int m_window = 0;
    bool m_closePending = false;
    std::shared_ptr<prism::Widget> m_table;
    std::shared_ptr<prism::Label> m_summary;
};

} // namespace UVE::Editor
