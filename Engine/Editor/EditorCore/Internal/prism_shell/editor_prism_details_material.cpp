// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Details panel's material section: a material file's settings, edited in place under the slot
// that uses it - the same fields, presets and save path as the previous Inspector's inline material
// editor (DrawMaterialPbrEditorInlineUVE).
#include "editor_prism_details.h"

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

[[nodiscard]] bool IsTextureFileUVE(const std::filesystem::path& path) {
    static constexpr std::array<const char*, 9> kExtensions{".png", ".jpg", ".jpeg", ".tga", ".bmp",
                                                            ".hdr", ".exr", ".ktx2", ".uvtex"};
    const std::string e = LowerExtensionUVE(path);
    return std::any_of(kExtensions.begin(), kExtensions.end(), [&](const char* x) { return e == x; });
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
        std::string text = "(None)";
        if (loaded.*field != Asset::AssetGuidUVE{}) {
            text = "(missing texture)";
            for (const Asset::ProjectFileEntryUVE& file : snapshot.entries) {
                if (file.registeredAssetGuid.has_value() && *file.registeredAssetGuid == loaded.*field) {
                    text = file.relativePath.filename().string();
                    break;
                }
            }
        }
        auto b = prism::make<prism::Button>(text, prism::Icon::NodeTexture, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = b;
        b->onClick = [this, edit, field, weak] {
            const Asset::ProjectFileSnapshotUVE snap = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            std::vector<prism::MenuItem> items;
            items.push_back(prism::MenuItem::Item("(None)", [edit, field, weak] {
                edit([&](MaterialUVE& m) { m.*field = Asset::AssetGuidUVE{}; });
                if (auto button = weak.lock()) {
                    button->setText("(None)");
                }
            }));
            items.push_back(prism::MenuItem::Sep());
            for (const Asset::ProjectFileEntryUVE& file : snap.entries) {
                if (!file.registeredAssetGuid.has_value() || !IsTextureFileUVE(file.relativePath)) {
                    continue;
                }
                const Asset::AssetGuidUVE guid = *file.registeredAssetGuid;
                const std::string name = file.relativePath.filename().string();
                items.push_back(prism::MenuItem::Item(file.relativePath.generic_string(), [edit, field, guid, weak, name] {
                    edit([&](MaterialUVE& m) { m.*field = guid; });
                    if (auto button = weak.lock()) {
                        button->setText(name);
                    }
                }, "", prism::Icon::NodeTexture));
            }
            if (items.size() == 2U) {
                items.push_back(prism::MenuItem::Item("No textures in this project yet (import some)", {}).disabled());
            }
            if (auto button = weak.lock()) {
                prism::showMenu(std::move(items), button->rect(), prism::PopupPlacement::Below);
            }
        };
        add(label, b, tip);
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

    section("UV");
    vec2("Scale", &MaterialUVE::uvScale, 0.05F);
    vec2("Offset", &MaterialUVE::uvOffset, 0.01F);

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
