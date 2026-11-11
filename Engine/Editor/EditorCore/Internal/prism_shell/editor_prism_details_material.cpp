// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Details panel's material section: a material file's settings, edited in place under the slot
// that uses it - the same fields, presets and save path as the previous Inspector's inline material
// editor (DrawMaterialPbrEditorInlineUVE).
#include "editor_prism_details.h"

#include "editor_prism_asset_thumbnail.h"

#include "asset_guid.h"
#include "i_project_file_index.h"
#include "material_asset.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using MaterialUVE = Asset::MaterialAssetUVE;

[[nodiscard]] std::string LowerExtensionUVE(const std::filesystem::path& path) {
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

[[nodiscard]] prism::Color ToColorUVE(const Math::ColorUVE& c, const float alpha = 1.0F) {
    const auto b = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(b(c.r), b(c.g), b(c.b), b(alpha));
}

[[nodiscard]] Math::ColorUVE FromColorUVE(const prism::Color c) {
    return Math::ColorUVE{static_cast<float>(c.r) / 255.0F, static_cast<float>(c.g) / 255.0F, static_cast<float>(c.b) / 255.0F};
}

} // namespace

void EditorPrismDetailsUVE::AddMaterialSectionUVE(const std::filesystem::path& absolutePath, const std::string& slotLabel) {
    MaterialUVE loaded{};
    std::error_code ec;
    if (absolutePath.empty() || !std::filesystem::is_regular_file(absolutePath, ec) ||
        !Asset::LoadMaterialAssetUVE(absolutePath, loaded)) {
        return;
    }
    const bool writable = m_editor.IsAuthoringCommandAllowedUVE();
    const std::string title = "Material: " + absolutePath.stem().string() + (slotLabel.empty() ? "" : "  (" + slotLabel + ")");

    // Every edit: read the file as it is now, change the one field, write it back, and let the
    // thumbnails and the renderer pick up the new file (as the previous Inspector did).
    EditorUVE& editor = m_editor;
    const auto edit = [&editor, absolutePath](const std::function<void(MaterialUVE&)>& change) {
        MaterialUVE material{};
        if (!Asset::LoadMaterialAssetUVE(absolutePath, material)) {
            return;
        }
        change(material);
        if (Asset::SaveMaterialAssetUVE(material, absolutePath)) {
            editor.InvalidateMaterialThumbnailCacheUVE(absolutePath);
        }
    };

    int category = -1;
    const auto section = [&](const char* name) { category = m_grid->addCategory(title + " / " + name, true); };
    const auto add = [&](const char* label, prism::Ref<prism::Widget> w, const char* tip = "") {
        w->setEnabled(writable);
        if (tip[0] != '\0') {
            w->setTooltip(tip);
        }
        m_grid->addRow(category, label, std::move(w), tip);
    };
    const auto slider = [&](const char* label, float MaterialUVE::*field, const float lo, const float hi,
                            const char* tip = "") {
        auto s = prism::make<prism::Slider>(lo, hi, loaded.*field, 0.0F);
        s->setDecimals(2);
        s->onChange = [edit, field](const float v) { edit([&](MaterialUVE& m) { m.*field = v; }); };
        add(label, s, tip);
    };
    const auto check = [&](const char* label, bool MaterialUVE::*field, const char* tip = "") {
        auto c = prism::make<prism::CheckBox>("", loaded.*field);
        c->onChange = [edit, field](const bool v) { edit([&](MaterialUVE& m) { m.*field = v; }); };
        add(label, c, tip);
    };
    const auto color = [&](const char* label, Math::ColorUVE MaterialUVE::*field, const char* tip = "") {
        auto f = prism::make<prism::ColorField>(ToColorUVE(loaded.*field));
        f->onChange = [edit, field](const prism::Color c) { edit([&](MaterialUVE& m) { m.*field = FromColorUVE(c); }); };
        add(label, f, tip);
    };
    const auto vec2 = [&](const char* label, Math::Vector2UVE MaterialUVE::*field, const float step) {
        auto row = prism::hbox(4.0F);
        for (int axis = 0; axis < 2; ++axis) {
            const float value = axis == 0 ? (loaded.*field).x : (loaded.*field).y;
            auto n = prism::make<prism::NumberField>(value, step, 3);
            n->setAxisLabel(axis == 0 ? "X" : "Y", axis == 0 ? prism::Color(206, 72, 72) : prism::Color(92, 170, 70));
            n->onChange = [edit, field, axis](const float v) {
                edit([&](MaterialUVE& m) { (axis == 0 ? (m.*field).x : (m.*field).y) = v; });
            };
            n->setEnabled(writable);
            row->add(n, prism::Slot{1.0F});
        }
        m_grid->addRow(category, label, row);
    };
    const auto combo = [&](const char* label, std::vector<std::string> items, const int current,
                           std::function<void(MaterialUVE&, int)> apply, const char* tip = "") {
        auto c = prism::make<prism::ComboBox>(std::move(items), current);
        c->onChange = [edit, apply](const int i) { edit([&](MaterialUVE& m) { apply(m, i); }); };
        add(label, c, tip);
    };
    // A texture slot: picked from the project's registered images, never typed.
    const auto texture = [&](const char* label, Asset::AssetGuidUVE MaterialUVE::*field, const char* tip = "") {
        const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        std::string text = "Empty";
        if (loaded.*field != Asset::AssetGuidUVE{}) {
            text = "Missing texture";
            for (const Asset::ProjectFileEntryUVE& file : snapshot.entries) {
                if (file.registeredAssetGuid.has_value() && *file.registeredAssetGuid == loaded.*field) {
                    text = file.relativePath.stem().string();
                    break;
                }
            }
        }
        auto b = prism::make<prism::Button>(text, prism::Icon::None, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = b;
        b->onClick = [this, edit, field, weak, current = loaded.*field] {
            PickerActionsUVE actions;
            actions.current = PathOfGuidUVE(current);
            actions.empty = [edit, field, weak] {
                edit([&](MaterialUVE& m) { m.*field = Asset::AssetGuidUVE{}; });
                if (auto button = weak.lock()) {
                    button->setText("Empty");
                }
            };
            actions.file = [this, edit, field, weak](const std::filesystem::path& absolute) {
                const Asset::AssetGuidUVE guid = PublishPickedUVE(absolute);
                if (guid == Asset::AssetGuidUVE{}) {
                    m_editor.m_contentStatusMessage = "Could not read " + absolute.filename().string() + " as a texture.";
                    return;
                }
                edit([&](MaterialUVE& m) { m.*field = guid; });
                if (auto button = weak.lock()) {
                    const std::filesystem::path path = PathOfGuidUVE(guid);
                    button->setText(path.empty() ? absolute.stem().string() : path.stem().string());
                }
            };
            if (auto button = weak.lock()) {
                ShowPickerUVE(PickerSpecForUVE({".uvtex"}), std::move(actions), button->rect());
            }
        };
        // The texture itself beside its picker, as Unreal shows a slot.
        auto thumb = prism::make<AssetThumbnailUVE>(m_editor, AssetThumbnailUVE::KindUVE::Texture);
        thumb->setAssetUVE(PathOfGuidUVE(loaded.*field));
        auto slot = prism::hbox(6.0F);
        slot->add(thumb, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
        slot->add(b, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Top});
        auto row = std::make_shared<RowUVE>();
        std::weak_ptr<AssetThumbnailUVE> weakThumb = thumb;
        row->sync = [weakThumb](const void*) {
            if (auto t = weakThumb.lock()) {
                t->refreshUVE();
            }
        };
        row->editor = slot.get();
        m_rows.push_back(row);
        b->setEnabled(writable);
        if (tip[0] != '\0') {
            b->setTooltip(tip);
        }
        m_grid->addRow(category, label, slot, tip);
    };

    // Kind: a preset over the same physically based path; picking one applies the same defaults
    // the content catalogue's material entries use and leaves every texture alone.
    section("Preset");
    combo("Kind", {"Standard", "ORM", "Custom Shader"}, static_cast<int>(loaded.kind), [](MaterialUVE& m, const int i) {
        m.kind = static_cast<Asset::MaterialKindUVE>(std::clamp(i, 0, 2));
        switch (m.kind) {
            case Asset::MaterialKindUVE::Standard: m.useCustomShaderCode = false; break;
            case Asset::MaterialKindUVE::Orm:
                m.useCustomShaderCode = false;
                m.metallic = 1.0F;
                m.occlusionStrength = 1.0F;
                break;
            case Asset::MaterialKindUVE::Shader: m.useCustomShaderCode = true; break;
        }
    }, "Standard: metallic / roughness values with optional maps. ORM: a packed occlusion / roughness / metallic map. "
       "Custom Shader: the material supplies its own shader code.");

    section("Albedo");
    color("Color", &MaterialUVE::albedoColor, "The surface's base colour (multiplies the albedo texture).");
    slider("Alpha", &MaterialUVE::albedoAlpha, 0.0F, 1.0F, "Opacity, used by the alpha blend modes and the cutoff.");
    texture("Texture", &MaterialUVE::albedoTexture);
    check("Vertex Color", &MaterialUVE::useVertexColorAsAlbedo, "Use the mesh's vertex colours as albedo.");

    section("Normal");
    texture("Normal Map", &MaterialUVE::normalTexture);
    slider("Strength", &MaterialUVE::normalScale, 0.0F, 4.0F, "1 is the map as authored; 0 flattens it.");
    check("Flip Green", &MaterialUVE::normalFlipGreen, "For maps authored with the other green-channel convention.");

    section("Surface");
    slider("Metallic", &MaterialUVE::metallic, 0.0F, 1.0F);
    slider("Roughness", &MaterialUVE::roughness, 0.0F, 1.0F);
    slider("Specular", &MaterialUVE::dielectricSpecular, 0.0F, 1.0F, "Reflectance of non-metals (0.5 is about 4%).");
    texture("ORM / Metal-Rough", &MaterialUVE::metallicRoughnessTexture, "Packed map: G roughness, B metallic.");
    texture("Metallic Map", &MaterialUVE::metallicTexture);
    texture("Roughness Map", &MaterialUVE::roughnessTexture);
    texture("Occlusion Map", &MaterialUVE::aoTexture);
    slider("Occlusion", &MaterialUVE::occlusionStrength, 0.0F, 1.0F, "How much the occlusion map darkens cavities.");

    section("Emission");
    color("Color", &MaterialUVE::emissiveColor);
    slider("Energy", &MaterialUVE::emissiveEnergy, 0.0F, 16.0F);
    texture("Texture", &MaterialUVE::emissiveTexture);

    section("Rendering");
    combo("Blend", {"Opaque", "Alpha Blend", "Premultiplied Alpha", "Additive", "Subtractive", "Multiply"},
          static_cast<int>(loaded.blendMode), [](MaterialUVE& m, const int i) {
              m.blendMode = static_cast<Asset::MaterialBlendModeUVE>(std::clamp(i, 0, 5));
              m.isTransparent = m.blendMode != Asset::MaterialBlendModeUVE::Opaque;
          });
    combo("Cull", {"Back", "Front", "Disabled (two-sided)"}, static_cast<int>(loaded.cullMode), [](MaterialUVE& m, const int i) {
        m.cullMode = static_cast<Asset::MaterialCullModeUVE>(std::clamp(i, 0, 2));
    });
    combo("Depth Draw", {"Opaque Only", "Always", "Never", "Depth Pre-Pass"}, static_cast<int>(loaded.depthDrawMode),
          [](MaterialUVE& m, const int i) { m.depthDrawMode = static_cast<Asset::MaterialDepthDrawModeUVE>(std::clamp(i, 0, 3)); });
    combo("Billboard", {"Disabled", "Enabled", "Y Axis"}, static_cast<int>(loaded.billboardMode), [](MaterialUVE& m, const int i) {
        m.billboardMode = static_cast<Asset::MaterialBillboardModeUVE>(std::clamp(i, 0, 2));
    });
    slider("Alpha Cutoff", &MaterialUVE::alphaCutoff, 0.0F, 1.0F, "Discard pixels whose alpha is below this (0 never).");
    check("Unshaded", &MaterialUVE::unshaded, "Albedo only, no lighting.");
    check("Depth Test", &MaterialUVE::depthTestEnabled);
    check("Depth Write", &MaterialUVE::depthWriteEnabled);
    check("No Ambient Light", &MaterialUVE::disableAmbientLight);
    check("No Fog", &MaterialUVE::disableFog);
    {
        auto n = prism::make<prism::NumberField>(static_cast<float>(loaded.renderPriority), 1.0F, 0);
        n->onChange = [edit](const float v) { edit([&](MaterialUVE& m) { m.renderPriority = static_cast<std::int32_t>(std::lround(v)); }); };
        add("Render Priority", n, "Draw order among transparent materials.");
    }

    // Godot's two UV sets. UV1 drives every map; UV2 is the same coordinates with their own
    // transform, read by AO and Emission when they are "On UV2". Either can be triplanar instead:
    // projected along the object's (or with World Triplanar, the world's) three axes.
    section("UV1");
    vec2("Scale", &MaterialUVE::uvScale, 0.05F);
    vec2("Offset", &MaterialUVE::uvOffset, 0.01F);
    check("Triplanar", &MaterialUVE::triplanarEnabled, "Project the maps along the object's three axes instead of the mesh UVs.");
    check("World Triplanar", &MaterialUVE::worldSpaceTriplanar, "Project along the world's axes: the pattern stays put as the object moves.");
    slider("Sharpness", &MaterialUVE::triplanarBlendSharpness, 0.0F, 16.0F, "How hard the three projections meet at the edges.");

    section("UV2");
    vec2("Scale", &MaterialUVE::uv2Scale, 0.05F);
    vec2("Offset", &MaterialUVE::uv2Offset, 0.01F);
    check("Triplanar", &MaterialUVE::uv2Triplanar, "Project the UV2 maps along the object's three axes.");
    check("World Triplanar", &MaterialUVE::uv2WorldTriplanar, "Project along the world's axes.");
    slider("Sharpness", &MaterialUVE::uv2TriplanarSharpness, 0.0F, 16.0F, "How hard the three projections meet at the edges.");
    check("AO On UV2", &MaterialUVE::aoOnUv2, "The occlusion map reads UV2.");
    check("Emission On UV2", &MaterialUVE::emissionOnUv2, "The emission map reads UV2.");

    section("Advanced");
    slider("Rim", &MaterialUVE::rimAmount, 0.0F, 1.0F);
    color("Rim Tint", &MaterialUVE::rimTint);
    slider("Clearcoat", &MaterialUVE::clearcoatAmount, 0.0F, 1.0F);
    slider("Clearcoat Roughness", &MaterialUVE::clearcoatRoughness, 0.0F, 1.0F);
    slider("Anisotropy", &MaterialUVE::anisotropyAmount, -1.0F, 1.0F);
    slider("Subsurface", &MaterialUVE::subsurfaceStrength, 0.0F, 1.0F);
    slider("Refraction", &MaterialUVE::refractionAmount, 0.0F, 1.0F);
    texture("Height Map", &MaterialUVE::heightTexture);
    slider("Height Scale", &MaterialUVE::heightScale, 0.0F, 0.25F);
}

} // namespace UVE::Editor
