// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Details rows some objects need beyond their properties: a Camera3D's Preview / Screen, a
// Skeleton3D's source model and its bones, a BoneAttachment3D's bone.
#include "editor_prism_details.h"

#include "editor.h"

#include "bone_attachment_3d.h"
#include "camera_component.h"
#include "skeleton_3d.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr int kBoneIconSizeUVE = 16;

/// A bone, drawn once: a shaft with a pair of knuckles at each end, bone-coloured, antialiased.
[[nodiscard]] const std::uint32_t* BoneIconUVE() {
    static const std::array<std::uint32_t, kBoneIconSizeUVE * kBoneIconSizeUVE> kIcon = [] {
        std::array<std::uint32_t, kBoneIconSizeUVE * kBoneIconSizeUVE> pixels{};
        const float ax = 4.0F, ay = 11.5F, bx = 11.5F, by = 4.0F;
        const float dx = bx - ax, dy = by - ay;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / len, ny = dx / len;   // perpendicular
        const auto segment = [&](const float px, const float py) {
            const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (len * len), 0.0F, 1.0F);
            const float cx = ax + dx * t, cy = ay + dy * t;
            return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        };
        const auto circle = [](const float px, const float py, const float cx, const float cy) {
            return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        };
        const float k = 1.55F;
        for (int y = 0; y < kBoneIconSizeUVE; ++y) {
            for (int x = 0; x < kBoneIconSizeUVE; ++x) {
                const float px = static_cast<float>(x) + 0.5F, py = static_cast<float>(y) + 0.5F;
                float d = segment(px, py) - 1.35F;
                d = std::min(d, circle(px, py, ax + nx * k - dx / len * 0.6F, ay + ny * k - dy / len * 0.6F) - 1.75F);
                d = std::min(d, circle(px, py, ax - nx * k - dx / len * 0.6F, ay - ny * k - dy / len * 0.6F) - 1.75F);
                d = std::min(d, circle(px, py, bx + nx * k + dx / len * 0.6F, by + ny * k + dy / len * 0.6F) - 1.75F);
                d = std::min(d, circle(px, py, bx - nx * k + dx / len * 0.6F, by - ny * k + dy / len * 0.6F) - 1.75F);
                const float coverage = std::clamp(0.5F - d, 0.0F, 1.0F);
                const auto alpha = static_cast<std::uint32_t>(std::lround(coverage * 255.0F));
                pixels[static_cast<std::size_t>(y * kBoneIconSizeUVE + x)] = (alpha << 24U) | 0x00EDE3C9U;
            }
        }
        return pixels;
    }();
    return kIcon.data();
}

[[nodiscard]] std::string Vector3TextUVE(const Math::Vector3UVE& v, const char* format = "%.3f   %.3f   %.3f") {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, format, static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
    return buffer;
}

[[nodiscard]] prism::Ref<prism::Label> ValueLabelUVE(const std::string& text) {
    auto label = prism::make<prism::Label>(text);
    label->setColor(prism::ThemeColor(&prism::Theme::textBright));
    return label;
}

} // namespace

// ---------------------------------------------------------------------------------------- camera

void EditorPrismDetailsUVE::AddCameraPreviewUVE(const Scene::EntityUVE entity) {
    if (!MatchesFilterUVE("Camera") && !MatchesFilterUVE("Preview") && !MatchesFilterUVE("Screen")) {
        return;
    }
    const int category = m_grid->addCategory("Camera3D / Preview", true);

    // Preview: the viewport looks through this camera (it flies in, and back out when turned off).
    // Screen: what the camera sees, live, in a corner of the viewport while it is selected.
    auto preview = prism::make<prism::Button>("Preview", prism::Icon::None, prism::ButtonStyle::Default);
    preview->setCheckable(true);
    preview->setTooltip("Look through this camera in the viewport");
    preview->onToggle = [this, entity](const bool on) {
        if (on) {
            m_editor.SetPreviewCameraUVE(entity);
        } else {
            m_editor.ClearPreviewCameraUVE();
        }
    };
    auto screen = prism::make<prism::Button>("Screen", prism::Icon::None, prism::ButtonStyle::Default);
    screen->setCheckable(true);
    screen->setChecked(m_cameraScreenOpen);
    screen->setTooltip("Show what this camera sees in a corner of the viewport");
    screen->onToggle = [this](const bool on) { m_cameraScreenOpen = on; };
    auto buttons = prism::hbox(6.0F);
    buttons->add(preview, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    buttons->add(screen, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto row = std::make_shared<RowUVE>();
    std::weak_ptr<prism::Button> weakPreview = preview;
    row->sync = [this, entity, weakPreview](const void*) {
        if (auto b = weakPreview.lock()) {
            const bool on = m_editor.GetPreviewCameraUVE() == entity;
            if (b->checked() != on) {
                b->setChecked(on);
            }
        }
    };
    row->editor = buttons.get();
    row->sync(nullptr);
    m_rows.push_back(row);
    m_grid->addRow(category, "View", buttons, "Preview looks through it in the viewport; Screen shows what it sees in the viewport's corner.");

}


// -------------------------------------------------------------------------------------- skeleton

void EditorPrismDetailsUVE::AddSkeletonSourceUVE(const int category, const EntryUVE& entry, const PropertyUVE& property,
                                                 const void* instance, const bool writable) {
    const auto* skeleton = static_cast<const Scene::Skeleton3DComponentUVE*>(instance);
    const std::string current = skeleton != nullptr ? skeleton->skeletonAssetPath : std::string();
    auto button = prism::make<prism::Button>(current.empty() ? std::string("Empty")
                                                             : std::filesystem::path{current}.stem().string(),
                                             prism::Icon::None, prism::ButtonStyle::Default);
    button->setTooltip(property.tooltip);
    std::weak_ptr<prism::Button> weak = button;
    button->onClick = [this, weak, current] {
        // Empty, the project's rigged models, then the usual Load / Import, and Reload Bones.
        const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        const auto bind = [this](const std::filesystem::path& relative) {
            if (!m_editor.BindSelectedSkeletonSourceUVE(relative) && !m_editor.m_skeletonSourceStatus.empty()) {
                m_editor.m_contentStatusMessage = m_editor.m_skeletonSourceStatus;
            }
        };
        const auto bindAbsolute = [this, bind](const std::filesystem::path& absolute) {
            const std::filesystem::path root = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
            const std::filesystem::path relative = absolute.lexically_relative(root);
            if (relative.empty() || *relative.begin() == "..") {
                m_editor.m_contentStatusMessage = "A skeleton's model must be in the project: use Import to bring it in.";
                return;
            }
            m_editor.RefreshProjectFileIndexUVE();
            bind(relative);
        };
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Empty", [bind] { bind({}); }));
        items.push_back(prism::MenuItem::Sep());
        bool any = false;
        for (const Asset::ProjectFileEntryUVE& file : snapshot.entries) {
            const EditorModelSourceInfoUVE* const source = m_editor.FindModelSourceInfoUVE(file.relativePath);
            if (file.kind == Asset::ProjectFileEntryKindUVE::Directory || source == nullptr || !source->hasSkeleton) {
                continue;
            }
            any = true;
            const std::string path = file.relativePath.generic_string();
            items.push_back(prism::MenuItem::Check(path, path == current, [bind, rel = file.relativePath] { bind(rel); }));
        }
        if (!any) {
            items.push_back(prism::MenuItem::Item("No rigged models in this project yet", {}).disabled());
        }
        items.push_back(prism::MenuItem::Sep());
        const std::vector<std::string> kinds{".glb", ".gltf", ".fbx"};
        items.push_back(prism::MenuItem::Item("Load...", [this, kinds, bindAbsolute] {
            FilePickerRequestUVE request;
            request.title = "Load Skeleton Source";
            request.extensions = kinds;
            request.onPick = bindAbsolute;
            m_editor.OpenFilePickerUVE(std::move(request));
        }));
        items.push_back(prism::MenuItem::Item("Import...", [this, kinds, bindAbsolute] {
            FilePickerRequestUVE request;
            request.title = "Import Skeleton Source";
            request.extensions = kinds;
            request.quickImport = true;
            request.onPick = bindAbsolute;
            m_editor.OpenFilePickerUVE(std::move(request));
        }));
        prism::MenuItem reload = prism::MenuItem::Item("Reload Bones", [bind, current] { bind(current); });
        reload.enabled = !current.empty();
        items.push_back(std::move(reload));
        if (auto b = weak.lock()) {
            prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
        }
    };
    button->setEnabled(writable);
    m_grid->addRow(category, property.displayName.empty() ? property.name : property.displayName, button, property.tooltip);
    static_cast<void>(entry);
}

void EditorPrismDetailsUVE::AddSkeletonBonesUVE(const int category, const void* instance) {
    const auto* skeleton = static_cast<const Scene::Skeleton3DComponentUVE*>(instance);
    if (skeleton == nullptr) {
        return;
    }
    if (skeleton->bones.empty()) {
        auto none = prism::make<prism::Label>("No bones: pick a Source model with an armature.", prism::FontRole::Small);
        none->setColor(prism::ThemeColor(&prism::Theme::textDim));
        m_grid->addRow(category, "Bones", none);
        return;
    }
    // The armature as a tree, each bone with the bone mark; picking one shows its rest pose.
    auto tree = prism::make<prism::TreeView>();
    tree->setShowGuides(true);
    std::vector<prism::TreeNode*> nodes(skeleton->bones.size(), nullptr);
    const auto bones = skeleton->bones;
    for (std::size_t pass = 0U; pass < bones.size(); ++pass) {
        bool added = false;
        for (std::size_t i = 0U; i < bones.size(); ++i) {
            if (nodes[i] != nullptr) {
                continue;
            }
            const std::int32_t parent = bones[i].parentIndex;
            const bool root = parent < 0 || static_cast<std::size_t>(parent) >= bones.size();
            if (!root && nodes[static_cast<std::size_t>(parent)] == nullptr) {
                continue;
            }
            prism::TreeNode* node = tree->addNode(root ? nullptr : nodes[static_cast<std::size_t>(parent)], bones[i].name,
                                                  prism::Icon::None, static_cast<int>(i) + 1);
            node->image = BoneIconUVE();
            node->imageSize = kBoneIconSizeUVE;
            nodes[i] = node;
            added = true;
        }
        if (!added) {
            break;
        }
    }
    tree->refresh();
    auto box = prism::make<prism::SizeBox>();
    box->setHeightOverride(std::min(240.0F, 22.0F * static_cast<float>(bones.size()) + 10.0F));
    box->setContent(tree);
    m_grid->addRow(category, "Bones (" + std::to_string(bones.size()) + ")", box);

    auto parent = ValueLabelUVE("-");
    auto position = ValueLabelUVE("-");
    auto rotation = ValueLabelUVE("-");
    auto scale = ValueLabelUVE("-");
    tree->onSelect = [bones, parent, position, rotation, scale](const std::vector<prism::TreeNode*>& selected) {
        if (selected.empty() || selected.front()->id <= 0 || static_cast<std::size_t>(selected.front()->id) > bones.size()) {
            return;
        }
        const Scene::SkeletonBoneUVE& bone = bones[static_cast<std::size_t>(selected.front()->id - 1)];
        parent->setText(bone.parentIndex < 0 || static_cast<std::size_t>(bone.parentIndex) >= bones.size()
                            ? std::string("Root")
                            : bones[static_cast<std::size_t>(bone.parentIndex)].name);
        position->setText(Vector3TextUVE(bone.localPosition));
        Math::Vector3UVE euler{};
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
        rotation->setText(Math::TryToEulerUVE(bone.localRotation, euler)
                              ? Vector3TextUVE(Math::Vector3UVE{euler.x * kDegrees, euler.y * kDegrees, euler.z * kDegrees},
                                               "%.1f°   %.1f°   %.1f°")
                              : std::string("-"));
        scale->setText(Vector3TextUVE(bone.localScale));
    };
    m_grid->addRow(category, "Parent", parent);
    m_grid->addRow(category, "Rest Position", position);
    m_grid->addRow(category, "Rest Rotation", rotation);
    m_grid->addRow(category, "Rest Scale", scale);
}

void EditorPrismDetailsUVE::AddBonePickerUVE(const int category, const EntryUVE& entry, const PropertyUVE& property,
                                             const void* instance, const bool writable) {
    const auto* attachment = static_cast<const Scene::BoneAttachment3DComponentUVE*>(instance);
    auto button = prism::make<prism::Button>("Empty", prism::Icon::None, prism::ButtonStyle::Default);
    button->setTooltip(property.tooltip);
    std::weak_ptr<prism::Button> weak = button;
    auto row = std::make_shared<RowUVE>();
    row->entry = &entry;
    const PropertyUVE* p = &property;
    row->sync = [p, weak](const void* inst) {
        auto b = weak.lock();
        if (b && inst != nullptr) {
            std::string v;
            p->getValue(inst, &v);
            const std::string text = v.empty() ? std::string("Empty") : v;
            if (b->text() != text) {
                b->setText(text);
            }
        }
    };
    const EntryUVE* e = &entry;
    button->onClick = [this, e, p, weak] {
        const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
        const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
        if (!em.HasComponentUVE<Scene::BoneAttachment3DComponentUVE>(selected)) {
            return;
        }
        const Scene::EntityUVE skeletonEntity = em.GetComponentUVE<Scene::BoneAttachment3DComponentUVE>(selected).skeleton;
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Empty", [this, e, p] {
            const std::string none;
            static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &none));
        }));
        items.push_back(prism::MenuItem::Sep());
        if (skeletonEntity == Scene::kInvalidEntityUVE || !em.IsAliveUVE(skeletonEntity) ||
            !em.HasComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity)) {
            items.push_back(prism::MenuItem::Item("Pick the Skeleton first", {}).disabled());
        } else {
            const auto& skeleton = em.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity);
            std::vector<prism::MenuItem> bones;
            for (const Scene::SkeletonBoneUVE& bone : skeleton.bones) {
                prism::MenuItem item = prism::MenuItem::Item(bone.name, [this, e, p, name = bone.name] {
                    static_cast<void>(m_editor.SetSelectedComponentPropertyUVE(*e, *p, &name));
                });
                item.withImage(BoneIconUVE(), kBoneIconSizeUVE);
                bones.push_back(std::move(item));
            }
            if (bones.empty()) {
                items.push_back(prism::MenuItem::Item("The skeleton has no bones", {}).disabled());
            } else if (bones.size() <= 24U) {
                for (prism::MenuItem& item : bones) {
                    items.push_back(std::move(item));
                }
            } else {
                items.push_back(prism::MenuItem::Sub("Bones (" + std::to_string(bones.size()) + ")", std::move(bones)));
            }
        }
        if (auto b = weak.lock()) {
            prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
        }
    };
    button->setEnabled(writable);
    row->editor = button.get();
    row->sync(attachment);
    m_rows.push_back(row);
    m_grid->addRow(category, property.displayName.empty() ? property.name : property.displayName, button, property.tooltip);
}

} // namespace UVE::Editor
