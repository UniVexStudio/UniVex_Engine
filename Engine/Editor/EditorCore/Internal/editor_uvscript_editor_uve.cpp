// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The UVScript text editor: the Scripting workspace while a `.uvs` file is open. The text is
// checked against the node it belongs to on every edit, so a mistake shows up while typing - by
// line and column - rather than when the game runs. Saving writes the file; the running engine
// notices the new text and restarts that script on its own.

#include "uve/editor/editor_uve.h"

#include "editor_chrome_layout_uve.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "uve/core/uvscript_node_host_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_parser_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool IsUVScriptPathUVE(const std::string_view path) noexcept { return path.ends_with(".uvs"); }

/// Every problem in the document's text. Checked against the node while it exists - its
/// components decide which properties are in scope - and only parsed once it is gone.
void RecheckUVScriptUVE(EditorUVE::UVScriptDocumentUVE& document, Scene::IEntityManagerUVE& entityManager) {
    if (entityManager.IsAliveUVE(document.entity)) {
        Core::UVScriptNodeHostUVE host(entityManager, nullptr, document.entity);
        document.diagnostics = UVScript::CompileUVScriptSourceUVE(document.text, host).diagnostics;
    } else {
        document.diagnostics = UVScript::ParseUVScriptUVE(document.text).diagnostics;
    }
}

/// What the text box's callback works on: the text it grows, and a pending caret move.
struct TextBoxStateUVE final {
    std::string* text = nullptr;
    std::uint32_t* jumpLine = nullptr;
};

/// Byte offset of the start of 1-based `line` in `text` (its end when there are fewer lines).
[[nodiscard]] int LineStartOffsetUVE(const std::string_view text, const std::uint32_t line) noexcept {
    std::size_t offset = 0U;
    for (std::uint32_t current = 1U; current < line; ++current) {
        const std::size_t newline = text.find('\n', offset);
        if (newline == std::string_view::npos) {
            return static_cast<int>(text.size());
        }
        offset = newline + 1U;
    }
    return static_cast<int>(offset);
}

/// ImGui grows a std::string-backed text box through this callback; it also places the caret
/// when a problem was clicked.
int TextBoxCallbackUVE(ImGuiInputTextCallbackData* const data) {
    auto* const state = static_cast<TextBoxStateUVE*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        state->text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = state->text->data();
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && *state->jumpLine != 0U) {
        const int offset = LineStartOffsetUVE(std::string_view{data->Buf, static_cast<std::size_t>(data->BufTextLen)},
                                              *state->jumpLine);
        data->CursorPos = offset;
        data->SelectionStart = offset;
        data->SelectionEnd = offset;
        *state->jumpLine = 0U;
    }
    return 0;
}

} // namespace

bool EditorUVE::OpenUVScriptForEntityUVE(const Scene::EntityUVE entity) {
    if (m_state != EditorStateUVE::Running || m_services == nullptr) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return false;
    }
    const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
    if (!IsUVScriptPathUVE(path)) {
        return false;
    }
    // The same file already open keeps its unsaved text; only the node it is checked against moves.
    if (!m_openUVScript.has_value() || m_openUVScript->path != path) {
        UVScriptDocumentUVE document;
        document.path = path;
        document.text = ReadProjectTextFileUVE(path).value_or(std::string{});
        document.savedText = document.text;
        m_openUVScript = std::move(document);
    }
    m_openUVScript->entity = entity;
    RecheckUVScriptUVE(*m_openUVScript, entityManager);
    m_activeWorkspace = EditorWorkspaceUVE::Scripting;
    return true;
}

const std::optional<EditorUVE::UVScriptDocumentUVE>& EditorUVE::GetOpenUVScriptUVE() const noexcept {
    return m_openUVScript;
}

void EditorUVE::SetOpenUVScriptTextUVE(std::string text) {
    if (!m_openUVScript.has_value() || m_services == nullptr) {
        return;
    }
    m_openUVScript->text = std::move(text);
    RecheckUVScriptUVE(*m_openUVScript, m_services->GetEntityManagerUVE());
}

bool EditorUVE::SaveOpenUVScriptUVE() {
    if (!m_openUVScript.has_value() || !WriteProjectTextFileUVE(m_openUVScript->path, m_openUVScript->text)) {
        return false;
    }
    m_openUVScript->savedText = m_openUVScript->text;
    return true;
}

void EditorUVE::CloseOpenUVScriptUVE() {
    m_openUVScript.reset();
    if (m_activeWorkspace == EditorWorkspaceUVE::Scripting) {
        m_activeWorkspace = EditorWorkspaceUVE::Library;
    }
}

void EditorUVE::DrawUVScriptEditorUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + kEditorTopChromeHeightUVE},
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2{mainViewport->WorkSize.x, std::max(120.0F, mainViewport->WorkSize.y - kEditorTopChromeHeightUVE)},
        ImGuiCond_Always);
    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("UVScript Editor##uve", nullptr, windowFlags)) {
        ImGui::End();
        return;
    }
    const bool close = DrawUVScriptEditorBodyUVE(true);
    ImGui::End();
    if (close) {
        CloseOpenUVScriptUVE();
    }
}

bool EditorUVE::DrawUVScriptEditorBodyUVE(const bool offerClose) {
    UVScriptDocumentUVE& document = *m_openUVScript;
    bool close = false;

    // Toolbar: the file, whether it has unsaved edits, and what to do with it.
    const std::string fileName = std::filesystem::path{document.path}.filename().string();
    ImGui::TextUnformatted(fileName.c_str());
    if (document.IsDirtyUVE()) {
        ImGui::SameLine(0.0F, 4.0F);
        ImGui::TextDisabled("(unsaved)");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", document.path.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.IsDirtyUVE());
    const bool saveShortcut = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S);
    if (ImGui::SmallButton("Save") || (saveShortcut && document.IsDirtyUVE())) {
        static_cast<void>(SaveOpenUVScriptUVE());
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Write the file (Ctrl+S). A game that is running restarts this script.");
    }
    if (offerClose) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Close")) {
            close = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Back to the scene. Unsaved text is dropped.");
        }
    }
    ImGui::Separator();

    // The problems list takes what it needs at the bottom, up to a third of the window.
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float listHeight = std::min(ImGui::GetContentRegionAvail().y / 3.0F,
                                      lineHeight * static_cast<float>(std::max<std::size_t>(document.diagnostics.size(), 1U) + 1U));
    std::string text = document.text;
    TextBoxStateUVE textBox{&text, &m_uvscriptJumpLine};
    if (m_uvscriptJumpLine != 0U) {
        ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::InputTextMultiline("##uvs-text", text.data(), text.capacity() + 1U,
                                  ImVec2{-FLT_MIN, ImGui::GetContentRegionAvail().y - listHeight - 6.0F},
                                  ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize |
                                      ImGuiInputTextFlags_CallbackAlways,
                                  TextBoxCallbackUVE, &textBox)) {
        SetOpenUVScriptTextUVE(std::move(text));
    }

    if (ImGui::BeginChild("##uvs-problems", ImVec2{0.0F, listHeight}, ImGuiChildFlags_Borders)) {
        if (document.diagnostics.empty()) {
            ImGui::TextColored(ImVec4{0.45F, 0.80F, 0.55F, 1.0F}, "No problems.");
        }
        for (const UVScript::DiagnosticUVE& diagnostic : document.diagnostics) {
            ImGui::PushID(&diagnostic);
            const std::string place = std::to_string(diagnostic.at.line) + ":" + std::to_string(diagnostic.at.column);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.62F, 0.35F, 1.0F});
            if (ImGui::Selectable(place.c_str(), false, ImGuiSelectableFlags_None,
                                  ImVec2{ImGui::CalcTextSize(place.c_str()).x, 0.0F})) {
                m_uvscriptJumpLine = std::max<std::uint32_t>(diagnostic.at.line, 1U);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Go to line %u", diagnostic.at.line);
            }
            ImGui::SameLine();
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    return close;
}

} // namespace UVE::Editor
