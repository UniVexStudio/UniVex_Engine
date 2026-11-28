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
#include <cctype>
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

constexpr int kBoneIconSize = 16;

/// A bone, drawn once: a shaft with a pair of knuckles at each end, bone-coloured, antialiased.
[[nodiscard]] const std::uint32_t* BoneIcon() {
    static const std::array<std::uint32_t, kBoneIconSize * kBoneIconSize> kIcon = [] {
        std::array<std::uint32_t, kBoneIconSize * kBoneIconSize> pixels{};
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
        for (int y = 0; y < kBoneIconSize; ++y) {
            for (int x = 0; x < kBoneIconSize; ++x) {
                const float px = static_cast<float>(x) + 0.5F, py = static_cast<float>(y) + 0.5F;
                float d = segment(px, py) - 1.35F;
                d = std::min(d, circle(px, py, ax + nx * k - dx / len * 0.6F, ay + ny * k - dy / len * 0.6F) - 1.75F);
                d = std::min(d, circle(px, py, ax - nx * k - dx / len * 0.6F, ay - ny * k - dy / len * 0.6F) - 1.75F);
                d = std::min(d, circle(px, py, bx + nx * k + dx / len * 0.6F, by + ny * k + dy / len * 0.6F) - 1.75F);
                d = std::min(d, circle(px, py, bx - nx * k + dx / len * 0.6F, by - ny * k + dy / len * 0.6F) - 1.75F);
                const float coverage = std::clamp(0.5F - d, 0.0F, 1.0F);
                const auto alpha = static_cast<std::uint32_t>(std::lround(coverage * 255.0F));
                pixels[static_cast<std::size_t>(y * kBoneIconSize + x)] = (alpha << 24U) | 0x00EDE3C9U;
            }
        }
        return pixels;
    }();
    return kIcon.data();
}

[[nodiscard]] std::string Vector3Text(const Math::Vector3& v, const char* format = "%.3f   %.3f   %.3f") {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, format, static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
    return buffer;
}

[[nodiscard]] prism::Ref<prism::Label> ValueLabel(const std::string& text) {
    auto label = prism::make<prism::Label>(text);
    label->setColor(prism::ThemeColor(&prism::Theme::textBright));
    return label;
}

} // namespace

// ---------------------------------------------------------------------------------------- camera

void EditorDetails::AddCameraPreview(const Scene::Entity entity) {
    if (!MatchesFilter("Camera") && !MatchesFilter("Preview") && !MatchesFilter("Screen")) {
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
            m_editor.SetPreviewCamera(entity);
        } else {
            m_editor.ClearPreviewCamera();
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
    auto row = std::make_shared<Row>();
    std::weak_ptr<prism::Button> weakPreview = preview;
    row->sync = [this, entity, weakPreview](const void*) {
        if (auto b = weakPreview.lock()) {
            const bool on = m_editor.GetPreviewCamera() == entity;
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

void EditorDetails::AddRagdollBuilder(const Scene::Entity entity) {
    if (!MatchesFilter("Ragdoll") && !MatchesFilter("Humanoid") && !MatchesFilter("Preview") &&
        !MatchesFilter("Bone") && !MatchesFilter("Limits")) {
        return;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (!em.IsAlive(entity) || !em.HasComponent<Scene::Ragdoll3DComponent>(entity)) {
        return;
    }

    // Links the nearest skeleton and fills the bone list when needed; on failure the status bar
    // carries the ragdoll's own reason, so the author knows what to change.
    const auto prepare = [this](Scene::IEntityManager& entities, const Scene::Entity ragEntity,
                                const bool regenerate) -> bool {
        if (Scene::PrepareRagdoll3D(entities, ragEntity, regenerate)) {
            return true;
        }
        m_editor.m_contentStatusMessage =
            entities.GetComponent<Scene::Ragdoll3DComponent>(ragEntity).lastSetupError;
        return false;
    };
    const auto reportSetupError = [this](const Scene::Ragdoll3DComponent& ragdoll) {
        m_editor.m_contentStatusMessage = ragdoll.lastSetupError.empty()
                                              ? std::string("Ragdoll3D could not be built.")
                                              : "Ragdoll3D: " + ragdoll.lastSetupError;
    };

    const int category = m_grid->addCategory("Ragdoll3D / Builder & Preview", true);

    auto generateBtn = prism::make<prism::Button>("Auto-Generate", prism::Icon::None, prism::ButtonStyle::Default);
    generateBtn->setTooltip("Generate capsule bodies & joint limits from the linked Skeleton3D (humanoid or generic rig)");
    generateBtn->onClick = [this, entity, prepare, reportSetupError] {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        Scene::Ragdoll3DComponent& ragdoll = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (ragdoll.editorPreviewSimulating) {
            static_cast<void>(Scene::StopRagdollEditorPreview(entities, entity));
        }
        if (!prepare(entities, entity, /*regenerate=*/true)) {
            m_rebuildRequested = true;
            return;
        }
        m_editor.m_services->GetSceneGraph().Update(entities);
        ragdoll.initialized = false;
        if (Scene::InitializeRagdoll3D(entities, entity)) {
            m_selectedRagdollBoneIndex = 0U;
            m_editor.m_selectedRagdollBoneName = ragdoll.bones.empty() ? std::string() : ragdoll.bones[0].boneName;
            m_editor.m_sceneDirty = true;
            m_editor.m_contentStatusMessage =
                "Generated " + std::to_string(ragdoll.solver.GetBodyCount()) + " ragdoll bodies.";
        } else {
            reportSetupError(ragdoll);
        }
        m_rebuildRequested = true;
    };

    auto previewBtn = prism::make<prism::Button>("Simulate Preview", prism::Icon::Play, prism::ButtonStyle::Default);
    previewBtn->setCheckable(true);
    previewBtn->setTooltip("Drop and test this ragdoll live in Edit mode; turning off restores the bind pose");
    previewBtn->onToggle = [this, entity, prepare, reportSetupError](const bool on) {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        Scene::Ragdoll3DComponent& ragdoll = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (on) {
            if (!prepare(entities, entity, /*regenerate=*/false)) {
                m_rebuildRequested = true;
                return;
            }
            m_editor.m_services->GetSceneGraph().Update(entities);
            if (!Scene::StartRagdollEditorPreview(entities, entity)) {
                reportSetupError(ragdoll);
            } else {
                m_editor.m_contentStatusMessage =
                    "Ragdoll3D preview: " + std::to_string(ragdoll.solver.GetBodyCount()) + " bodies simulating.";
            }
        } else {
            static_cast<void>(Scene::StopRagdollEditorPreview(entities, entity));
            m_editor.m_services->GetSceneGraph().Update(entities);
            static_cast<void>(Scene::InitializeRagdoll3D(entities, entity));
        }
        m_rebuildRequested = true;
    };

    auto impulseBtn = prism::make<prism::Button>("Poke", prism::Icon::None, prism::ButtonStyle::Default);
    impulseBtn->setTooltip("Apply a test sideways hit impulse to the ragdoll torso during preview");
    impulseBtn->onClick = [this, entity, prepare, reportSetupError] {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        Scene::Ragdoll3DComponent& ragdoll = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (!ragdoll.editorPreviewSimulating) {
            if (!prepare(entities, entity, /*regenerate=*/false)) return;
            m_editor.m_services->GetSceneGraph().Update(entities);
            if (!Scene::StartRagdollEditorPreview(entities, entity)) {
                reportSetupError(ragdoll);
                m_rebuildRequested = true;
                return;
            }
        }
        if (ragdoll.solver.GetBodyCount() > 0U) {
            const std::size_t targetIdx = ragdoll.solver.GetBodyCount() > 1U ? 1U : 0U;
            const Math::Vector3 hitPos = ragdoll.solver.GetBody(targetIdx).position;
            static_cast<void>(Scene::ApplyRagdollImpact(entities, entity, hitPos,
                                                           Math::Vector3{18.0F, 4.0F, -12.0F}, 1.0F));
        }
        m_rebuildRequested = true;
    };

    auto actionBar = prism::hbox(4.0F);
    actionBar->add(generateBtn, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    actionBar->add(previewBtn, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    actionBar->add(impulseBtn, prism::Slot{0.0F, {}, prism::HAlign::Center, prism::VAlign::Center});

    auto statusLabel = ValueLabel("-");
    auto syncRow = std::make_shared<Row>();
    std::weak_ptr<prism::Button> weakPreview = previewBtn;
    std::weak_ptr<prism::Label> weakStatus = statusLabel;
    syncRow->sync = [this, entity, weakPreview, weakStatus](const void*) {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        const Scene::Ragdoll3DComponent& ragdoll = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (auto b = weakPreview.lock()) {
            if (b->checked() != ragdoll.editorPreviewSimulating) {
                b->setChecked(ragdoll.editorPreviewSimulating);
            }
        }
        if (auto lbl = weakStatus.lock()) {
            char buf[128];
            const char* stateText =
                ragdoll.editorPreviewSimulating
                    ? "Previewing"
                    : (ragdoll.solver.IsSleeping() ? "Sleeping" : (ragdoll.initialized ? "Ready" : "Not Built"));
            if (!ragdoll.initialized && !ragdoll.lastSetupError.empty()) {
                lbl->setText("Not Built  |  " + ragdoll.lastSetupError);
            } else {
                std::snprintf(buf, sizeof(buf), "%zu bodies  |  %zu joints  |  %.1f kg  |  %s",
                              ragdoll.solver.GetBodyCount(), ragdoll.solver.GetJointCount(),
                              static_cast<double>(ragdoll.solver.GetTotalMass()), stateText);
                lbl->setText(buf);
            }
        }
    };
    syncRow->editor = actionBar.get();
    syncRow->sync(nullptr);
    m_rows.push_back(syncRow);
    m_grid->addRow(category, "Actions", actionBar, "Auto-generate capsules, live preview in Edit mode, or test hit impulse.");
    m_grid->addRow(category, "Rig Summary", statusLabel);

    // Compact Per-Bone Body & Joint Limit Inspector.
    const Scene::Ragdoll3DComponent& ragdoll = em.GetComponent<Scene::Ragdoll3DComponent>(entity);
    if (ragdoll.bones.empty()) {
        m_editor.m_selectedRagdollBoneName.clear();
        return;
    }
    if (m_selectedRagdollBoneIndex >= ragdoll.bones.size()) {
        m_selectedRagdollBoneIndex = 0U;
    }
    m_editor.m_selectedRagdollBoneName = ragdoll.bones[m_selectedRagdollBoneIndex].boneName;

    std::vector<std::string> boneNames;
    boneNames.reserve(ragdoll.bones.size());
    for (const Scene::RagdollBoneSetup& bone : ragdoll.bones) {
        boneNames.push_back(bone.boneName);
    }
    auto bonePicker = prism::make<prism::ComboBox>(boneNames, static_cast<int>(m_selectedRagdollBoneIndex));
    bonePicker->onChange = [this, entity](const int index) {
        if (index >= 0) {
            m_selectedRagdollBoneIndex = static_cast<std::size_t>(index);
            Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
            if (entities.IsAlive(entity) && entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) {
                const auto& comp = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
                if (m_selectedRagdollBoneIndex < comp.bones.size()) {
                    m_editor.m_selectedRagdollBoneName = comp.bones[m_selectedRagdollBoneIndex].boneName;
                }
            }
            m_rebuildRequested = true;
        }
    };

    auto addBoneBtn = prism::make<prism::Button>("+", prism::Icon::None, prism::ButtonStyle::Default);
    addBoneBtn->setTooltip("Add another bone from the linked Skeleton3D to this ragdoll");
    std::weak_ptr<prism::Button> weakAddBone = addBoneBtn;
    addBoneBtn->onClick = [this, entity, weakAddBone] {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        const auto& comp = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (comp.skeleton == Scene::kInvalidEntity || !entities.IsAlive(comp.skeleton) ||
            !entities.HasComponent<Scene::Skeleton3DComponent>(comp.skeleton)) {
            return;
        }
        const auto& skel = entities.GetComponent<Scene::Skeleton3DComponent>(comp.skeleton);
        std::vector<prism::MenuItem> items;
        for (const Scene::SkeletonBone& skelBone : skel.bones) {
            const bool already = std::any_of(comp.bones.begin(), comp.bones.end(),
                                             [&](const Scene::RagdollBoneSetup& b) { return b.boneName == skelBone.name; });
            if (already || skelBone.name.empty()) continue;
            items.push_back(prism::MenuItem::Item(skelBone.name, [this, entity, name = skelBone.name] {
                Scene::IEntityManager& innerEm = m_editor.m_services->GetEntityManager();
                if (!innerEm.IsAlive(entity) || !innerEm.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
                Scene::Ragdoll3DComponent& r = innerEm.GetComponent<Scene::Ragdoll3DComponent>(entity);
                if (r.bones.size() >= Scene::kMaximumRagdollBones) return;
                Scene::RagdollBoneSetup setup;
                setup.boneName = name;
                r.bones.push_back(std::move(setup));
                m_selectedRagdollBoneIndex = r.bones.size() - 1U;
                m_editor.m_selectedRagdollBoneName = name;
                if (!r.editorPreviewSimulating) {
                    m_editor.m_services->GetSceneGraph().Update(innerEm);
                    r.initialized = false;
                    static_cast<void>(Scene::InitializeRagdoll3D(innerEm, entity));
                }
                m_editor.m_sceneDirty = true;
                m_rebuildRequested = true;
            }));
        }
        if (items.empty()) {
            items.push_back(prism::MenuItem::Item("All skeleton bones are already in this ragdoll", {}).disabled());
        }
        if (auto b = weakAddBone.lock()) {
            prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
        }
    };

    auto removeBoneBtn = prism::make<prism::Button>("-", prism::Icon::None, prism::ButtonStyle::Default);
    removeBoneBtn->setTooltip("Remove the selected bone collider from this ragdoll");
    removeBoneBtn->setEnabled(ragdoll.bones.size() > 1U);
    removeBoneBtn->onClick = [this, entity] {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        Scene::Ragdoll3DComponent& comp = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (comp.bones.size() <= 1U || m_selectedRagdollBoneIndex >= comp.bones.size()) return;
        comp.bones.erase(comp.bones.begin() + static_cast<std::ptrdiff_t>(m_selectedRagdollBoneIndex));
        if (m_selectedRagdollBoneIndex >= comp.bones.size()) {
            m_selectedRagdollBoneIndex = comp.bones.size() - 1U;
        }
        m_editor.m_selectedRagdollBoneName = comp.bones[m_selectedRagdollBoneIndex].boneName;
        if (!comp.editorPreviewSimulating) {
            m_editor.m_services->GetSceneGraph().Update(entities);
            comp.initialized = false;
            static_cast<void>(Scene::InitializeRagdoll3D(entities, entity));
        }
        m_editor.m_sceneDirty = true;
        m_rebuildRequested = true;
    };

    auto boneRow = prism::hbox(4.0F);
    boneRow->add(bonePicker, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    boneRow->add(addBoneBtn, prism::Slot{0.0F, {}, prism::HAlign::Center, prism::VAlign::Center});
    boneRow->add(removeBoneBtn, prism::Slot{0.0F, {}, prism::HAlign::Center, prism::VAlign::Center});
    m_grid->addRow(category, "Edit Bone", boneRow,
                   "Select a ragdoll body (highlighted gold in viewport) to tune mass, capsule size, and joint limits.");

    const std::size_t boneIdx = m_selectedRagdollBoneIndex;
    const Scene::RagdollBoneSetup& currentBone = ragdoll.bones[boneIdx];

    const auto addBoneFloatRow = [&](const std::string& label, const float initialValue, const float minVal,
                                     const float maxVal, const float step, const int decimals,
                                     auto setter) {
        auto num = prism::make<prism::NumberField>(initialValue, step, decimals);
        num->setRange(minVal, maxVal);
        auto applyEdit = [this, entity, boneIdx, setter](const float value) {
            Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
            if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
            Scene::Ragdoll3DComponent& comp = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
            if (boneIdx >= comp.bones.size()) return;
            setter(comp.bones[boneIdx], value);
            if (!comp.editorPreviewSimulating) {
                m_editor.m_services->GetSceneGraph().Update(entities);
                comp.initialized = false;
                static_cast<void>(Scene::InitializeRagdoll3D(entities, entity));
            }
            m_editor.m_sceneDirty = true;
        };
        num->onChange = applyEdit;
        num->onCommit = applyEdit;
        m_grid->addRow(category, label, num);
    };

    addBoneFloatRow("Bone Mass (kg)", currentBone.mass, 0.05F, 200.0F, 0.25F, 2,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.mass = v; });
    addBoneFloatRow("Capsule Radius", currentBone.radius, 0.01F, 1.0F, 0.005F, 3,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.radius = v; });
    addBoneFloatRow("Leaf Half-Height", currentBone.halfHeight, 0.0F, 2.0F, 0.01F, 3,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.halfHeight = v; });
    addBoneFloatRow("Swing 1 Limit (°)", currentBone.swing1LimitDegrees, 0.0F, 180.0F, 1.0F, 1,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.swing1LimitDegrees = v; });
    addBoneFloatRow("Swing 2 Limit (°)", currentBone.swing2LimitDegrees, 0.0F, 180.0F, 1.0F, 1,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.swing2LimitDegrees = v; });
    if (currentBone.twistAsymmetric) {
        addBoneFloatRow("Twist Min (°)", currentBone.twistLowerLimitDegrees, -180.0F, 180.0F, 1.0F, 1,
                        [](Scene::RagdollBoneSetup& b, const float v) {
                            b.twistLowerLimitDegrees = std::min(v, b.twistUpperLimitDegrees);
                        });
        addBoneFloatRow("Twist Max (°)", currentBone.twistUpperLimitDegrees, -180.0F, 180.0F, 1.0F, 1,
                        [](Scene::RagdollBoneSetup& b, const float v) {
                            b.twistUpperLimitDegrees = std::max(v, b.twistLowerLimitDegrees);
                        });
    } else {
        addBoneFloatRow("Twist Limit (°)", currentBone.twistLimitDegrees, 0.0F, 180.0F, 1.0F, 1,
                        [](Scene::RagdollBoneSetup& b, const float v) { b.twistLimitDegrees = v; });
    }
    addBoneFloatRow("Motor Scale", currentBone.motorStrengthScale, 0.0F, 1.0F, 0.05F, 2,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.motorStrengthScale = v; });
    addBoneFloatRow("Blend Weight", currentBone.blendWeight, 0.0F, 1.0F, 0.05F, 2,
                    [](Scene::RagdollBoneSetup& b, const float v) { b.blendWeight = v; });

    auto simCheck = prism::make<prism::CheckBox>("Simulate Body", currentBone.simulate);
    simCheck->onChange = [this, entity, boneIdx](const bool v) {
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (!entities.IsAlive(entity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(entity)) return;
        Scene::Ragdoll3DComponent& comp = entities.GetComponent<Scene::Ragdoll3DComponent>(entity);
        if (boneIdx >= comp.bones.size()) return;
        comp.bones[boneIdx].simulate = v;
        if (!comp.editorPreviewSimulating) {
            m_editor.m_services->GetSceneGraph().Update(entities);
            comp.initialized = false;
            static_cast<void>(Scene::InitializeRagdoll3D(entities, entity));
        }
        m_editor.m_sceneDirty = true;
    };
    m_grid->addRow(category, "Simulate", simCheck);
}


// -------------------------------------------------------------------------------------- skeleton

void EditorDetails::AddSkeletonSource(const int category, const Entry& entry, const Property& property,
                                                 const void* instance, const bool writable) {
    const auto* skeleton = static_cast<const Scene::Skeleton3DComponent*>(instance);
    const std::string current = skeleton != nullptr ? skeleton->skeletonAssetPath : std::string();
    auto button = prism::make<prism::Button>(current.empty() ? std::string("Empty")
                                                             : std::filesystem::path{current}.stem().string(),
                                             prism::Icon::None, prism::ButtonStyle::Default);
    button->setTooltip(property.tooltip);
    std::weak_ptr<prism::Button> weak = button;
    button->onClick = [this, weak, current] {
        // Empty, the project's rigged models, then the usual Load / Import, and Reload Bones.
        const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        const auto bind = [this](const std::filesystem::path& relative) {
            if (!m_editor.BindSelectedSkeletonSource(relative) && !m_editor.m_skeletonSourceStatus.empty()) {
                m_editor.m_contentStatusMessage = m_editor.m_skeletonSourceStatus;
            }
        };
        const auto bindAbsolute = [this, bind](const std::filesystem::path& absolute) {
            const std::filesystem::path root = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
            const auto contentRelative = [](const std::filesystem::path& path, const std::filesystem::path& base) {
                // Both sides canonical: a symlinked or relative content root (Android /sdcard, "assets/")
                // must not make a file inside the project look outside it.
                std::error_code canonError;
                std::filesystem::path p = std::filesystem::weakly_canonical(path, canonError);
                if (canonError) { p = path; }
                std::filesystem::path b = std::filesystem::weakly_canonical(base, canonError);
                if (canonError) { b = base; }
                return p.lexically_relative(b);
            };
            const std::filesystem::path relative = contentRelative(absolute, root);
            if (relative.empty() || *relative.begin() == "..") {
                m_editor.m_contentStatusMessage = "A skeleton's model must be in the project: use Import to bring it in.";
                return;
            }
            m_editor.RefreshProjectFileIndex();
            bind(relative);
        };
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Empty", [bind] { bind({}); }));
        items.push_back(prism::MenuItem::Sep());
        bool any = false;
        for (const Asset::ProjectFileEntry& file : snapshot.entries) {
            if (file.kind == Asset::ProjectFileEntryKind::Directory) {
                continue;
            }
            const EditorModelSourceInfo* const source = m_editor.FindModelSourceInfo(file.relativePath);
            std::string ext = file.relativePath.extension().string();
            std::ranges::transform(ext, ext.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            bool hasRig = (source != nullptr && source->hasSkeleton);
            if (!hasRig && ext == ".uvmodel") {
                std::filesystem::path skelPath = snapshot.contentRoot / file.relativePath;
                skelPath.replace_extension(".uvskel");
                std::error_code ec;
                hasRig = std::filesystem::is_regular_file(skelPath, ec);
            }
            if (!hasRig) {
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
        const std::vector<std::string> kinds{".uvmodel", ".glb", ".gltf", ".fbx"};
        items.push_back(prism::MenuItem::Item("Load...", [this, kinds, bindAbsolute] {
            FilePickerRequest request;
            request.title = "Load Skeleton Source";
            request.extensions = kinds;
            request.onPick = bindAbsolute;
            m_editor.OpenFilePicker(std::move(request));
        }));
        items.push_back(prism::MenuItem::Item("Import...", [this, kinds, bindAbsolute] {
            FilePickerRequest request;
            request.title = "Import Skeleton Source";
            request.extensions = kinds;
            request.quickImport = true;
            request.onPick = bindAbsolute;
            m_editor.OpenFilePicker(std::move(request));
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

void EditorDetails::AddSkeletonBones(const int category, const void* instance) {
    const auto* skeleton = static_cast<const Scene::Skeleton3DComponent*>(instance);
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
            node->image = BoneIcon();
            node->imageSize = kBoneIconSize;
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

    auto parent = ValueLabel("-");
    auto position = ValueLabel("-");
    auto rotation = ValueLabel("-");
    auto scale = ValueLabel("-");
    tree->onSelect = [this, bones, parent, position, rotation, scale](const std::vector<prism::TreeNode*>& selected) {
        if (selected.empty() || selected.front()->id <= 0 || static_cast<std::size_t>(selected.front()->id) > bones.size()) {
            return;
        }
        const Scene::SkeletonBone& bone = bones[static_cast<std::size_t>(selected.front()->id - 1)];
        m_editor.m_selectedSkeletonBone = bone.name;
        parent->setText(bone.parentIndex < 0 || static_cast<std::size_t>(bone.parentIndex) >= bones.size()
                            ? std::string("Root")
                            : bones[static_cast<std::size_t>(bone.parentIndex)].name);
        position->setText(Vector3Text(bone.localPosition));
        Math::Vector3 euler{};
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
        rotation->setText(Math::TryToEuler(bone.localRotation, euler)
                              ? Vector3Text(Math::Vector3{euler.x * kDegrees, euler.y * kDegrees, euler.z * kDegrees},
                                               "%.1f°   %.1f°   %.1f°")
                              : std::string("-"));
        scale->setText(Vector3Text(bone.localScale));
    };
    m_grid->addRow(category, "Parent", parent);
    m_grid->addRow(category, "Rest Position", position);
    m_grid->addRow(category, "Rest Rotation", rotation);
    m_grid->addRow(category, "Rest Scale", scale);
}

void EditorDetails::AddBonePicker(const int category, const Entry& entry, const Property& property,
                                             const void* instance, const bool writable) {
    const auto* attachment = static_cast<const Scene::BoneAttachment3DComponent*>(instance);
    auto button = prism::make<prism::Button>("Empty", prism::Icon::None, prism::ButtonStyle::Default);
    button->setTooltip(property.tooltip);
    std::weak_ptr<prism::Button> weak = button;
    auto row = std::make_shared<Row>();
    row->entry = &entry;
    const Property* p = &property;
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
    const Entry* e = &entry;
    button->onClick = [this, e, p, weak] {
        const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        const Scene::Entity selected = m_editor.GetSelectedEntity();
        if (!em.HasComponent<Scene::BoneAttachment3DComponent>(selected)) {
            return;
        }
        const Scene::Entity skeletonEntity = em.GetComponent<Scene::BoneAttachment3DComponent>(selected).skeleton;
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Empty", [this, e, p] {
            const std::string none;
            static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &none));
        }));
        items.push_back(prism::MenuItem::Sep());
        if (skeletonEntity == Scene::kInvalidEntity || !em.IsAlive(skeletonEntity) ||
            !em.HasComponent<Scene::Skeleton3DComponent>(skeletonEntity)) {
            items.push_back(prism::MenuItem::Item("Pick the Skeleton first", {}).disabled());
        } else {
            const auto& skeleton = em.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity);
            std::vector<prism::MenuItem> bones;
            for (const Scene::SkeletonBone& bone : skeleton.bones) {
                prism::MenuItem item = prism::MenuItem::Item(bone.name, [this, e, p, name = bone.name] {
                    static_cast<void>(m_editor.SetSelectedComponentProperty(*e, *p, &name));
                });
                item.withImage(BoneIcon(), kBoneIconSize);
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
