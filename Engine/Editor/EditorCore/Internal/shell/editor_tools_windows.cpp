// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_tools_windows.h"

#include "editor.h"
#include "editor_audio_mixer.h"
#include "editor_chrome.h"
#include "editor_host.h"

#include "network_inspector.h"
#include "network_packet_capture.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace UVE::Editor {

constexpr float kToolRowUVE = 22.0F;

/// The title bar every tool window has: its name, a drag strip, minimise / maximise / close.
[[nodiscard]] static prism::Ref<prism::Widget> TitleBarUVE(const std::string& name, std::function<void()> onClose) {
    auto title = prism::hbox(6.0F);
    title->setPadding(prism::Margin(10.0F, 0.0F, 0.0F, 0.0F));
    auto titleText = prism::make<prism::Label>(name);
    titleText->setColor(prism::ThemeColor(&prism::Theme::textBright));
    title->add(titleText, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    title->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*title, std::move(onClose));
    auto titleBox = prism::make<prism::SizeBox>();
    titleBox->setHeightOverride(kTitleRowUVE + 4.0F);
    titleBox->setContent(title);
    auto titleBar = prism::make<prism::Border>();
    titleBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    titleBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    titleBar->setChild(titleBox);
    return titleBar;
}

// ------------------------------------------------------------------------------ network capture

/// The captured packets as rows (the filtered ones, newest at the bottom); a click picks one.
class PacketListUVE final : public prism::Widget {
public:
    explicit PacketListUVE(EditorNetworkCaptureWindowUVE& window) : m_window(window) {}
    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        EditorUVE& editor = m_window.EditorRefUVE();
        const std::vector<std::size_t>& rows = m_window.RowsUVE();
        const auto visible = static_cast<std::size_t>(std::max(1.0F, std::floor(m_rect.h / kToolRowUVE)));
        // Follows the newest packets unless one is picked.
        const std::size_t first = rows.size() > visible ? rows.size() - visible : 0U;
        c.pushClip(m_rect);
        for (std::size_t i = first; i < rows.size(); ++i) {
            const Network::InspectedPacketUVE* const packet = editor.m_packetCapture.TryAtUVE(rows[i]);
            if (packet == nullptr) {
                continue;
            }
            const prism::Rect row{m_rect.x, m_rect.y + kToolRowUVE * static_cast<float>(i - first), m_rect.w, kToolRowUVE};
            if (rows[i] == editor.m_captureSelected) {
                c.fillRect(row, t.rowSelected);
            }
            c.textIn(*t.mono, prism::Rect{row.x + 8.0F, row.y, row.w - 16.0F, row.h}, Network::DescribePacketUVE(*packet),
                     packet->malformed ? prism::Color(255, 115, 115) : t.text);
        }
        if (rows.empty()) {
            c.textIn(*t.ui, prism::Rect{m_rect.x + 12.0F, m_rect.y + 8.0F, m_rect.w - 24.0F, 20.0F},
                     "No packets yet: they appear here while a networked session runs.", t.textDim);
        }
        c.popClip();
        m_first = first;
    }
    bool onMouseDown(const prism::MouseEvent& e) override {
        const std::vector<std::size_t>& rows = m_window.RowsUVE();
        const auto row = static_cast<std::size_t>(std::max(0.0F, (e.pos.y - m_rect.y) / kToolRowUVE)) + m_first;
        if (row < rows.size()) {
            m_window.EditorRefUVE().m_captureSelected = rows[row];
            invalidate();
        }
        return true;
    }

private:
    EditorNetworkCaptureWindowUVE& m_window;
    std::size_t m_first = 0U;
};

/// The picked packet: its line, its time and size, its bytes.
class PacketDetailUVE final : public prism::Widget {
public:
    explicit PacketDetailUVE(EditorUVE& editor) : m_editor(editor) {}
    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        const Network::InspectedPacketUVE* const packet = m_editor.m_captureSelected == EditorUVE::kNoCaptureSelectionUVE
                                                              ? nullptr
                                                              : m_editor.m_packetCapture.TryAtUVE(m_editor.m_captureSelected);
        const float x = m_rect.x + 10.0F;
        const float w = m_rect.w - 20.0F;
        if (packet == nullptr) {
            c.textIn(*t.ui, prism::Rect{x, m_rect.y + 8.0F, w, 20.0F}, "Select a packet.", t.textDim);
            return;
        }
        c.pushClip(m_rect);
        float y = m_rect.y + 6.0F;
        c.textIn(*t.ui, prism::Rect{x, y, w, 20.0F}, Network::DescribePacketUVE(*packet), t.textBright);
        y += 22.0F;
        const std::string meta = "t=" + std::to_string(packet->timestampMs) + " ms, " + std::to_string(packet->raw.size()) + " bytes" +
                                 (packet->malformed ? ", MALFORMED" : "");
        c.textIn(*t.small, prism::Rect{x, y, w, 18.0F}, meta, packet->malformed ? prism::Color(255, 115, 115) : t.textDim);
        y += 24.0F;
        const std::string dump = Network::HexdumpPacketUVE(packet->raw);
        std::size_t start = 0U;
        while (start < dump.size() && y < m_rect.bottom()) {
            std::size_t end = dump.find('\n', start);
            if (end == std::string::npos) {
                end = dump.size();
            }
            c.text(*t.mono, prism::Vec2{x, y}, std::string_view(dump).substr(start, end - start), t.text);
            y += 17.0F;
            start = end + 1U;
        }
        c.popClip();
    }

private:
    EditorUVE& m_editor;
};

// ------------------------------------------------------------------------------ audio mixer

/// Every bus as a row: name, Mute, Solo, volume (drag), level meter. Drawn from a fresh snapshot of
/// the mix each frame, so the meters move and nothing is rebuilt.
class MixerTableUVE final : public prism::Widget {
public:
    explicit MixerTableUVE(EditorUVE& editor) : m_editor(editor) {}
    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }

    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        const EditorAudioMixerSnapshotUVE snapshot = SnapshotUVE();
        c.pushClip(m_rect);
        // Header.
        const Columns col = ColumnsUVE();
        c.fillRect(prism::Rect{m_rect.x, m_rect.y, m_rect.w, kToolRowUVE}, t.header);
        c.textIn(*t.small, prism::Rect{col.name, m_rect.y, 200.0F, kToolRowUVE}, "Bus", t.textDim);
        c.textIn(*t.small, prism::Rect{col.mute, m_rect.y, 20.0F, kToolRowUVE}, "M", t.textDim, prism::HAlign::Center);
        c.textIn(*t.small, prism::Rect{col.solo, m_rect.y, 20.0F, kToolRowUVE}, "S", t.textDim, prism::HAlign::Center);
        c.textIn(*t.small, prism::Rect{col.volume, m_rect.y, col.volumeW, kToolRowUVE}, "Volume", t.textDim);
        c.textIn(*t.small, prism::Rect{col.level, m_rect.y, col.levelW, kToolRowUVE}, "Level", t.textDim);
        for (std::size_t i = 0U; i < snapshot.rows.size(); ++i) {
            const EditorAudioMixerRowUVE& row = snapshot.rows[i];
            const float y = m_rect.y + kToolRowUVE * static_cast<float>(i + 1U);
            const prism::Color nameColour = row.isSilencedWithSources ? prism::Color(242, 184, 89) : t.text;
            c.textIn(*t.ui, prism::Rect{col.name + 14.0F * static_cast<float>(row.depth), y, col.mute - col.name - 8.0F, kToolRowUVE}, row.name,
                     nameColour);
            const auto toggle = [&](const float x, const bool on, const prism::Color colour, const char* letter) {
                const prism::Rect box{x, y + 2.0F, 20.0F, kToolRowUVE - 4.0F};
                c.fillRoundRect(box, 3.0F, on ? colour : t.control);
                c.textIn(*t.small, box, letter, on ? prism::Color(16, 16, 16) : t.textDim, prism::HAlign::Center);
            };
            toggle(col.mute, row.muted, prism::Color(230, 90, 80), "M");
            toggle(col.solo, row.soloed, prism::Color(240, 200, 80), "S");
            const prism::Rect track{col.volume, y + 8.0F, col.volumeW, 6.0F};
            c.fillRoundRect(track, 3.0F, t.field);
            c.fillRoundRect(prism::Rect{track.x, track.y, std::max(6.0F, track.w * std::clamp(row.volume, 0.0F, 1.0F)), track.h}, 3.0F, t.accent);
            c.textIn(*t.small, prism::Rect{col.volume + col.volumeW + 4.0F, y, 36.0F, kToolRowUVE},
                     std::to_string(static_cast<int>(std::lround(row.volume * 100.0F))) + "%", t.textDim);
            const prism::Rect meter{col.level, y + 8.0F, col.levelW - 70.0F, 6.0F};
            c.fillRoundRect(meter, 3.0F, t.field);
            c.fillRoundRect(prism::Rect{meter.x, meter.y, meter.w * std::clamp(row.levelNormalized, 0.0F, 1.0F), meter.h}, 3.0F,
                            row.isClipping ? prism::Color(235, 77, 66) : prism::Color(90, 200, 120));
            c.textIn(*t.small, prism::Rect{meter.x + meter.w + 6.0F, y, 64.0F, kToolRowUVE}, row.levelText, t.textDim);
        }
        if (snapshot.rows.empty()) {
            c.textIn(*t.ui, prism::Rect{m_rect.x + 12.0F, m_rect.y + kToolRowUVE + 8.0F, m_rect.w - 24.0F, 20.0F}, "No buses yet.", t.textDim);
        }
        c.popClip();
    }

    bool onMouseDown(const prism::MouseEvent& e) override {
        const EditorAudioMixerSnapshotUVE snapshot = SnapshotUVE();
        const int row = static_cast<int>((e.pos.y - m_rect.y) / kToolRowUVE) - 1;
        if (row < 0 || static_cast<std::size_t>(row) >= snapshot.rows.size()) {
            return true;
        }
        const EditorAudioMixerRowUVE& bus = snapshot.rows[static_cast<std::size_t>(row)];
        Audio::IAudioSystemUVE& audio = m_editor.m_services->GetAudioSystemUVE();
        const Columns col = ColumnsUVE();
        if (e.pos.x >= col.mute && e.pos.x < col.mute + 20.0F) {
            static_cast<void>(audio.SetMixerGroupMutedUVE(bus.name, !bus.muted));
        } else if (e.pos.x >= col.solo && e.pos.x < col.solo + 20.0F) {
            static_cast<void>(audio.SetMixerGroupSoloedUVE(bus.name, !bus.soloed));
        } else if (e.pos.x >= col.volume - 4.0F && e.pos.x <= col.volume + col.volumeW + 4.0F) {
            m_dragBus = bus.name;
            SetVolumeAtUVE(e.pos.x);
        }
        invalidate();
        return true;
    }
    bool onMouseMove(const prism::MouseEvent& e) override {
        if (!m_dragBus.empty()) {
            SetVolumeAtUVE(e.pos.x);
            invalidate();
        }
        return true;
    }
    bool onMouseUp(const prism::MouseEvent&) override {
        m_dragBus.clear();
        return true;
    }

private:
    struct Columns final {
        float name = 0.0F, mute = 0.0F, solo = 0.0F, volume = 0.0F, volumeW = 0.0F, level = 0.0F, levelW = 0.0F;
    };
    [[nodiscard]] Columns ColumnsUVE() const {
        Columns col;
        col.name = m_rect.x + 10.0F;
        col.mute = m_rect.x + std::max(160.0F, m_rect.w * 0.30F);
        col.solo = col.mute + 26.0F;
        col.volume = col.solo + 34.0F;
        col.volumeW = std::max(80.0F, m_rect.w * 0.26F);
        col.level = col.volume + col.volumeW + 48.0F;
        col.levelW = std::max(100.0F, m_rect.right() - col.level - 10.0F);
        return col;
    }
    [[nodiscard]] EditorAudioMixerSnapshotUVE SnapshotUVE() const {
        Audio::IAudioSystemUVE& audio = m_editor.m_services->GetAudioSystemUVE();
        return BuildEditorAudioMixerSnapshotUVE(audio.GetMixerDiagnosticsUVE(),
                                                [&audio](const std::string_view name) { return audio.GetMixerGroupMeterUVE(name); });
    }
    void SetVolumeAtUVE(const float x) {
        const Columns col = ColumnsUVE();
        const float volume = std::clamp((x - col.volume) / std::max(1.0F, col.volumeW), 0.0F, 1.0F);
        // Kept in the project's audio settings when the bus has one; session-only otherwise.
        const std::string_view settingId = EditorAudioBusVolumeSettingIdUVE(m_dragBus);
        if (!settingId.empty()) {
            static_cast<void>(m_editor.m_services->GetProjectSettingsUVE().SetValueUVE(settingId, static_cast<double>(volume)));
        } else {
            static_cast<void>(m_editor.m_services->GetAudioSystemUVE().SetMixerGroupVolumeUVE(m_dragBus, volume));
        }
    }

    EditorUVE& m_editor;
    std::string m_dragBus;
};

// ------------------------------------------------------------------------------ network capture

EditorNetworkCaptureWindowUVE::EditorNetworkCaptureWindowUVE(EditorUVE& editor, EditorHostUVE& host) : m_editor(editor), m_host(host) {}
EditorNetworkCaptureWindowUVE::~EditorNetworkCaptureWindowUVE() = default;

prism::Ref<prism::Widget> EditorNetworkCaptureWindowUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(TitleBarUVE("Network Capture", [this] {
        m_editor.m_showCaptureWindow = false;
        m_closePending = true;
    }));
    auto toolbar = prism::hbox(8.0F);
    toolbar->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    auto clear = prism::make<prism::Button>("Clear", prism::Icon::Close, prism::ButtonStyle::Default);
    clear->onClick = [this] {
        m_editor.ClearCapturedPacketsUVE();
        m_editor.m_captureSelected = EditorUVE::kNoCaptureSelectionUVE;
    };
    toolbar->add(clear, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto direction = prism::make<prism::ComboBox>(std::vector<std::string>{"Both", "Out", "In"}, m_editor.m_captureDirection);
    direction->onChange = [this](const int value) { m_editor.m_captureDirection = value; };
    toolbar->add(direction, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto channel = prism::make<prism::ComboBox>(std::vector<std::string>{"Any", "Wire", "Replication", "RPC", "Prediction", "Shot"},
                                                m_editor.m_captureChannel);
    channel->onChange = [this](const int value) { m_editor.m_captureChannel = value; };
    toolbar->add(channel, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto malformed = prism::make<prism::CheckBox>("Malformed", m_editor.m_captureMalformedOnly);
    malformed->onChange = [this](const bool on) { m_editor.m_captureMalformedOnly = on; };
    toolbar->add(malformed, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto search = prism::make<prism::TextInput>(std::string(m_editor.m_captureText.data()), "Filter packets");
    search->onChanged = [this](const std::string& text) {
        m_editor.m_captureText.fill('\0');
        std::copy_n(text.begin(), std::min(text.size(), m_editor.m_captureText.size() - 1U), m_editor.m_captureText.begin());
    };
    toolbar->add(search, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    window->add(toolbar);
    m_stats = prism::make<prism::Label>("", prism::FontRole::Small);
    m_stats->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto statsRow = prism::hbox();
    statsRow->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 4.0F));
    statsRow->add(m_stats, prism::Slot{1.0F});
    window->add(statsRow);
    auto list = prism::make<PacketListUVE>(*this);
    auto detail = prism::make<PacketDetailUVE>(m_editor);
    m_live = {list, detail};
    auto body = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    body->add(PanelUVE(list), prism::Splitter::Pane{240.0F, 100.0F, 1e9F, true});
    body->add(PanelUVE(detail), prism::Splitter::Pane{220.0F, 100.0F, 1e9F, false});
    window->add(body, prism::Slot{1.0F});
    return window;
}

void EditorNetworkCaptureWindowUVE::FrameUVE() {
    bool& visible = m_editor.m_showCaptureWindow;
    if (m_window != 0 && m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        visible = false;
    }
    if (m_window != 0 && (m_closePending || !visible)) {
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_closePending = false;
        m_stats.reset();
        m_live.clear();
        return;
    }
    m_closePending = false;
    if (!visible) {
        return;
    }
    if (m_window == 0) {
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Network Capture", 900, 600);
        if (m_window == 0) {
            visible = false;
            return;
        }
        m_host.FocusWindowUVE(m_window);
    }
    // The filter and the rows a few times a second: packets arrive live.
    const auto now = std::chrono::steady_clock::now();
    if (now - m_refreshedAt > std::chrono::milliseconds(200)) {
        m_refreshedAt = now;
        m_editor.m_captureFilter.text = std::string(m_editor.m_captureText.data());
        m_editor.m_captureFilter.direction = static_cast<Network::PacketCaptureDirectionUVE>(m_editor.m_captureDirection);
        if (m_editor.m_captureChannel == 0) {
            m_editor.m_captureFilter.channel.reset();
        } else {
            m_editor.m_captureFilter.channel = static_cast<Network::InspectedChannelUVE>(m_editor.m_captureChannel - 1);
        }
        m_editor.m_captureFilter.malformedOnly = m_editor.m_captureMalformedOnly;
        m_rows = m_editor.m_packetCapture.FilteredUVE(m_editor.m_captureFilter);
        if (m_editor.m_captureSelected != EditorUVE::kNoCaptureSelectionUVE &&
            m_editor.m_packetCapture.TryAtUVE(m_editor.m_captureSelected) == nullptr) {
            m_editor.m_captureSelected = EditorUVE::kNoCaptureSelectionUVE;
        }
        const Network::PacketCaptureUVE& capture = m_editor.m_packetCapture;
        const std::string stats = std::to_string(capture.SizeUVE()) + "/" + std::to_string(capture.CapacityUVE()) + " packets (" +
                                  std::to_string(capture.DropsUVE()) + " dropped, " + std::to_string(capture.MalformedUVE()) + " malformed)";
        m_host.AppUVE().runInWindow(m_window, [this, stats] {
            if (m_stats && m_stats->text() != stats) {
                m_stats->setText(stats);
            }
            for (const auto& live : m_live) {
                live->invalidate();
            }
        });
    }
    m_host.RenderWindowUVE(m_window, {});
}

// ------------------------------------------------------------------------------ audio mixer

EditorAudioMixerWindowUVE::EditorAudioMixerWindowUVE(EditorUVE& editor, EditorHostUVE& host) : m_editor(editor), m_host(host) {}
EditorAudioMixerWindowUVE::~EditorAudioMixerWindowUVE() = default;

prism::Ref<prism::Widget> EditorAudioMixerWindowUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(TitleBarUVE("Audio Mixer", [this] {
        m_editor.m_showAudioMixerWindow = false;
        m_closePending = true;
    }));
    m_summary = prism::make<prism::Label>("", prism::FontRole::Small);
    m_summary->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto summaryRow = prism::hbox();
    summaryRow->setPadding(prism::Margin(10.0F, 6.0F, 10.0F, 6.0F));
    summaryRow->add(m_summary, prism::Slot{1.0F});
    window->add(summaryRow);
    m_table = prism::make<MixerTableUVE>(m_editor);
    window->add(PanelUVE(m_table), prism::Slot{1.0F});
    auto hintRow = prism::hbox();
    hintRow->setPadding(prism::Margin(10.0F, 4.0F, 10.0F, 8.0F));
    auto hint = prism::make<prism::Label>("A source's own volume and falloff are in its Details.", prism::FontRole::Small);
    hint->setColor(prism::ThemeColor(&prism::Theme::textDim));
    hintRow->add(hint, prism::Slot{1.0F});
    window->add(hintRow);
    return window;
}

void EditorAudioMixerWindowUVE::FrameUVE() {
    bool& visible = m_editor.m_showAudioMixerWindow;
    if (m_window != 0 && m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        visible = false;
    }
    if (m_window != 0 && (m_closePending || !visible)) {
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_closePending = false;
        m_table.reset();
        m_summary.reset();
        return;
    }
    m_closePending = false;
    if (!visible) {
        return;
    }
    if (m_window == 0) {
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Audio Mixer", 760, 460);
        if (m_window == 0) {
            visible = false;
            return;
        }
        m_host.FocusWindowUVE(m_window);
    }
    Audio::IAudioSystemUVE& audio = m_editor.m_services->GetAudioSystemUVE();
    const Audio::AudioMixerDiagnosticsUVE diagnostics = audio.GetMixerDiagnosticsUVE();
    std::string summary = std::to_string(diagnostics.routedSourceCount) + " sources";
    if (diagnostics.anySoloed) {
        summary += "   Solo active: buses outside every soloed chain are silent.";
    }
    m_host.AppUVE().runInWindow(m_window, [this, summary] {
        if (m_summary && m_summary->text() != summary) {
            m_summary->setText(summary);
        }
        if (m_table) {
            m_table->invalidate();   // the meters move
        }
    });
    m_host.RenderWindowUVE(m_window, {});
}

} // namespace UVE::Editor
