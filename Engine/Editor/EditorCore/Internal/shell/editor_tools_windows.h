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

class EditorUVE;
class EditorHostUVE;

/// Network Capture, drawn with PRISM in a window of its own: the captured packets (filtered by
/// direction, channel, malformed and text), the picked one's line, size and hex dump below.
class EditorNetworkCaptureWindowUVE final {
public:
    EditorNetworkCaptureWindowUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorNetworkCaptureWindowUVE();
    EditorNetworkCaptureWindowUVE(const EditorNetworkCaptureWindowUVE&) = delete;
    EditorNetworkCaptureWindowUVE& operator=(const EditorNetworkCaptureWindowUVE&) = delete;

    void FrameUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }
    [[nodiscard]] EditorUVE& EditorRefUVE() noexcept { return m_editor; }
    [[nodiscard]] const std::vector<std::size_t>& RowsUVE() const noexcept { return m_rows; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
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
class EditorAudioMixerWindowUVE final {
public:
    EditorAudioMixerWindowUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorAudioMixerWindowUVE();
    EditorAudioMixerWindowUVE(const EditorAudioMixerWindowUVE&) = delete;
    EditorAudioMixerWindowUVE& operator=(const EditorAudioMixerWindowUVE&) = delete;

    void FrameUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    int m_window = 0;
    bool m_closePending = false;
    std::shared_ptr<prism::Widget> m_table;
    std::shared_ptr<prism::Label> m_summary;
};

} // namespace UVE::Editor
