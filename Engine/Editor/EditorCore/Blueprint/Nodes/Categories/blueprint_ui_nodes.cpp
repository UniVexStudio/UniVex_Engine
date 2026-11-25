// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_ui_nodes.h"

#include <utility>

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterUiBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Role = BlueprintNodeBuilder::Role;
    using Pin = BlueprintNodeBuilder::Pin;
    using NodeType = BlueprintNodeBuilder::NodeType;
    BlueprintNodeBuilder b{out};

    // Widget events
    b.WidgetEvent("ui_on_clicked", "On Button Clicked", "ui_clicked", "Button", {Pin{"Button", T::String, ""}}, "press click onclicked widget",
                  "A button of the widget was clicked (name it, or leave it empty for any)");
    b.WidgetEvent("ui_on_changed", "On Value Changed", "ui_changed", "Widget", {Pin{"Widget", T::String, ""}}, "slider check box toggle",
                  "A slider moved or a check box was ticked");
    b.WidgetEvent("ui_on_committed", "On Text Committed", "ui_committed", "Text Box", {Pin{"Text Box", T::String, ""}, Pin{"Text", T::String, ""}},
                  "enter submit input", "A text box was submitted with Enter");
    b.WidgetEvent("ui_on_hovered", "On Hovered", "ui_hovered", "Button", {Pin{"Button", T::String, ""}}, "hover mouse enter over",
                  "The pointer came onto a button of the widget");
    b.WidgetEvent("ui_on_unhovered", "On Unhovered", "ui_unhovered", "Button", {Pin{"Button", T::String, ""}}, "hover mouse leave exit",
                  "The pointer left a button of the widget");
    {
        NodeType construct;
        construct.key = "ui_on_construct";
        construct.title = "Event Construct";
        construct.category = "User Interface|Events";
        construct.keywords = "widget created ready begin";
        construct.role = Role::Event;
        construct.event = "ready";
        construct.outputs.push_back(BlueprintNodeBuilder::Exec());
        construct.tooltip = "Once, when the widget is made";
        b.Add(std::move(construct));
    }

    // Widget controls, combo box, screens, layout, animation, viewport
    b.Action("ui_set_text", "Set Text", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Text", T::String, "Hello"}},
             "ui.set_text({1}, {2})", "label caption", "A Text or Text Box of the widget, by name");
    b.Pure("ui_get_text", "Get Text", "User Interface", {Pin{"Widget", T::String, "Text"}}, {Pin{"Text", T::String, ""}},
           {"ui.get_text({0})"}, "label caption read");
    b.Action("ui_set_visibility", "Set Visibility", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Visible", T::Bool, "true"}},
             "ui.set_visible({1}, {2})", "hide show collapse");
    b.Action("ui_set_color", "Set Color", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Color", T::Vector, "1,1,1"}},
             "ui.set_color({1}, {2})", "tint colour", "A Text's color, a Button's color, an Image's tint");
    b.Action("ui_set_percent", "Set Percent", "User Interface", {Pin{"Widget", T::String, "ProgressBar"}, Pin{"Percent", T::Float, "0.5"}},
             "ui.set_progress({1}, {2})", "progress bar health fill slider value");
    b.Pure("ui_get_value", "Get Value", "User Interface", {Pin{"Widget", T::String, "Slider"}}, {Pin{"Value", T::Float, ""}},
           {"ui.get_value({0})"}, "slider progress percent");
    b.Pure("ui_is_checked", "Is Checked", "User Interface", {Pin{"Widget", T::String, "CheckBox"}}, {Pin{"Checked", T::Bool, ""}},
           {"ui.is_checked({0})"}, "check box ticked toggle");
    b.Action("ui_set_checked", "Set Is Checked", "User Interface", {Pin{"Widget", T::String, "CheckBox"}, Pin{"Checked", T::Bool, "true"}},
             "ui.set_checked({1}, {2})", "check box tick toggle");
    b.Pure("ui_get_selected_option", "Get Selected Option", "User Interface|Combo Box", {Pin{"Widget", T::String, "ComboBox"}},
           {Pin{"Option", T::String, ""}}, {"ui.get_selected_option({0})"}, "dropdown combo choice");
    b.Pure("ui_get_selected_index", "Get Selected Index", "User Interface|Combo Box", {Pin{"Widget", T::String, "ComboBox"}},
           {Pin{"Index", T::Int, ""}}, {"ui.get_selected_index({0})"}, "dropdown combo choice");
    b.Action("ui_set_selected_index", "Set Selected Index", "User Interface|Combo Box",
             {Pin{"Widget", T::String, "ComboBox"}, Pin{"Index", T::Int, "0"}}, "ui.set_selected_index({1}, {2})", "dropdown combo choice");
    b.Action("ui_add_option", "Add Option", "User Interface|Combo Box", {Pin{"Widget", T::String, "ComboBox"}, Pin{"Option", T::String, "Option"}},
             "ui.add_option({1}, {2})", "dropdown combo item");
    b.Action("ui_show_screen", "Show Screen", "User Interface", {Pin{"Screen", T::String, "Main"}}, "ui.show_screen({1})",
             "page menu switcher designer tab", "Shows one of the widget's screens (its Designer tabs) and hides the others");
    b.Action("ui_show_mouse_cursor", "Set Show Mouse Cursor", "User Interface|Input", {Pin{"Show", T::Bool, "true"}}, "ui.show_cursor({1})",
             "pointer mouse cursor visible input mode ui", "Shows and frees the pointer so the UI can be clicked; false hides it for the game again");
    b.Action("ui_remove", "Remove from Parent", "User Interface", {}, "ui.remove()", "close hide widget",
             "Takes this widget off the screen");
    b.ValuedAction("ui_create_widget", "Create Widget", "User Interface", {Pin{"Class", T::String, ""}}, {Pin{"Return Value", T::Object, ""}},
                   {"ui.create({1})"}, "spawn new widget blueprint hud menu",
                   "Makes a Widget Blueprint's widgets (its .uvwbp file); Add to Viewport shows them");
    b.Action("ui_set_position", "Set Render Position", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}, Pin{"Position", T::Vector, "0,0,0"}},
             "ui.set_position({1}, {2})", "move translate offset slot", "Its top-left corner, in design pixels (X, Y)");
    b.Action("ui_set_size", "Set Size", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}, Pin{"Size", T::Vector, "120,32,0"}},
             "ui.set_size({1}, {2})", "width height resize", "Its width and height, in design pixels (X, Y)");
    b.Pure("ui_get_position", "Get Render Position", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}}, {Pin{"Position", T::Vector, ""}},
           {"ui.get_position({0})"}, "where location");
    b.Pure("ui_get_size", "Get Size", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}}, {Pin{"Size", T::Vector, ""}},
           {"ui.get_size({0})"}, "width height");
    b.Action("ui_set_opacity", "Set Opacity", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Opacity", T::Float, "1.0"}},
             "ui.set_opacity({1}, {2})", "alpha fade transparency", "A Text's or an Image's opacity (0 to 1)");
    b.Action("ui_set_font_size", "Set Font Size", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Size", T::Float, "24.0"}},
             "ui.set_font_size({1}, {2})", "text size bigger smaller");
    b.Action("ui_play_animation", "Play Widget Animation", "User Interface|Animation", {Pin{"Widget", T::String, "Panel"}},
             "ui.play_tween({1})", "tween animate slide fade", "Plays the widget's Tween from its start");
    b.Action("ui_stop_animation", "Stop Widget Animation", "User Interface|Animation", {Pin{"Widget", T::String, "Panel"}},
             "ui.stop_tween({1})", "tween pause");
    b.Pure("ui_is_hovered", "Is Hovered", "User Interface", {Pin{"Widget", T::String, "Button"}}, {Pin{"Hovered", T::Bool, ""}},
           {"ui.is_hovered({0})"}, "mouse over");
    b.Action("ui_add_to_viewport", "Add to Viewport", "User Interface", {Pin{"Widget", T::Object, ""}, Pin{"Z Order", T::Int, "0"}},
             "ui.add_to_viewport({1}, {2})", "show screen hud", "Shows a widget Create Widget made");
}

} // namespace UVE::Blueprint::Nodes
