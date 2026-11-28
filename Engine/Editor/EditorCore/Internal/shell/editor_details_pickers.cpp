// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Details panel's asset pickers: one menu for every slot that names a file - a mesh, a
// material, a texture, an animation, a sound - in the same order everywhere:
//
//   Empty
//   (New Material)
//   ── In this project ──  the project's files of the slot's kind
//   ── Shapes ──           a MeshInstance3D's mesh only: Box, Sphere, Capsule, Cylinder, Plane
//   ──
//   Load...                the file window, on the slot's kinds (anywhere on disk)
//   Import...        the same, copied into a Content folder first
//   Show in Asset Table
#include "editor_details.h"

#include "editor.h"
#include "material_asset.h"

#include "prism/Application.h"
#include "prism/Menu.h"

#include <cstddef>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <functional>

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string Utf8Generic(const std::filesystem::path& path) {
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] bool HasExtension(const std::vector<std::string>& kinds, const std::filesystem::path& path) {
    const std::string extension = Lower(Utf8Generic(path.extension()));
    return std::any_of(kinds.begin(), kinds.end(), [&](const std::string& kind) { return Lower(kind) == extension; });
}

const std::vector<std::string> kImageSources{".png", ".jpg", ".jpeg", ".bmp", ".tga"};
const std::vector<std::string> kModelSources{".glb", ".gltf", ".fbx", ".obj"};

} // namespace

EditorDetails::PickerSpec EditorDetails::PickerSpecFor(const std::vector<std::string>& extensions) {
    PickerSpec spec;
    const auto has = [&](const char* kind) {
        return std::find(extensions.begin(), extensions.end(), std::string(kind)) != extensions.end();
    };
    if (has(".uvmodel")) {
        spec.title = "Mesh";
        // Content holds the model sources (.fbx, ...); their .uvmodel is in DerivedData, so list both.
        spec.listed = {".uvmodel", ".glb", ".gltf", ".fbx", ".obj"};
        spec.loadable = {".uvmodel", ".glb", ".gltf", ".fbx", ".obj"};
    } else if (has(".uvmat") || has(".uvmaterial")) {
        spec.title = "Material";
        spec.listed = {".uvmat", ".uvmaterial"};
        spec.loadable = {".uvmat", ".uvmaterial"};
        spec.newMaterial = true;
    } else if (has(".uvtex") || has(".png")) {
        spec.title = "Texture";
        // The project's images whether published or not: a source image is published when picked.
        spec.listed = {".uvtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga"};
        spec.loadable = {".uvtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    } else if (has(".uvmontagelib")) {
        spec.title = "Motion Montage Library";
        spec.listed = {".uvmontagelib"};
        spec.loadable = {".uvmontagelib"};
    } else if (has(".uvmontage")) {
        spec.title = "Motion Montage";
        spec.listed = {".uvmontage"};
        spec.loadable = {".uvmontage"};
    } else if (has(".uvaudio") || has(".wav")) {
        spec.title = "Sound";
        spec.listed = {".uvaudio", ".wav"};
        spec.loadable = {".uvaudio", ".wav"};
    } else {
        spec.title = extensions.empty() ? std::string("File") : extensions.front().substr(1);
        spec.listed = extensions;
        spec.loadable = extensions;
    }
    return spec;
}

Asset::AssetGuid EditorDetails::PublishPicked(const std::filesystem::path& absolute) {
    // What a slot holds is always the asset the runtime reads: a model source becomes a .uvmodel,
    // an image a .uvtex, anything else is registered as it is.
    if (HasExtension(kModelSources, absolute) || Lower(absolute.extension().string()) == ".uvmodel") {
        return m_editor.PublishModelSource(absolute);
    }
    if (HasExtension(kImageSources, absolute)) {
        return m_editor.PublishTextureSource(absolute);
    }
    const Asset::AssetGuid guid = m_editor.m_services->GetAssetDatabase().Register(absolute);
    m_editor.RefreshProjectFileIndex();
    return guid;
}

std::filesystem::path EditorDetails::PathOfGuid(const Asset::AssetGuid guid) const {
    if (guid == Asset::AssetGuid{}) {
        return {};
    }
    return m_editor.m_services->GetAssetDatabase().Resolve(guid);
}

void EditorDetails::ShowPicker(const PickerSpec& spec, PickerActions actions, const prism::Rect anchor) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    // The pick lands on the object the menu was opened for, even if the selection moved meanwhile
    // (the file window stays open while the author looks around).
    const Scene::Entity target = m_editor.GetSelectedEntity();
    const auto onTarget = [this, target](std::function<void()> run) {
        return [this, target, run = std::move(run)] {
            if (target != Scene::kInvalidEntity && m_editor.GetSelectedEntity() != target &&
                m_editor.IsDocumentEntity(target)) {
                m_editor.SelectEntity(target);
            }
            run();
        };
    };
    auto shared = std::make_shared<PickerActions>(std::move(actions));

    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Empty", onTarget([shared] {
        if (shared->empty) {
            shared->empty();
        }
    })));
    for (std::size_t index = 0U; index < shared->extras.size(); ++index) {
        items.push_back(prism::MenuItem::Item(shared->extras[index].first, onTarget([shared, index] { shared->extras[index].second(); })));
    }
    if (spec.newMaterial) {
        // A new material in the Asset Table's folder, of the kind asked for, assigned at once.
        struct Kind final { const char* label; const char* stem; Asset::MaterialKind kind; };
        static constexpr Kind kKinds[] = {
            {"New Standard Material", "M_Standard", Asset::MaterialKind::Standard},
            {"New ORM Material", "M_ORM", Asset::MaterialKind::Orm},
            {"New Shader Material", "M_Shader", Asset::MaterialKind::Shader},
        };
        for (const Kind& kind : kKinds) {
            items.push_back(prism::MenuItem::Item(kind.label, onTarget([this, shared, kind] {
                const Asset::ProjectFileSnapshot now = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
                const std::filesystem::path folder = now.contentRoot / m_editor.m_assetTableDirectory;
                std::error_code error;
                std::filesystem::create_directories(folder, error);
                const std::filesystem::path path = Editor::MakeUniqueContentPath(folder, kind.stem, ".uvmat");
                Asset::MaterialAsset material{};
                material.kind = kind.kind;
                if (kind.kind == Asset::MaterialKind::Orm) {
                    material.metallic = 1.0F;
                    material.occlusionStrength = 1.0F;
                } else if (kind.kind == Asset::MaterialKind::Shader) {
                    material.useCustomShaderCode = true;
                }
                if (!Asset::SaveMaterialAsset(material, path)) {
                    m_editor.m_contentStatusMessage = "Could not create a material here.";
                    return;
                }
                m_editor.RefreshProjectFileIndex();
                if (shared->file) {
                    shared->file(path);
                }
            })));
        }
    }

    // The project's files of this kind. A source image already published beside itself is listed once,
    // as its .uvtex.
    std::set<std::string> published;
    for (const Asset::ProjectFileEntry& file : snapshot.entries) {
        if (HasExtension({".uvtex"}, file.relativePath)) {
            published.insert((file.relativePath.parent_path() / file.relativePath.stem()).generic_string());
        }
    }
    std::vector<prism::MenuItem> project;
    for (const Asset::ProjectFileEntry& file : snapshot.entries) {
        if (file.kind == Asset::ProjectFileEntryKind::Directory || !HasExtension(spec.listed, file.relativePath)) {
            continue;
        }
        if (!HasExtension({".uvtex"}, file.relativePath) && HasExtension({".png", ".jpg", ".jpeg", ".bmp", ".tga"}, file.relativePath) &&
            published.contains(Utf8Generic(file.relativePath.parent_path() / file.relativePath.stem()))) {
            continue;
        }
        const bool modelSource = Editor::IsModelSourcePath(file.relativePath);
        if (modelSource && spec.title == "Mesh") {
            // An animation-only file has no mesh to show.
            if (const EditorModelSourceInfo* info = m_editor.FindModelSourceInfo(file.relativePath); info != nullptr && info->animationOnly) {
                continue;
            }
        }
        const std::filesystem::path absolute = snapshot.contentRoot / file.relativePath;
        const bool isCurrent = !shared->current.empty() &&
                               (shared->current == absolute ||
                                (modelSource && shared->current == m_editor.GetImportedModelPath(file.relativePath)));
        prism::MenuItem item = prism::MenuItem::Check(Utf8Generic(file.relativePath), isCurrent, onTarget([shared, absolute] {
            if (shared->file) {
                shared->file(absolute);
            }
        }));
        project.push_back(std::move(item));
    }
    items.push_back(prism::MenuItem::Sep());
    if (project.empty()) {
        items.push_back(prism::MenuItem::Item("No " + Lower(spec.title) + " in this project yet", {}).disabled());
    } else if (project.size() <= 14U) {
        for (prism::MenuItem& item : project) {
            items.push_back(std::move(item));
        }
    } else {
        items.push_back(prism::MenuItem::Sub("In this project (" + std::to_string(project.size()) + ")", std::move(project)));
    }

    if (spec.shapes && shared->shape) {
        items.push_back(prism::MenuItem::Sep());
        struct Shape final { const char* label; Scene::PrimitiveMeshKind kind; };
        static constexpr Shape kShapes[] = {
            {"Box", Scene::PrimitiveMeshKind::Cube},
            {"Sphere", Scene::PrimitiveMeshKind::UVSphere},
            {"Capsule", Scene::PrimitiveMeshKind::Capsule},
            {"Cylinder", Scene::PrimitiveMeshKind::Cylinder},
            {"Plane", Scene::PrimitiveMeshKind::Plane},
        };
        for (const Shape& shape : kShapes) {
            const bool isCurrent = shared->currentShape.has_value() && *shared->currentShape == shape.kind;
            prism::MenuItem item = prism::MenuItem::Check(std::string(shape.label) + " (shape)", isCurrent,
                                                          onTarget([shared, kind = shape.kind] { shared->shape(kind); }));
            items.push_back(std::move(item));
        }
    }

    items.push_back(prism::MenuItem::Sep());
    const std::filesystem::path start = !shared->current.empty() ? shared->current.parent_path() : snapshot.contentRoot;
    items.push_back(prism::MenuItem::Item("Load...", [this, spec, shared, start, onTarget] {
        FilePickerRequest request;
        request.mode = FilePickerMode::Open;
        request.title = "Load " + spec.title;
        request.extensions = spec.loadable;
        request.startDirectory = start;
        request.onPick = [shared, onTarget](const std::filesystem::path& picked) {
            onTarget([shared, picked] {
                if (shared->file) {
                    shared->file(picked);
                }
            })();
        };
        m_editor.OpenFilePicker(std::move(request));
    }));
    items.push_back(prism::MenuItem::Item("Import...", [this, spec, shared, onTarget] {
        FilePickerRequest request;
        request.mode = FilePickerMode::Open;
        request.title = "Import " + spec.title;
        request.extensions = spec.loadable;
        request.quickImport = true;
        // Importing usually starts from outside the project: the person's own folders.
#if defined(_WIN32)
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        request.startDirectory = home != nullptr ? std::filesystem::path{home} : std::filesystem::path{};
        request.onPick = [shared, onTarget](const std::filesystem::path& picked) {
            onTarget([shared, picked] {
                if (shared->file) {
                    shared->file(picked);
                }
            })();
        };
        m_editor.OpenFilePicker(std::move(request));
    }));
    prism::MenuItem show = prism::MenuItem::Item("Show in Asset Table", [this, shared] {
        const Asset::ProjectFileSnapshot now = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(shared->current, now.contentRoot, error);
        for (const Asset::ProjectFileEntry& file : now.entries) {
            if (!error && file.relativePath == relative) {
                m_editor.m_assetTableDirectory = file.relativePath.parent_path();
                m_editor.m_assetTableShelf.clear();
                m_editor.m_contentSelection.Clear();
                m_editor.m_selectedProjectFile = file;
                break;
            }
        }
    });
    show.enabled = !shared->current.empty();
    items.push_back(std::move(show));
    prism::showMenu(std::move(items), anchor, prism::PopupPlacement::Below);
}

} // namespace UVE::Editor
