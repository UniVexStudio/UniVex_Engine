// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Details rows some objects need beyond their properties: a Camera3D's Preview / Screen, a
// Skeleton3D's source model and its bones, a BoneAttachment3D's bone.
#include "editor_details.h"

#include "editor.h"

#include "bone_attachment_3d.h"
#include "camera_component.h"
#include "ragdoll_3d.h"
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
#include <cstddef>
#include <cstdint>

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

void EditorDetailsUVE::AddCameraPreviewUVE(const Scene::EntityUVE entity) {
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

// --------------------------------------------------------------------------------------- ragdoll

void EditorDetailsUVE::AddRagdollBuilderUVE(const Scene::EntityUVE entity) {
    if (!MatchesFilterUVE("Ragdoll") && !MatchesFilterUVE("Humanoid") && !MatchesFilterUVE("Preview") &&
        !MatchesFilterUVE("Bone") && !MatchesFilterUVE("Limits")) {
        return;
    }
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (!em.IsAliveUVE(entity) || !em.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) {
        return;
    }

    const int category = m_grid->addCategory("Ragdoll3D / Builder & Preview", true);

    auto generateBtn = prism::make<prism::Button>("Auto-Generate", prism::Icon::None, prism::ButtonStyle::Default);
    generateBtn->setTooltip("Generate humanoid capsule bodies & joint limits from the linked Skeleton3D");
    generateBtn->onClick = [this, entity] {
        Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
        if (!entities.IsAliveUVE(entity) || !entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) return;
        Scene::Ragdoll3DComponentUVE& ragdoll = entities.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(entity);
        if (ragdoll.editorPreviewSimulating) {
            static_cast<void>(Scene::StopRagdollEditorPreviewUVE(entities, entity));
        }
        if (ragdoll.skeleton != Scene::kInvalidEntityUVE && entities.IsAliveUVE(ragdoll.skeleton) &&
            entities.HasComponentUVE<Scene::Skeleton3DComponentUVE>(ragdoll.skeleton)) {
            const auto& skel = entities.GetComponentUVE<Scene::Skeleton3DComponentUVE>(ragdoll.skeleton);
            ragdoll.bones = Scene::BuildHumanoidRagdollBonesUVE(skel.bones);
            ragdoll.initialized = false;
            static_cast<void>(Scene::InitializeRagdoll3DUVE(entities, entity));
            m_selectedRagdollBoneIndex = 0U;
            m_editor.m_sceneDirty = true;
            m_rebuildRequested = true;
        }
    };

    auto previewBtn = prism::make<prism::Button>("Simulate Preview", prism::Icon::Play, prism::ButtonStyle::Default);
    previewBtn->setCheckable(true);
    previewBtn->setTooltip("Drop and test this ragdoll live in Edit mode; turning off restores the bind pose");
    previewBtn->onToggle = [this, entity](const bool on) {
        Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
        if (!entities.IsAliveUVE(entity) || !entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) return;
        if (on) {
            static_cast<void>(Scene::StartRagdollEditorPreviewUVE(entities, entity));
        } else {
            static_cast<void>(Scene::StopRagdollEditorPreviewUVE(entities, entity));
            static_cast<void>(Scene::InitializeRagdoll3DUVE(entities, entity));
        }
        m_rebuildRequested = true;
    };

    auto impulseBtn = prism::make<prism::Button>("Poke", prism::Icon::None, prism::ButtonStyle::Default);
    impulseBtn->setTooltip("Apply a test sideways hit impulse to the ragdoll torso during preview");
    impulseBtn->onClick = [this, entity] {
        Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
        if (!entities.IsAliveUVE(entity) || !entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) return;
        Scene::Ragdoll3DComponentUVE& ragdoll = entities.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(entity);
        if (!ragdoll.editorPreviewSimulating) {
            static_cast<void>(Scene::StartRagdollEditorPreviewUVE(entities, entity));
        }
        if (ragdoll.solver.GetBodyCountUVE() > 0U) {
            const std::size_t targetIdx = ragdoll.solver.GetBodyCountUVE() > 1U ? 1U : 0U;
            const Math::Vector3UVE hitPos = ragdoll.solver.GetBodyUVE(targetIdx).position;
            static_cast<void>(Scene::ApplyRagdollImpactUVE(entities, entity, hitPos,
                                                           Math::Vector3UVE{18.0F, 4.0F, -12.0F}, 1.0F));
        }
    };

    auto actionBar = prism::hbox(4.0F);
    actionBar->add(generateBtn, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    actionBar->add(previewBtn, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    actionBar->add(impulseBtn, prism::Slot{0.0F, {}, prism::HAlign::Center, prism::VAlign::Center});

    auto statusLabel = ValueLabelUVE("-");
    auto syncRow = std::make_shared<RowUVE>();
    std::weak_ptr<prism::Button> weakPreview = previewBtn;
    std::weak_ptr<prism::Label> weakStatus = statusLabel;
    syncRow->sync = [this, entity, weakPreview, weakStatus](const void*) {
        Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
        if (!entities.IsAliveUVE(entity) || !entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) return;
        const Scene::Ragdoll3DComponentUVE& ragdoll = entities.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(entity);
        if (auto b = weakPreview.lock()) {
            if (b->checked() != ragdoll.editorPreviewSimulating) {
                b->setChecked(ragdoll.editorPreviewSimulating);
            }
        }
        if (auto lbl = weakStatus.lock()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%zu bodies  |  %zu joints  |  %.1f kg  |  %s",
                          ragdoll.solver.GetBodyCountUVE(), ragdoll.solver.GetJointCountUVE(),
                          static_cast<double>(ragdoll.solver.GetTotalMassUVE()),
                          ragdoll.solver.IsSleepingUVE() ? "Sleeping"
                                                         : (ragdoll.initialized ? "Ready" : "Not Built"));
            lbl->setText(buf);
        }
    };
    syncRow->editor = actionBar.get();
    syncRow->sync(nullptr);
    m_rows.push_back(syncRow);
    m_grid->addRow(category, "Actions", actionBar, "Auto-generate capsules, live preview in Edit mode, or test hit impulse.");
    m_grid->addRow(category, "Rig Summary", statusLabel);

    // Compact Per-Bone Body & Joint Limit Inspector.
    const Scene::Ragdoll3DComponentUVE& ragdoll = em.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(entity);
    if (ragdoll.bones.empty()) {
        return;
    }
    if (m_selectedRagdollBoneIndex >= ragdoll.bones.size()) {
        m_selectedRagdollBoneIndex = 0U;
    }

    std::vector<std::string> boneNames;
    boneNames.reserve(ragdoll.bones.size());
    for (const Scene::RagdollBoneSetupUVE& bone : ragdoll.bones) {
        boneNames.push_back(bone.boneName);
    }
    auto bonePicker = prism::make<prism::ComboBox>(boneNames, static_cast<int>(m_selectedRagdollBoneIndex));
    bonePicker->onChange = [this](const int index) {
        if (index >= 0) {
            m_selectedRagdollBoneIndex = static_cast<std::size_t>(index);
            m_rebuildRequested = true;
        }
    };
    m_grid->addRow(category, "Edit Bone", bonePicker, "Select a ragdoll body to tune mass, capsule size, and joint limits.");

    const std::size_t boneIdx = m_selectedRagdollBoneIndex;
    const Scene::RagdollBoneSetupUVE& currentBone = ragdoll.bones[boneIdx];

    const auto addBoneFloatRow = [&](const std::string& label, const float initialValue, const float minVal,
                                     const float maxVal, const float step, const int decimals,
                                     auto setter) {
        auto num = prism::make<prism::NumberField>(initialValue, step, decimals);
        num->setRange(minVal, maxVal);
        auto applyEdit = [this, entity, boneIdx, setter](const float value) {
            Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
            if (!entities.IsAliveUVE(entity) || !entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) return;
            Scene::Ragdoll3DComponentUVE& comp = entities.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(entity);
            if (boneIdx >= comp.bones.size()) return;
            setter(comp.bones[boneIdx], value);
            if (!comp.editorPreviewSimulating) {
                comp.initialized = false;
                static_cast<void>(Scene::InitializeRagdoll3DUVE(entities, entity));
            }
            m_editor.m_sceneDirty = true;
        };
        num->onChange = applyEdit;
        num->onCommit = applyEdit;
        m_grid->addRow(category, label, num);
    };

    addBoneFloatRow("Bone Mass (kg)", currentBone.mass, 0.05F, 200.0F, 0.25F, 2,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.mass = v; });
    addBoneFloatRow("Capsule Radius", currentBone.radius, 0.01F, 1.0F, 0.005F, 3,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.radius = v; });
    addBoneFloatRow("Leaf Half-Height", currentBone.halfHeight, 0.0F, 2.0F, 0.01F, 3,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.halfHeight = v; });
    addBoneFloatRow("Swing 1 Limit (°)", currentBone.swing1LimitDegrees, 0.0F, 180.0F, 1.0F, 1,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.swing1LimitDegrees = v; });
    addBoneFloatRow("Swing 2 Limit (°)", currentBone.swing2LimitDegrees, 0.0F, 180.0F, 1.0F, 1,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.swing2LimitDegrees = v; });
    if (currentBone.twistAsymmetric) {
        addBoneFloatRow("Twist Min (°)", currentBone.twistLowerLimitDegrees, -180.0F, 180.0F, 1.0F, 1,
                        [](Scene::RagdollBoneSetupUVE& b, const float v) {
                            b.twistLowerLimitDegrees = std::min(v, b.twistUpperLimitDegrees);
                        });
        addBoneFloatRow("Twist Max (°)", currentBone.twistUpperLimitDegrees, -180.0F, 180.0F, 1.0F, 1,
                        [](Scene::RagdollBoneSetupUVE& b, const float v) {
                            b.twistUpperLimitDegrees = std::max(v, b.twistLowerLimitDegrees);
                        });
    } else {
        addBoneFloatRow("Twist Limit (°)", currentBone.twistLimitDegrees, 0.0F, 180.0F, 1.0F, 1,
                        [](Scene::RagdollBoneSetupUVE& b, const float v) { b.twistLimitDegrees = v; });
    }
    addBoneFloatRow("Motor Scale", currentBone.motorStrengthScale, 0.0F, 1.0F, 0.05F, 2,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.motorStrengthScale = v; });
    addBoneFloatRow("Blend Weight", currentBone.blendWeight, 0.0F, 1.0F, 0.05F, 2,
                    [](Scene::RagdollBoneSetupUVE& b, const float v) { b.blendWeight = v; });

    auto simCheck = prism::make<prism::CheckBox>("Simulate Body", currentBone.simulate);
    simCheck->onChange = [this, entity, boneIdx](const bool v) {
        Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
        if (!entities.IsAliveUVE(entity) || !entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(entity)) return;
        Scene::Ragdoll3DComponentUVE& comp = entities.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(entity);
        if (boneIdx >= comp.bones.size()) return;
        comp.bones[boneIdx].simulate = v;
        m_editor.m_sceneDirty = true;
    };
    m_grid->addRow(category, "Simulate", simCheck);
}


// -------------------------------------------------------------------------------------- skeleton

void EditorDetailsUVE::AddSkeletonSourceUVE(const int category, const EntryUVE& entry, const PropertyUVE& property,
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

void EditorDetailsUVE::AddSkeletonBonesUVE(const int category, const void* instance) {
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

void EditorDetailsUVE::AddBonePickerUVE(const int category, const EntryUVE& entry, const PropertyUVE& property,
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
