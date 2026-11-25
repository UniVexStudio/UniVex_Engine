// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Details panel's material section: a material file's settings, edited in place under the slot
// that uses it - the same fields, presets and save path as the previous Inspector's inline material
// editor (DrawMaterialPbrEditorInline).
#include "editor_details.h"
#include "editor_touch_controls.h"

#include "texture_asset.h"

#include "editor_asset_thumbnail.h"

#include "asset_guid.h"
#include "color.h"
#include "i_project_file_index.h"
#include "material_asset.h"
#include "material_graph_asset.h"

#include "prism/BlueprintGraph.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <system_error>

namespace UVE::Editor {
namespace {

using Material = Asset::MaterialAsset;

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

[[nodiscard]] prism::Color ToColor(const Math::Color& c, const float alpha = 1.0F) {
    const auto toByte = [](const float v) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
    };
    return prism::Color(toByte(Math::LinearToDisplay(c.r)), toByte(Math::LinearToDisplay(c.g)),
                        toByte(Math::LinearToDisplay(c.b)), toByte(alpha));
}

[[nodiscard]] Math::Color FromColor(const prism::Color c) {
    return Math::Color{Math::DisplayToLinear(static_cast<float>(c.r) / 255.0F),
                          Math::DisplayToLinear(static_cast<float>(c.g) / 255.0F),
                          Math::DisplayToLinear(static_cast<float>(c.b) / 255.0F)};
}

} // namespace

/// The separate Metallic and Roughness maps (and the Occlusion map) packed into the one ORM texture
/// the lit shader samples, kept beside the material as <material>_ORM.uvtex. An ORM the author
/// picked themselves supplies any channel a separate map does not. Picking a first map sets its
/// value to 1, since the shader multiplies the map by it (a 0 Metallic would hide the map).
void EditorDetails::PackSeparateMaps(Asset::MaterialAsset& material, const std::filesystem::path& materialPath,
                                           const bool metallicPicked, const bool roughnessPicked) {
    Editor& editor = m_editor;
    Asset::IAssetDatabase& database = editor.m_services->GetAssetDatabase();
    if (material.metallicTexture == Asset::AssetGuid{} && material.roughnessTexture == Asset::AssetGuid{}) {
        return;
    }
    if (metallicPicked && material.metallicTexture != Asset::AssetGuid{}) {
        material.metallic = 1.0F;
    }
    if (roughnessPicked && material.roughnessTexture != Asset::AssetGuid{}) {
        material.roughness = 1.0F;
    }
    const std::filesystem::path ormPath = materialPath.parent_path() / (materialPath.stem().string() + "_ORM.uvtex");
    const auto load = [&database](const Asset::AssetGuid guid, Asset::TextureAsset& out) {
        if (guid == Asset::AssetGuid{}) {
            return false;
        }
        const std::filesystem::path path = database.Resolve(guid);
        return !path.empty() && Asset::LoadTextureAssetSource(path, out);
    };
    // A hand-picked ORM (not the one made here) gives the channels no separate map replaces.
    Asset::TextureAsset ownOrm;
    const bool hasOwnOrm = material.metallicRoughnessTexture != Asset::AssetGuid{} &&
                           database.Resolve(material.metallicRoughnessTexture) != ormPath &&
                           load(material.metallicRoughnessTexture, ownOrm);
    Asset::TextureAsset metallic;
    Asset::TextureAsset roughness;
    const bool hasMetallic = load(material.metallicTexture, metallic);
    const bool hasRoughness = load(material.roughnessTexture, roughness);
    // A separate map's own reading (its channel, invert and strength) is baked into the grey value
    // the packer takes from red; its tiling and offset are the ORM map's, which the shader applies.
    const auto applySettings = [](Asset::TextureAsset& map, const Asset::MaterialMapSettings& settings) {
        const std::size_t source = settings.channel == Asset::MaterialTextureChannel::Rgb
                                       ? 0U
                                       : static_cast<std::size_t>(settings.channel);
        const float strength = std::clamp(settings.strength, 0.0F, 1.0F);
        for (std::size_t at = 0U; at + 3U < map.pixels.size(); at += 4U) {
            float value = static_cast<float>(std::to_integer<int>(map.pixels[at + source])) / 255.0F;
            if (settings.invert) {
                value = 1.0F - value;
            }
            value = 1.0F + (value - 1.0F) * strength;
            map.pixels[at] = static_cast<std::byte>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
        }
    };
    if (hasMetallic) {
        applySettings(metallic, material.Map(Asset::MaterialMap::Metallic));
    }
    if (hasRoughness) {
        applySettings(roughness, material.Map(Asset::MaterialMap::Roughness));
    }
    // A channel of the hand-picked ORM as a grey map of its own.
    const auto channelOf = [&ownOrm](const std::size_t channel) {
        Asset::TextureAsset grey = ownOrm;
        grey.mipLevels.clear();
        for (std::size_t at = 0U; at + 3U < grey.pixels.size(); at += 4U) {
            grey.pixels[at] = ownOrm.pixels[at + channel];
        }
        return grey;
    };
    Asset::TextureAsset roughnessFromOrm;
    Asset::TextureAsset metallicFromOrm;
    if (hasOwnOrm) {
        roughnessFromOrm = channelOf(1U);
        metallicFromOrm = channelOf(2U);
    }
    Asset::TextureAsset orm;
    if (!Asset::PackOrmTexture(nullptr, hasRoughness ? &roughness : (hasOwnOrm ? &roughnessFromOrm : nullptr),
                                  hasMetallic ? &metallic : (hasOwnOrm ? &metallicFromOrm : nullptr), orm) ||
        !Asset::SaveTextureAsset(orm, ormPath)) {
        editor.m_contentStatusMessage = "Could not pack the Metallic and Roughness maps (they must be images the editor can read).";
        return;
    }
    editor.RefreshProjectFileIndex();
    const Asset::AssetGuid guid = database.Register(ormPath);
    if (guid != Asset::AssetGuid{}) {
        material.metallicRoughnessTexture = guid;
    }
}

void EditorDetails::AddMaterialSection(const std::filesystem::path& absolutePath, const std::string& slotLabel) {
    Material loaded{};
    std::error_code ec;
    if (absolutePath.empty() || !std::filesystem::is_regular_file(absolutePath, ec) ||
        !Asset::LoadMaterialAsset(absolutePath, loaded)) {
        return;
    }
    const bool writable = m_editor.IsAuthoringCommandAllowed();
    static_cast<void>(slotLabel);
    const std::string title = absolutePath.stem().string();

    // Every edit: read the file as it is now, change the one field, write it back, and let the
    // thumbnails and the renderer pick up the new file (as the previous Inspector did).
    Editor& editor = m_editor;
    const auto edit = [&editor, absolutePath](const std::function<void(Material&)>& change) {
        Material material{};
        if (!Asset::LoadMaterialAsset(absolutePath, material)) {
            return;
        }
        change(material);
        if (Asset::SaveMaterialAsset(material, absolutePath)) {
            editor.InvalidateMaterialThumbnailCache(absolutePath);
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
    const auto slider = [&](const char* label, float Material::*field, const float lo, const float hi,
                            const char* tip = "") {
        auto s = prism::make<prism::Slider>(lo, hi, loaded.*field, 0.0F);
        s->setDecimals(2);
        s->onChange = [edit, field](const float v) { edit([&](Material& m) { m.*field = v; }); };
        add(label, s, tip);
    };
    const auto check = [&](const char* label, bool Material::*field, const char* tip = "") {
        auto c = prism::make<prism::CheckBox>("", loaded.*field);
        c->onChange = [edit, field](const bool v) { edit([&](Material& m) { m.*field = v; }); };
        add(label, c, tip);
    };
    const auto color = [&](const char* label, Math::Color Material::*field, const char* tip = "") {
        auto f = prism::make<prism::ColorField>(ToColor(loaded.*field));
        f->onChange = [edit, field](const prism::Color c) { edit([&](Material& m) { m.*field = FromColor(c); }); };
        add(label, f, tip);
    };
    const auto vec2 = [&](const char* label, Math::Vector2 Material::*field, const float step) {
        auto row = prism::hbox(4.0F);
        for (int axis = 0; axis < 2; ++axis) {
            const float value = axis == 0 ? (loaded.*field).x : (loaded.*field).y;
            auto n = prism::make<prism::NumberField>(value, step, 3);
            n->setAxisLabel(axis == 0 ? "X" : "Y", axis == 0 ? prism::Color(206, 72, 72) : prism::Color(92, 170, 70));
            n->onChange = [edit, field, axis](const float v) {
                edit([&](Material& m) { (axis == 0 ? (m.*field).x : (m.*field).y) = v; });
            };
            n->setEnabled(writable);
            row->add(n, prism::Slot{1.0F});
        }
        m_grid->addRow(category, label, row);
    };
    const auto combo = [&](const char* label, std::vector<std::string> items, const int current,
                           std::function<void(Material&, int)> apply, const char* tip = "") {
        auto c = prism::make<prism::ComboBox>(std::move(items), current);
        c->onChange = [edit, apply](const int i) { edit([&](Material& m) { apply(m, i); }); };
        add(label, c, tip);
    };
    // A texture slot: the map's own preview and name beside its picker (any image of the project, a
    // published .uvtex or a source image, which is published on use), a drop target for the Asset
    // Library, and under it how the map is read (MaterialMapSettings) once it holds one.
    constexpr Asset::MaterialMap kNoMap = Asset::MaterialMap::Count;
    const auto texture = [&](const char* label, Asset::AssetGuid Material::*field, const Asset::MaterialMap map,
                             const char* tip = "") {
        const bool packed = map == Asset::MaterialMap::Metallic || map == Asset::MaterialMap::Roughness;
        const auto nameOf = [this](const Asset::AssetGuid guid) {
            if (guid == Asset::AssetGuid{}) {
                return std::string("Empty");
            }
            const std::filesystem::path path = PathOfGuid(guid);
            return path.empty() ? std::string("Missing texture") : path.stem().string();
        };
        auto b = prism::make<prism::Button>(nameOf(loaded.*field), prism::Icon::None, prism::ButtonStyle::Default);
        auto thumb = prism::make<AssetThumbnail>(m_editor, AssetThumbnail::Kind::Texture);
        thumb->setAsset(PathOfGuid(loaded.*field));
        std::weak_ptr<prism::Button> weak = b;
        std::weak_ptr<AssetThumbnail> weakThumb = thumb;
        // What the slot shows, right away: the name and the picture of the map it now holds.
        const auto show = [this, weak, weakThumb, nameOf](const Asset::AssetGuid guid) {
            if (auto button = weak.lock()) {
                button->setText(nameOf(guid));
            }
            if (auto t = weakThumb.lock()) {
                t->setAsset(PathOfGuid(guid));
            }
            // A slot that gains or loses its map gains or loses its settings rows.
            m_rebuildRequested = true;
        };
        const std::function<void(const std::filesystem::path&)> useFile = [this, edit, field, show, path = absolutePath](
                                                                              const std::filesystem::path& absolute) {
            const Asset::AssetGuid guid = PublishPicked(absolute);
            if (guid == Asset::AssetGuid{}) {
                m_editor.m_contentStatusMessage = "Could not read " + absolute.filename().string() + " as a texture.";
                return;
            }
            edit([&](Material& m) {
                const bool metallicPicked = field == &Material::metallicTexture && m.metallicTexture == Asset::AssetGuid{};
                const bool roughnessPicked = field == &Material::roughnessTexture && m.roughnessTexture == Asset::AssetGuid{};
                m.*field = guid;
                if (field == &Material::metallicTexture || field == &Material::roughnessTexture ||
                    field == &Material::metallicRoughnessTexture) {
                    PackSeparateMaps(m, path, metallicPicked, roughnessPicked);
                }
            });
            show(guid);
        };
        b->onClick = [this, edit, field, weak, show, useFile, current = loaded.*field, absolutePath] {
            PickerActions actions;
            actions.current = PathOfGuid(current);
            actions.empty = [edit, field, show, path = absolutePath, this] {
                edit([&](Material& m) {
                    m.*field = Asset::AssetGuid{};
                    PackSeparateMaps(m, path, false, false);
                });
                show(Asset::AssetGuid{});
            };
            actions.file = useFile;
            if (current != Asset::AssetGuid{}) {
                actions.extras.emplace_back("Texture Settings...", [this, current] { ShowTextureSettings(PathOfGuid(current)); });
            }
            if (auto button = weak.lock()) {
                ShowPicker(PickerSpecFor({".uvtex", ".png"}), std::move(actions), button->rect());
            }
        };
        auto slot = prism::hbox(6.0F);
        slot->add(thumb, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
        slot->add(b, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Top});
        auto row = std::make_shared<Row>();
        row->sync = [weakThumb](const void*) {
            // A preview still being made shows up as soon as it is ready.
            if (auto t = weakThumb.lock()) {
                t->refresh();
            }
        };
        row->editor = slot.get();
        m_rows.push_back(row);
        b->setEnabled(writable);
        if (tip[0] != '\0') {
            b->setTooltip(tip);
        }
        const prism::Ref<prism::Widget> dropTarget =
            WithAssetDrop([this] { return m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot; }, slot,
                             PickerSpecFor({".uvtex", ".png"}).loadable, useFile);
        m_grid->addRow(category, label, dropTarget, tip);

        // How the map is read, once the slot holds one.
        if (map == kNoMap || loaded.*field == Asset::AssetGuid{}) {
            return;
        }
        const Asset::MaterialMapSettings settings = loaded.Map(map);
        // A setting of a separate Metallic / Roughness map is baked into the ORM map it is packed into.
        const auto editMap = [edit, map, packed, this, path = absolutePath](const std::function<void(Asset::MaterialMapSettings&)>& change) {
            edit([&](Material& m) {
                change(m.Map(map));
                if (packed) {
                    PackSeparateMaps(m, path, false, false);
                }
            });
        };
        const std::string prefix = std::string("  ");
        if (!packed) {
            auto tiling = prism::hbox(4.0F);
            auto offset = prism::hbox(4.0F);
            for (int axis = 0; axis < 2; ++axis) {
                const auto field2 = [&](const Math::Vector2 value, const float step, const bool isTiling) {
                    auto n = prism::make<prism::NumberField>(axis == 0 ? value.x : value.y, step, 3);
                    n->setAxisLabel(axis == 0 ? "X" : "Y", axis == 0 ? prism::Color(206, 72, 72) : prism::Color(92, 170, 70));
                    n->onCommit = [editMap, axis, isTiling](const float v) {
                        editMap([&](Asset::MaterialMapSettings& s) {
                            Math::Vector2& target = isTiling ? s.tiling : s.offset;
                            (axis == 0 ? target.x : target.y) = v;
                        });
                    };
                    n->setEnabled(writable);
                    return n;
                };
                tiling->add(field2(settings.tiling, 0.05F, true), prism::Slot{1.0F});
                offset->add(field2(settings.offset, 0.01F, false), prism::Slot{1.0F});
            }
            m_grid->addRow(category, prefix + "Tiling", tiling, "How many times the map repeats across the surface (on top of the material's UV scale).");
            m_grid->addRow(category, prefix + "Offset", offset, "Slides the map across the surface.");
            auto uv2 = prism::make<prism::CheckBox>("", settings.onUv2);
            uv2->onChange = [editMap](const bool v) { editMap([&](Asset::MaterialMapSettings& s) { s.onUv2 = v; }); };
            add((prefix + "On UV2").c_str(), uv2, "Read this map with the UV2 set instead of UV1.");
        }
        auto channel = prism::make<prism::ComboBox>(std::vector<std::string>{"Red", "Green", "Blue", "Alpha", "RGB"},
                                                    static_cast<int>(settings.channel));
        channel->onChange = [editMap](const int i) {
            editMap([&](Asset::MaterialMapSettings& s) { s.channel = static_cast<Asset::MaterialTextureChannel>(std::clamp(i, 0, 4)); });
        };
        add((prefix + "Channel").c_str(), channel, "RGB reads the map as it is; a single channel reads it as grey (a packed or mask map).");
        auto invert = prism::make<prism::CheckBox>("", settings.invert);
        invert->onChange = [editMap](const bool v) { editMap([&](Asset::MaterialMapSettings& s) { s.invert = v; }); };
        add((prefix + "Invert").c_str(), invert, "Reads the map inverted (white becomes black).");
        auto strength = prism::make<prism::Slider>(0.0F, 1.0F, settings.strength, 0.0F);
        strength->setDecimals(2);
        strength->onChange = [editMap](const float v) { editMap([&](Asset::MaterialMapSettings& s) { s.strength = v; }); };
        add((prefix + "Strength").c_str(), strength, "How strongly the map acts: 0 is as if there were no map, 1 the map as authored.");
    };

    section("Albedo");
    color("Color", &Material::albedoColor, "The surface's base colour (multiplies the albedo texture).");
    slider("Alpha", &Material::albedoAlpha, 0.0F, 1.0F, "Opacity, used by the alpha blend modes and the cutoff.");
    texture("Texture", &Material::albedoTexture, Asset::MaterialMap::Albedo);
    check("Vertex Color", &Material::useVertexColorAsAlbedo, "Use the mesh's vertex colours as albedo.");

    section("Normal");
    texture("Normal Map", &Material::normalTexture, Asset::MaterialMap::Normal);
    slider("Strength", &Material::normalScale, 0.0F, 4.0F, "1 is the map as authored; 0 flattens it.");
    check("Flip Green", &Material::normalFlipGreen, "For maps authored with the other green-channel convention.");

    section("Surface");
    slider("Metallic", &Material::metallic, 0.0F, 1.0F);
    slider("Roughness", &Material::roughness, 0.0F, 1.0F);
    slider("Specular", &Material::dielectricSpecular, 0.0F, 1.0F, "Reflectance of non-metals (0.5 is about 4%).");
    texture("ORM / Metal-Rough", &Material::metallicRoughnessTexture, Asset::MaterialMap::MetallicRoughness, "Packed map: G roughness, B metallic.");
    texture("Metallic Map", &Material::metallicTexture, Asset::MaterialMap::Metallic, "A grey map (white = metal), packed into the ORM map the shader reads.");
    texture("Roughness Map", &Material::roughnessTexture, Asset::MaterialMap::Roughness, "A grey map (white = rough), packed into the ORM map the shader reads.");
    texture("Occlusion Map", &Material::aoTexture, Asset::MaterialMap::Occlusion);
    slider("Occlusion", &Material::occlusionStrength, 0.0F, 1.0F, "How much the occlusion map darkens cavities.");

    section("Emission");
    color("Color", &Material::emissiveColor);
    slider("Energy", &Material::emissiveEnergy, 0.0F, 16.0F);
    texture("Texture", &Material::emissiveTexture, Asset::MaterialMap::Emissive);

    section("Rendering");
    combo("Blend", {"Opaque", "Alpha Blend", "Premultiplied Alpha", "Additive", "Subtractive", "Multiply"},
          static_cast<int>(loaded.blendMode), [](Material& m, const int i) {
              m.blendMode = static_cast<Asset::MaterialBlendMode>(std::clamp(i, 0, 5));
              m.isTransparent = m.blendMode != Asset::MaterialBlendMode::Opaque;
          });
    combo("Cull", {"Back", "Front", "Disabled (two-sided)"}, static_cast<int>(loaded.cullMode), [](Material& m, const int i) {
        m.cullMode = static_cast<Asset::MaterialCullMode>(std::clamp(i, 0, 2));
    });
    combo("Depth Draw", {"Opaque Only", "Always", "Never", "Depth Pre-Pass"}, static_cast<int>(loaded.depthDrawMode),
          [](Material& m, const int i) { m.depthDrawMode = static_cast<Asset::MaterialDepthDrawMode>(std::clamp(i, 0, 3)); });
    combo("Billboard", {"Disabled", "Enabled", "Y Axis"}, static_cast<int>(loaded.billboardMode), [](Material& m, const int i) {
        m.billboardMode = static_cast<Asset::MaterialBillboardMode>(std::clamp(i, 0, 2));
    });
    slider("Alpha Cutoff", &Material::alphaCutoff, 0.0F, 1.0F, "Discard pixels whose alpha is below this (0 never).");
    check("Unshaded", &Material::unshaded, "Albedo only, no lighting.");
    check("Depth Test", &Material::depthTestEnabled);
    check("Depth Write", &Material::depthWriteEnabled);
    check("No Ambient Light", &Material::disableAmbientLight);
    check("No Fog", &Material::disableFog);
    {
        auto n = prism::make<prism::NumberField>(static_cast<float>(loaded.renderPriority), 1.0F, 0);
        n->onChange = [edit](const float v) { edit([&](Material& m) { m.renderPriority = static_cast<std::int32_t>(std::lround(v)); }); };
        add("Render Priority", n, "Draw order among transparent materials.");
    }

    // Godot's two UV sets. UV1 drives every map; UV2 is the same coordinates with their own
    // transform, read by AO and Emission when they are "On UV2". Either can be triplanar instead:
    // projected along the object's (or with World Triplanar, the world's) three axes.
    section("UV1");
    vec2("Scale", &Material::uvScale, 0.05F);
    vec2("Offset", &Material::uvOffset, 0.01F);
    check("Triplanar", &Material::triplanarEnabled, "Project the maps along the object's three axes instead of the mesh UVs.");
    check("World Triplanar", &Material::worldSpaceTriplanar, "Project along the world's axes: the pattern stays put as the object moves.");
    slider("Sharpness", &Material::triplanarBlendSharpness, 0.0F, 16.0F, "How hard the three projections meet at the edges.");

    section("UV2");
    vec2("Scale", &Material::uv2Scale, 0.05F);
    vec2("Offset", &Material::uv2Offset, 0.01F);
    check("Triplanar", &Material::uv2Triplanar, "Project the UV2 maps along the object's three axes.");
    check("World Triplanar", &Material::uv2WorldTriplanar, "Project along the world's axes.");
    slider("Sharpness", &Material::uv2TriplanarSharpness, 0.0F, 16.0F, "How hard the three projections meet at the edges.");
    check("AO On UV2", &Material::aoOnUv2, "The occlusion map reads UV2.");
    check("Emission On UV2", &Material::emissionOnUv2, "The emission map reads UV2.");

    section("Advanced");
    slider("Rim", &Material::rimAmount, 0.0F, 1.0F);
    color("Rim Tint", &Material::rimTint);
    slider("Clearcoat", &Material::clearcoatAmount, 0.0F, 1.0F);
    slider("Clearcoat Roughness", &Material::clearcoatRoughness, 0.0F, 1.0F);
    slider("Anisotropy", &Material::anisotropyAmount, -1.0F, 1.0F);
    slider("Subsurface", &Material::subsurfaceStrength, 0.0F, 1.0F);
    slider("Refraction", &Material::refractionAmount, 0.0F, 1.0F);
    texture("Height Map", &Material::heightTexture, kNoMap);
    slider("Height Scale", &Material::heightScale, 0.0F, 0.25F);

    {
        auto openGraphBtn = prism::make<prism::Button>("Open Visual Material Graph...");
        openGraphBtn->onClick = [this, materialPath = absolutePath, edit] {
            const std::filesystem::path graphPath =
                materialPath.parent_path() / (materialPath.stem().string() + ".uvmatgraph");
            auto graphAsset = std::make_shared<Asset::MaterialGraphAsset>();
            if (!Asset::LoadMaterialGraphAsset(graphPath, *graphAsset)) {
                *graphAsset = Asset::CreateDefaultPbrMaterialGraph();
                graphAsset->name = materialPath.stem().string();
            }
            auto body = prism::vbox(6.0F);
            auto statusLbl = prism::make<prism::Label>("Shader graph ready");
            statusLbl->setColor(prism::ThemeColor(&prism::Theme::textDim));
            auto bpGraph = prism::make<prism::BlueprintGraph>();
            bpGraph->setPreset(prism::BlueprintGraphPreset::MaterialShader);
            auto graphBox = prism::make<prism::SizeBox>();
            graphBox->setWidthOverride(780.0F);
            graphBox->setHeightOverride(440.0F);
            graphBox->setContent(bpGraph);
            body->add(graphBox);
            body->add(statusLbl);
            if (prism::Ref<prism::Widget> controls = MakeTouchControlsButton()) {   // touch: Undo, Ctrl, ...
                body->add(controls, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            }

            const auto compiled = Asset::CompileMaterialGraphToGlsl(*graphAsset);
            if (compiled.ok) {
                statusLbl->setText("Compiled OK (" + std::to_string(graphAsset->nodes.size()) + " nodes, " +
                                   std::to_string(compiled.glslSource.size()) + " bytes GLSL)");
            } else if (!compiled.errors.empty()) {
                statusLbl->setText("Compile error: " + compiled.errors.front());
            }

            prism::showDialog(
                "Visual Material Graph — " + materialPath.filename().string(), body,
                {{"Close", false, nullptr},
                 {"Save & Compile", true,
                  [this, graphPath, materialPath, graphAsset, edit] {
                      (void)Asset::SaveMaterialGraphAsset(*graphAsset, graphPath);
                      const auto res = Asset::CompileMaterialGraphToGlsl(*graphAsset);
                      if (res.ok) {
                          const std::filesystem::path glslPath =
                              materialPath.parent_path() / (materialPath.stem().string() + "_graph.glsl");
                          std::ofstream glslOut(glslPath, std::ios::trunc);
                          if (glslOut) {
                              glslOut << res.glslSource;
                          }
                          if (m_editor.m_services != nullptr) {
                              const Asset::AssetGuid shaderGuid =
                                  m_editor.m_services->GetAssetDatabase().Register(glslPath);
                              edit([shaderGuid](Material& m) {
                                  m.customShader = shaderGuid;
                                  m.useCustomShaderCode = true;
                              });
                          }
                          m_editor.m_contentStatusMessage =
                              "Saved & compiled material graph " + graphPath.filename().string();
                      } else if (!res.errors.empty()) {
                          m_editor.m_contentStatusMessage = "Material graph error: " + res.errors.front();
                      }
                  }}},
                820.0F);
        };
        add("Shader Graph", openGraphBtn, "Author this material visually in the node-based PBR Material Graph.");
    }
}

} // namespace UVE::Editor

namespace UVE::Editor {

/// The texture's import, edited and applied by importing its source image again: colour space and
/// usage (a colour map, a normal map or a data mask), mipmaps and their filter, the largest size it
/// is kept at, compression, and the per-image fixes (invert green for the other normal-map
/// convention, premultiplied alpha). The choice is kept in the source's `.uvimport` sidecar, so a
/// later re-import (or a fresh pick of the same image) imports it the same way.
void EditorDetails::ShowTextureSettings(const std::filesystem::path& texturePath) {
    const std::filesystem::path source = Editor::FindTextureSource(texturePath);
    if (source.empty()) {
        auto note = prism::make<prism::Label>("'" + texturePath.filename().string() +
                                              "' has no source image beside it, so it cannot be imported again.");
        prism::showDialog("Texture Settings", note, {{"Close", true, nullptr}});
        return;
    }
    auto settings = std::make_shared<Asset::TextureImportSettings>(Editor::LoadTextureImportSettings(source));
    auto body = prism::vbox(6.0F);
    const auto row = [&body](const char* label, prism::Ref<prism::Widget> control, const char* tip) {
        auto line = prism::hbox(8.0F);
        auto name = prism::make<prism::Label>(label);
        auto nameBox = prism::make<prism::SizeBox>();
        nameBox->setWidthOverride(150.0F);
        nameBox->setContent(name);
        line->add(nameBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        control->setTooltip(tip);
        line->add(control, prism::Slot{1.0F});
        body->add(line);
    };
    auto heading = prism::make<prism::Label>(source.filename().string());
    heading->setColor(prism::ThemeColor(&prism::Theme::textDim));
    body->add(heading);

    auto memEstimateLabel = prism::make<prism::Label>("");
    memEstimateLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    const auto updateMemEstimate = [settings, weakMem = std::weak_ptr<prism::Label>(memEstimateLabel)] {
        if (auto lbl = weakMem.lock()) {
            const std::size_t bytes = Asset::EstimateImportedTextureMemoryBytes(
                settings->maxResolutionClamp, settings->maxResolutionClamp, *settings);
            char buf[64];
            if (bytes >= 1024U * 1024U) {
                std::snprintf(buf, sizeof(buf), "%.2f MiB (at max size)",
                              static_cast<double>(bytes) / (1024.0 * 1024.0));
            } else {
                std::snprintf(buf, sizeof(buf), "%.1f KiB (at max size)", static_cast<double>(bytes) / 1024.0);
            }
            lbl->setText(buf);
        }
    };
    updateMemEstimate();

    auto colorSpace = prism::make<prism::ComboBox>(std::vector<std::string>{"sRGB (colour)", "Linear (data)"},
                                                   settings->colorSpace == Asset::TextureAssetColorSpace::Linear ? 1 : 0);
    colorSpace->onChange = [settings](const int i) {
        settings->colorSpace = i == 1 ? Asset::TextureAssetColorSpace::Linear : Asset::TextureAssetColorSpace::Srgb;
    };
    row("Color Space", colorSpace, "sRGB for pictures and albedo; Linear for normal, roughness, metallic and other data maps.");
    const int usageIndex = settings->usage == Asset::TextureUsage::Normal ? 1 : settings->usage == Asset::TextureUsage::Data ? 2 : 0;
    auto usage = prism::make<prism::ComboBox>(std::vector<std::string>{"Color", "Normal Map", "Mask / Data"}, usageIndex);
    usage->onChange = [settings, weakSpace = std::weak_ptr<prism::ComboBox>(colorSpace)](const int i) {
        settings->usage = i == 1 ? Asset::TextureUsage::Normal : i == 2 ? Asset::TextureUsage::Data : Asset::TextureUsage::Color;
        // A normal map or a mask is data: read linear.
        if (i != 0) {
            settings->colorSpace = Asset::TextureAssetColorSpace::Linear;
            if (auto space = weakSpace.lock()) {
                space->setSelected(1);
            }
        }
    };
    row("Usage", usage, "What the image holds: a picture, a tangent-space normal map, or a mask of values.");
    auto mipmaps = prism::make<prism::CheckBox>("", settings->generateMipmaps);
    mipmaps->onChange = [settings, updateMemEstimate](const bool v) {
        settings->generateMipmaps = v;
        updateMemEstimate();
    };
    row("Generate Mipmaps", mipmaps, "Smaller copies for distant surfaces: no shimmering, faster sampling.");
    auto mipFilter = prism::make<prism::ComboBox>(std::vector<std::string>{"Smooth (box)", "Sharp (nearest)"},
                                                  settings->mipFilter == Asset::TextureMipmapFilter::Nearest ? 1 : 0);
    mipFilter->onChange = [settings](const int i) {
        settings->mipFilter = i == 1 ? Asset::TextureMipmapFilter::Nearest : Asset::TextureMipmapFilter::Box;
    };
    row("Mipmap Filter", mipFilter, "Sharp keeps hard pixel edges (pixel art); Smooth averages.");
    static const std::vector<std::uint32_t> kSizes{256U, 512U, 1024U, 2048U, 4096U, 8192U};
    int sizeIndex = 4;
    for (std::size_t i = 0U; i < kSizes.size(); ++i) {
        if (kSizes[i] == settings->maxResolutionClamp) {
            sizeIndex = static_cast<int>(i);
        }
    }
    auto maxSize = prism::make<prism::ComboBox>(std::vector<std::string>{"256", "512", "1024", "2048", "4096", "8192"}, sizeIndex);
    maxSize->onChange = [settings, updateMemEstimate](const int i) {
        settings->maxResolutionClamp = kSizes[static_cast<std::size_t>(std::clamp(i, 0, 5))];
        updateMemEstimate();
    };
    row("Max Size", maxSize, "A larger image is scaled down to this on import (memory and download size).");
    const int compressionIndex = settings->compressionMode == Asset::TextureCompressionMode::BasisETC1S   ? 1
                                 : settings->compressionMode == Asset::TextureCompressionMode::BasisUASTC ? 2
                                                                                                              : 0;
    auto compression = prism::make<prism::ComboBox>(std::vector<std::string>{"None", "Small (ETC1S)", "High Quality (UASTC)"},
                                                    compressionIndex);
    compression->onChange = [settings, updateMemEstimate](const int i) {
        settings->compressionMode = i == 1 ? Asset::TextureCompressionMode::BasisETC1S
                                    : i == 2 ? Asset::TextureCompressionMode::BasisUASTC
                                             : Asset::TextureCompressionMode::None;
        updateMemEstimate();
    };
    row("Compression", compression, "Stored compressed: far smaller on disk and in memory, at some quality.");
    row("Est. Memory", memEstimateLabel, "Estimated GPU/asset memory budget for the chosen resolution, mipmaps, and compression.");
    auto invertGreen = prism::make<prism::CheckBox>("", settings->invertGreenChannel);
    invertGreen->onChange = [settings](const bool v) { settings->invertGreenChannel = v; };
    row("Invert Green", invertGreen, "For normal maps made with the other green-channel convention.");
    auto premultiply = prism::make<prism::CheckBox>("", settings->premultiplyAlpha);
    premultiply->onChange = [settings](const bool v) { settings->premultiplyAlpha = v; };
    row("Premultiply Alpha", premultiply, "Multiplies colour by alpha on import (no dark fringes on cut-out edges).");

    prism::showDialog("Texture Settings", body,
                      {{"Cancel", false, nullptr},
                       {"Apply", true, [this, source, settings] {
                            if (!m_editor.ReimportTexture(source, *settings)) {
                                m_editor.m_contentStatusMessage = "Could not import " + source.filename().string() + " with these settings.";
                                return;
                            }
                            m_editor.m_contentStatusMessage = "Imported " + source.filename().string() + " again.";
                            m_rebuildRequested = true;
                        }}},
                      460.0F);
}

} // namespace UVE::Editor
