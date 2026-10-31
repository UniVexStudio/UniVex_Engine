// The Montage: Content "+ Add" -> 3D Body Montage, or any of the body items, asks first and writes
// second. One window lists every body an entity can start as - the player, the character body, the
// things a level is made of - with the object icon each one becomes, and says on the right what the
// picked one is for. Open makes that body's `.uventity`, selects it in Content and opens it in the
// Entity Editor; nothing is written until Open, so trying a few bodies costs nothing.
//
// The list is the content catalogue's (GetContentCatalogueMontageEntriesUVE), not a second menu: a
// body added to "+ Add" is listed here without anyone restating its name or its icon.
#include "uve/editor/editor_uve.h"

#include "uve/editor/editor_content_catalogue_uve.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_object_icons_uve.h"

namespace UVE::Editor {
namespace {

constexpr float kMontageWindowWidthUVE = 760.0F;
constexpr float kMontageWindowHeightUVE = 460.0F;
constexpr float kMontageListWidthUVE = 292.0F;
constexpr float kMontageRowInsetUVE = 8.0F;
constexpr float kMontageIconSizeUVE = 16.0F;
constexpr float kMontageIconGapUVE = 8.0F;

[[nodiscard]] char LowerMontageCharUVE(const char character) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

[[nodiscard]] bool ContainsMontageTextUVE(const std::string_view haystack, const std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    for (std::size_t start = 0U; start + needle.size() <= haystack.size(); ++start) {
        std::size_t matched = 0U;
        while (matched < needle.size() &&
               LowerMontageCharUVE(haystack[start + matched]) == LowerMontageCharUVE(needle[matched])) {
            ++matched;
        }
        if (matched == needle.size()) {
            return true;
        }
    }
    return false;
}

/// Every space-separated word of `query` appears in the body's name or in what the window says about
/// it. An empty query matches every body.
[[nodiscard]] bool DoesMontageEntryMatchUVE(const ContentCatalogueItemUVE& item,
                                            const ContentCatalogueMontageEntryUVE& entry,
                                            std::string_view query) noexcept {
    while (!query.empty()) {
        const std::size_t space = query.find(' ');
        const std::string_view word = query.substr(0U, space);
        if (!word.empty() && !ContainsMontageTextUVE(item.label, word) &&
            !ContainsMontageTextUVE(entry.summary, word) && !ContainsMontageTextUVE(entry.detail, word)) {
            return false;
        }
        if (space == std::string_view::npos) {
            break;
        }
        query.remove_prefix(space + 1U);
    }
    return true;
}

/// The body's root type name ("Character3D"), or empty when the descriptor is missing.
[[nodiscard]] std::string_view MontageRootTypeNameUVE(const ContentCatalogueItemUVE& item) noexcept {
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(GetContentCatalogueIconKindUVE(item));
    return descriptor == nullptr ? std::string_view{} : descriptor->displayName;
}

/// One body: its icon, its name, and the type it becomes, on one row the whole width of the list.
/// The row is the selectable; the picture and the words are drawn into it, so the highlight covers
/// the icon too instead of starting where the text does.
[[nodiscard]] bool DrawMontageRowUVE(const std::uintptr_t iconTexture, const ContentCatalogueItemUVE& item,
                                     const std::string_view typeName, const bool selected) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowHeight = ImGui::GetTextLineHeight() + (style.FramePadding.y * 2.0F);
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const bool clicked =
        ImGui::Selectable("##montage-row", selected, ImGuiSelectableFlags_AllowOverlap, ImVec2{0.0F, rowHeight});

    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 hintColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    // Whole pixels, so the icon texture's texels land on screen pixels and stay sharp.
    const ImVec2 iconMin{std::floor(rowMin.x + kMontageRowInsetUVE),
                         std::floor(rowMin.y + ((rowHeight - kMontageIconSizeUVE) * 0.5F))};
    if (iconTexture != 0U) {
        drawList.AddImage(static_cast<ImTextureID>(iconTexture), iconMin,
                          ImVec2{iconMin.x + kMontageIconSizeUVE, iconMin.y + kMontageIconSizeUVE});
    }
    const float textY = std::floor(rowMin.y + ((rowHeight - ImGui::GetTextLineHeight()) * 0.5F));
    const float labelX = iconMin.x + kMontageIconSizeUVE + kMontageIconGapUVE;
    drawList.AddText(ImVec2{labelX, textY}, textColor, std::string{item.label}.c_str());
    // The type rides at the far edge, dimmed, and only when all of it fits: a type cut to "Cha..."
    // says nothing the name above has not already said.
    if (!typeName.empty()) {
        const std::string hint{typeName};
        const float hintWidth = ImGui::CalcTextSize(hint.c_str()).x;
        const float hintRight = rowMin.x + rowWidth - kMontageRowInsetUVE;
        const float nameRight = labelX + ImGui::CalcTextSize(std::string{item.label}.c_str()).x;
        if (hintRight - hintWidth > nameRight + (style.ItemSpacing.x * 2.0F)) {
            drawList.AddText(ImVec2{hintRight - hintWidth, textY}, hintColor, hint.c_str());
        }
    }
    return clicked;
}

} // namespace

void EditorUVE::OpenMontageUVE(std::filesystem::path directory) {
    MontageStateUVE montage;
    montage.directory = std::move(directory);
    // The first body is picked for the author rather than opening on an empty pane: the window is a
    // choice, and a choice with nothing chosen reads as broken.
    const std::span<const ContentCatalogueMontageEntryUVE> entries = GetContentCatalogueMontageEntriesUVE();
    if (!entries.empty()) {
        montage.pickedItemId = std::string{entries.front().itemId};
    }
    m_montage = std::move(montage);
}

void EditorUVE::CloseMontageUVE() {
    m_montage.reset();
}

bool EditorUVE::OpenMontageBodyUVE(const std::string_view itemId) {
    if (!m_montage.has_value()) {
        return false;
    }
    MontageStateUVE& montage = *m_montage;
    const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(itemId);
    if (item == nullptr || item->action != ContentCatalogueActionUVE::EntityAsset ||
        !IsContentCatalogueMontageBodyUVE(itemId)) {
        montage.status = "That is not a body the Montage offers.";
        montage.statusIsError = true;
        return false;
    }
    if (!IsAuthoringCommandAllowedUVE()) {
        montage.status = "Stop Play to make an entity.";
        montage.statusIsError = true;
        return false;
    }
    // An entity is edited alone: making one only to find the editor busy would leave a file behind
    // with nothing open on it, so the refusal comes before the file.
    if (m_entityEditSession.has_value()) {
        montage.status = "Close " + m_entityEditSession->assetPath.filename().string() +
                         " first: an entity is edited on its own.";
        montage.statusIsError = true;
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(montage.directory, error);
    const std::optional<std::filesystem::path> created = CreateContentCatalogueItemUVE(itemId, montage.directory);
    if (!created.has_value()) {
        montage.status = "Could not create " + std::string{item->label} + ".";
        montage.statusIsError = true;
        return false;
    }

    // Made here, so it counts as recently used and Content shows it selected - the same footing as a
    // body made straight from "+ Add".
    PushContentCreateRecentUVE(m_contentCreateRecent, itemId);
    m_projectFileSnapshotInitialized = false;
    RefreshProjectFileIndexUVE();
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    const std::filesystem::path relative = created->lexically_relative(contentRoot);
    if (!relative.empty() && !relative.generic_string().starts_with("..")) {
        m_selectedProjectFile =
            Asset::ProjectFileEntryUVE{relative, Asset::ProjectFileEntryKindUVE::File, std::nullopt};
        m_selectedAsset.reset();
    }

    const std::filesystem::path body = *created;
    // Closed first: OpenEntityEditorUVE puts the scene aside and draws its own window, and a Montage
    // left standing over it would be asking about a body that already exists.
    m_montage.reset();
    if (!OpenEntityEditorUVE(body)) {
        m_contentStatusMessage = "Made " + body.filename().string() + " but could not open it in the Entity Editor.";
        return false;
    }
    m_contentStatusMessage.clear();
    return true;
}

void EditorUVE::DrawMontageUVE() {
    MontageStateUVE& montage = *m_montage;
    const std::span<const ContentCatalogueMontageEntryUVE> entries = GetContentCatalogueMontageEntriesUVE();
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    const bool allowed = IsAuthoringCommandAllowedUVE();

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    // Its own OS window, like the file picker and the library window: never merged into the main
    // one, never docked, and the same size wherever it opens. TopMost ensures popups never fall behind.
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_TopMost;
    windowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowPos(ImVec2{mainViewport->GetCenter().x, mainViewport->GetCenter().y}, ImGuiCond_FirstUseEver,
                            ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{kMontageWindowWidthUVE, kMontageWindowHeightUVE}, ImGuiCond_FirstUseEver);
    if (montage.focusPending) {
        ImGui::SetNextWindowFocus();
    }
    bool open = true;
    ImGui::Begin("3D Body Montage###body-montage", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
    if (montage.focusPending) {
        montage.focusPending = false;
        if (const ImGuiViewport* const vp = ImGui::GetWindowViewport(); vp != nullptr && vp != mainViewport) {
            if (ImGui::GetPlatformIO().Platform_SetWindowFocus != nullptr) {
                ImGui::GetPlatformIO().Platform_SetWindowFocus(const_cast<ImGuiViewport*>(vp));
            }
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Pick the body this entity starts as. It opens in the Entity Editor, and everything it wears - "
                       "its mesh, its bones, its shape - you add there, one object at a time.");
    ImGui::PopStyleColor();

    // What is picked now, resolved once: the panes, the footer and Enter all read it.
    const ContentCatalogueMontageEntryUVE* pickedEntry = nullptr;
    const ContentCatalogueItemUVE* pickedItem = nullptr;
    for (const ContentCatalogueMontageEntryUVE& entry : entries) {
        if (montage.pickedItemId == entry.itemId) {
            pickedEntry = &entry;
            pickedItem = FindContentCatalogueItemUVE(entry.itemId);
            break;
        }
    }

    char searchBuffer[128];
    std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", montage.search.c_str());
    ImGui::SetNextItemWidth(kMontageListWidthUVE);
    if (ImGui::InputTextWithHint("##montage-search", "Search bodies: player, prop, trigger...", searchBuffer,
                                 sizeof(searchBuffer))) {
        montage.search = searchBuffer;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    // One row of buttons and the status line above them, so the two panes get the rest.
    const float footerHeight = (ImGui::GetFrameHeight() * 2.0F) + (style.ItemSpacing.y * 2.0F);
    const float panesHeight = std::max(ImGui::GetContentRegionAvail().y - footerHeight, 120.0F);

    std::vector<const ContentCatalogueMontageEntryUVE*> listed;
    listed.reserve(entries.size());
    for (const ContentCatalogueMontageEntryUVE& entry : entries) {
        const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(entry.itemId);
        if (item != nullptr && DoesMontageEntryMatchUVE(*item, entry, montage.search)) {
            listed.push_back(&entry);
        }
    }

    // A body opened by a double click closes the window's own state, so it is noted here and done
    // after End: halfway down the list `montage` would be a reference to nothing.
    std::string bodyOpenedByDoubleClick;

    ImGui::BeginChild("##montage-bodies", ImVec2{kMontageListWidthUVE, panesHeight}, ImGuiChildFlags_Borders);
    if (listed.empty()) {
        ImGui::TextDisabled("No body matches \"%s\".", montage.search.c_str());
    }
    for (const ContentCatalogueMontageEntryUVE* const entry : listed) {
        const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(entry->itemId);
        if (item == nullptr) {
            continue;
        }
        ImGui::PushID(entry->itemId.data(), entry->itemId.data() + entry->itemId.size());
        const bool selected = montage.pickedItemId == entry->itemId;
        const std::uintptr_t icon = m_uiAssets.GetObjectIconTextureIdUVE(GetContentCatalogueIconKindUVE(*item));
        if (DrawMontageRowUVE(icon, *item, MontageRootTypeNameUVE(*item), selected)) {
            montage.pickedItemId = std::string{entry->itemId};
            montage.status.clear();
            montage.statusIsError = false;
            // Clicking the body already picked, twice, is the same answer as pressing Open.
            if (selected && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                bodyOpenedByDoubleClick = std::string{entry->itemId};
            }
        }
        ImGui::PopID();
        if (!bodyOpenedByDoubleClick.empty()) {
            break;
        }
    }
    // Arrows walk the list without a mouse; Enter takes the body the panes are describing.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !montage.pickedItemId.empty()) {
        // Over the bodies the search left, so a filtered list never steps onto a hidden one.
        const auto step = [&montage, &listed](const int direction) {
            const auto found = std::find_if(listed.begin(), listed.end(), [&montage](const auto* const entry) {
                return montage.pickedItemId == entry->itemId;
            });
            if (found == listed.end()) {
                return;
            }
            const auto index = static_cast<std::ptrdiff_t>(std::distance(listed.begin(), found));
            const auto count = static_cast<std::ptrdiff_t>(listed.size());
            const std::ptrdiff_t moved = (index + (direction > 0 ? 1 : count - 1)) % count;
            montage.pickedItemId = std::string{listed[static_cast<std::size_t>(moved)]->itemId};
        };
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            step(1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            step(-1);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##montage-detail", ImVec2{0.0F, panesHeight}, ImGuiChildFlags_Borders);
    if (pickedItem == nullptr || pickedEntry == nullptr) {
        ImGui::TextDisabled("Pick a body on the left to see what it is.");
    } else {
        const std::string name{pickedItem->label};
        ImGui::TextUnformatted(name.c_str());
        const std::string typeName{MontageRootTypeNameUVE(*pickedItem)};
        if (!typeName.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", typeName.c_str());
        }
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(std::string{pickedEntry->summary}.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", std::string{pickedEntry->detail}.c_str());
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("MAKES");
        // The real name, collision suffix and all: pressing Open twice gets "Character" then
        // "Character 2", and the pane should have said so before the second one.
        const std::string fileName =
            MakeUniqueContentPathUVE(montage.directory, pickedItem->label, ".uventity").filename().string();
        ImGui::TextUnformatted(fileName.c_str());
        const std::filesystem::path where = montage.directory.lexically_relative(contentRoot);
        ImGui::TextDisabled("in Content%s", where.empty() ? "" : ("/" + where.generic_string()).c_str());

        ImGui::Spacing();
        ImGui::TextDisabled("INSIDE");
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextWrapped("Nothing yet. The body opens on its own, and what it wears is yours to add.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    // The footer says what is picked, then takes the answer.
    const bool canOpen = allowed && pickedItem != nullptr;
    if (!montage.status.empty()) {
        if (montage.statusIsError) {
            ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.45F, 1.0F}, "%s", montage.status.c_str());
        } else {
            ImGui::TextDisabled("%s", montage.status.c_str());
        }
    } else if (!allowed) {
        ImGui::TextDisabled("Stop Play to make an entity.");
    } else if (pickedItem != nullptr) {
        ImGui::TextDisabled("You selected %s.", std::string{pickedItem->label}.c_str());
    } else {
        ImGui::TextDisabled("You selected nothing.");
    }

    constexpr float kOpenButtonWidthUVE = 128.0F;
    constexpr float kCancelButtonWidthUVE = 96.0F;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - kOpenButtonWidthUVE -
                         kCancelButtonWidthUVE - style.ItemSpacing.x);
    bool confirmed = false;
    ImGui::BeginDisabled(!canOpen);
    if (ImGui::Button("Open", ImVec2{kOpenButtonWidthUVE, ImGui::GetFrameHeight()})) {
        confirmed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel", ImVec2{kCancelButtonWidthUVE, ImGui::GetFrameHeight()});
    if (canOpen && pickedItem != nullptr && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        confirmed = true;
    }
    const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape);

    // The body to make, taken before End: Open closes this window's state, so nothing below may
    // touch `montage` again.
    const std::string bodyToOpen = !bodyOpenedByDoubleClick.empty() ? bodyOpenedByDoubleClick
                                    : (confirmed && pickedEntry != nullptr) ? std::string{pickedEntry->itemId}
                                                                           : std::string{};

    ImGui::End();
    if (!bodyToOpen.empty()) {
        static_cast<void>(OpenMontageBodyUVE(bodyToOpen));
        return;
    }
    if (!open || cancelled || escaped) {
        CloseMontageUVE();
    }
}

} // namespace UVE::Editor
