// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_graph_component.h"

#include "prism/Widget.h"

#include <cstdint>
#include <optional>
#include <string>

namespace UVE::Editor {

class EditorAnimGraphPanel;

/// A State Machine node opened from the Animation Graph: its states as boxes (what each plays, the
/// one running lit), Entry and Any, and the transitions as arrows - two lanes when two states lead to
/// each other, dimmed when off, flashing when taken.
///
/// Click picks a state or an arrow (the side panel edits it); drag a state, Entry or Any to move
/// it; Shift + drag from a state to another makes a transition (from Any: from any state; from
/// Entry: where the machine starts); double-click empty space adds a state, a state opens what it
/// plays; right-click for the menu; Delete removes what is picked; the wheel zooms, a drag on empty
/// space pans, F frames, Esc goes back to the tree.
class StateMachineView final : public prism::Widget {
public:
    explicit StateMachineView(EditorAnimGraphPanel& panel) : m_panel(panel) {}

    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onMouseWheel(const prism::MouseEvent& e) override;
    bool onKeyDown(const prism::KeyEvent& e) override;
    void onMouseLeave() override;
    prism::Cursor cursor() const override;
    bool focusable() const override { return true; }

    /// Starts a transition from `slot` that follows the mouse until a state is clicked.
    void BeginTransition(int slot);
    /// Asks for a state's new name in place.
    void RenameState(std::size_t slot);

    /// Fits every box in view.
    void Frame();

private:
    void CancelDrag();
    static constexpr int kEntry = -1;
    static constexpr int kAny = -2;
    static constexpr int kNone = -3;
    enum class Drag : std::uint8_t { None, Pan, Move, Link };

    [[nodiscard]] const Scene::AnimationGraphNode* Machine() const;
    [[nodiscard]] std::size_t MachineIndex() const;
    [[nodiscard]] prism::Vec2 ToScreen(Math::Vector2 world) const;
    [[nodiscard]] Math::Vector2 ToWorld(prism::Vec2 screen) const;
    /// A box in world units: a state slot, kEntry or kAny; with the live drag applied.
    [[nodiscard]] prism::Rect Box(const Scene::AnimationGraphNode& machine, int box) const;
    [[nodiscard]] prism::Rect ScreenBox(const Scene::AnimationGraphNode& machine, int box) const;
    [[nodiscard]] int BoxAt(const Scene::AnimationGraphNode& machine, prism::Vec2 screen) const;
    [[nodiscard]] int TransitionAt(const Scene::AnimationGraphNode& machine, prism::Vec2 screen) const;
    /// The screen segment a transition is drawn on (ends on the box edges, laned when paired).
    [[nodiscard]] bool TransitionSegment(const Scene::AnimationGraphNode& machine, std::size_t index, prism::Vec2& from,
                                            prism::Vec2& to) const;
    [[nodiscard]] std::string StateName(const Scene::AnimationGraphNode& machine, std::size_t slot) const;
    [[nodiscard]] std::string StateDetail(const Scene::AnimationGraphNode& machine, std::size_t slot) const;
    void ShowMenu(prism::Vec2 at, int box, int transition);
    /// Near a state's border (where a drag makes a transition) rather than inside it.
    [[nodiscard]] bool OnEdge(const Scene::AnimationGraphNode& machine, int box, prism::Vec2 screen) const;
    void AddState(Math::Vector2 world, Asset::AssetGuid clip);
    void DeletePicked();
    void OpenState(std::size_t slot);

    EditorAnimGraphPanel& m_panel;
    Drag m_drag = Drag::None;
    int m_dragBox = kNone;
    prism::Vec2 m_dragFrom{};
    prism::Vec2 m_mouse{};
    int m_hoverBox = -3;
    bool m_hoverEdge = false;
    /// A transition started from the menu: it follows the mouse until the next click.
    bool m_linkPending = false;
};

} // namespace UVE::Editor
