// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "input_action.h"

#include "prism/Widget.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace prism {
class Box;
class Button;
class Label;
class TextInput;
class TreeView;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;
class EditorHostUVE;

/// The Input Map, drawn with PRISM in a window of its own: the project's actions on the left (add,
/// duplicate, delete), the picked one on the right - its name, Button or Axis, and its bindings
/// (an axis has a positive and a negative side). A binding is added by pressing it (a key in this
/// window, a gamepad button or stick anywhere) or picked from a list of every input; one another
/// action also uses is drawn in the warning colour. Changes reach Play at once; Save (and closing
/// the window) writes the project's input file.
class EditorInputMapWindowUVE final {
public:
    EditorInputMapWindowUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorInputMapWindowUVE();
    EditorInputMapWindowUVE(const EditorInputMapWindowUVE&) = delete;
    EditorInputMapWindowUVE& operator=(const EditorInputMapWindowUVE&) = delete;

    void FrameUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

    /// The binding a key pressed while listening makes (the capture widget calls it).
    void CapturedUVE(const Input::InputBindingUVE& binding);
    void CancelListeningUVE();
    [[nodiscard]] bool ListeningUVE() const noexcept;

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void RebuildListUVE();
    void RebuildDetailUVE();
    void CommitUVE(std::vector<Input::InputActionUVE> actions);
    void ListenUVE(bool negative, std::optional<std::size_t> replace);
    void ShowChooseMenuUVE(bool negative, prism::Rect anchor);
    [[nodiscard]] prism::Ref<prism::Widget> BindingSectionUVE(const std::vector<Input::InputActionUVE>& actions, std::size_t index,
                                                              bool negative, const char* title);

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    int m_window = 0;
    bool m_closePending = false;
    bool m_listDirty = false;
    bool m_detailDirty = false;
    std::vector<std::size_t> m_shown;   ///< list row -> action index
    std::shared_ptr<prism::TextInput> m_filter;
    std::shared_ptr<prism::TreeView> m_list;
    std::shared_ptr<prism::Box> m_detail;
    std::shared_ptr<prism::Widget> m_capture;
    std::shared_ptr<prism::Label> m_note;
    std::shared_ptr<prism::Button> m_save;
};

} // namespace UVE::Editor
