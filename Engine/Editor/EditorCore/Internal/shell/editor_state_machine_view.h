// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_graph_component.h"

#include "prism/Widget.h"

#include <cstdint>
#include <optional>
#include <string>

namespace UVE::Editor {

class EditorAnimGraphPanelUVE;

/// A State Machine node opened from the Animation Graph: its states as boxes (what each plays, the
/// one running lit), Entry and Any, and the transitions as arrows - two lanes when two states lead to
/// each other, dimmed when off, flashing when taken.
///
/// Click picks a state or an arrow (the side panel edits it); drag a state, Entry or Any to move
/// it; Shift + drag from a state to another makes a transition (from Any: from any state; from
/// Entry: where the machine starts); double-click empty space adds a state, a state opens what it
/// plays; right-click for the menu; Delete removes what is picked; the wheel zooms, a drag on empty
/// space pans, F frames, Esc goes back to the tree.
class StateMachineViewUVE final : public prism::Widget {
public:
    explicit StateMachineViewUVE(EditorAnimGraphPanelUVE& panel) : m_panel(panel) {}

    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onMouseWheel(const prism::MouseEvent& e) override;
    bool onKeyDown(const prism::KeyEvent& e) override;
    bool focusable() const override { return true; }

    /// Fits every box in view.
    void FrameUVE();

private:
    static constexpr int kEntryUVE = -1;
    static constexpr int kAnyUVE = -2;
    static constexpr int kNoneUVE = -3;
    enum class DragUVE : std::uint8_t { None, Pan, Move, Link };

    [[nodiscard]] const Scene::AnimationGraphNodeUVE* MachineUVE() const;
    [[nodiscard]] std::size_t MachineIndexUVE() const;
    [[nodiscard]] prism::Vec2 ToScreenUVE(Math::Vector2UVE world) const;
    [[nodiscard]] Math::Vector2UVE ToWorldUVE(prism::Vec2 screen) const;
    /// A box in world units: a state slot, kEntryUVE or kAnyUVE; with the live drag applied.
    [[nodiscard]] prism::Rect BoxUVE(const Scene::AnimationGraphNodeUVE& machine, int box) const;
    [[nodiscard]] prism::Rect ScreenBoxUVE(const Scene::AnimationGraphNodeUVE& machine, int box) const;
    [[nodiscard]] int BoxAtUVE(const Scene::AnimationGraphNodeUVE& machine, prism::Vec2 screen) const;
    [[nodiscard]] int TransitionAtUVE(const Scene::AnimationGraphNodeUVE& machine, prism::Vec2 screen) const;
    /// The screen segment a transition is drawn on (ends on the box edges, laned when paired).
    [[nodiscard]] bool TransitionSegmentUVE(const Scene::AnimationGraphNodeUVE& machine, std::size_t index, prism::Vec2& from,
                                            prism::Vec2& to) const;
    [[nodiscard]] std::string StateNameUVE(const Scene::AnimationGraphNodeUVE& machine, std::size_t slot) const;
    [[nodiscard]] std::string StateDetailUVE(const Scene::AnimationGraphNodeUVE& machine, std::size_t slot) const;
    void ShowMenuUVE(prism::Vec2 at, int box, int transition);
    void DeletePickedUVE();
    void OpenStateUVE(std::size_t slot);

    EditorAnimGraphPanelUVE& m_panel;
    DragUVE m_drag = DragUVE::None;
    int m_dragBox = kNoneUVE;
    prism::Vec2 m_dragFrom{};
    prism::Vec2 m_mouse{};
};

} // namespace UVE::Editor
