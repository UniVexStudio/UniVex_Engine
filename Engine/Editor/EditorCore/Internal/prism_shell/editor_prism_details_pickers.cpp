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
//   Show in Content Browser
#include "editor_prism_details.h"

#include "editor.h"
#include "material_asset.h"

#include "prism/Application.h"
#include "prism/Menu.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] bool HasExtensionUVE(const std::vector<std::string>& kinds, const std::filesystem::path& path) {
    const std::string extension = LowerUVE(path.extension().string());
    return std::any_of(kinds.begin(), kinds.end(), [&](const std::string& kind) { return LowerUVE(kind) == extension; });
}

const std::vector<std::string> kImageSourcesUVE{".png", ".jpg", ".jpeg", ".bmp", ".tga"};
const std::vector<std::string> kModelSourcesUVE{".glb", ".gltf", ".fbx", ".obj"};

} // namespace

EditorPrismDetailsUVE::PickerSpecUVE EditorPrismDetailsUVE::PickerSpecForUVE(const std::vector<std::string>& extensions) {
    PickerSpecUVE spec;
    const auto has = [&](const char* kind) {
        return std::find(extensions.begin(), extensions.end(), std::string(kind)) != extensions.end();
    };
    if (has(".uvmodel")) {
        spec.title = "Mesh";
        spec.listed = {".uvmodel"};
        spec.loadable = {".uvmodel", ".glb", ".gltf", ".fbx", ".obj"};
    } else if (has(".uvmat") || has(".uvmaterial")) {
        spec.title = "Material";
        spec.listed = {".uvmat", ".uvmaterial"};
        spec.loadable = {".uvmat", ".uvmaterial"};
        spec.newMaterial = true;
    } else if (has(".uvtex") || has(".png")) {
        spec.title = "Texture";
        spec.listed = {".uvtex"};
        spec.loadable = {".uvtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    } else if (has(".uvmontagelib")) {
        spec.title = "Montage Library";
        spec.listed = {".uvmontagelib"};
        spec.loadable = {".uvmontagelib"};
    } else if (has(".uvmontage")) {
        spec.title = "Montage";
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

Asset::AssetGuidUVE EditorPrismDetailsUVE::PublishPickedUVE(const std::filesystem::path& absolute) {
    // What a slot holds is always the asset the runtime reads: a model source becomes a .uvmodel,
    // an image a .uvtex, anything else is registered as it is.
    if (HasExtensionUVE(kModelSourcesUVE, absolute) || LowerUVE(absolute.extension().string()) == ".uvmodel") {
        return m_editor.PublishModelSourceUVE(absolute);
    }
    if (HasExtensionUVE(kImageSourcesUVE, absolute)) {
        return m_editor.PublishTextureSourceUVE(absolute);
    }
    const Asset::AssetGuidUVE guid = m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
    m_editor.RefreshProjectFileIndexUVE();
    return guid;
}

std::filesystem::path EditorPrismDetailsUVE::PathOfGuidUVE(const Asset::AssetGuidUVE guid) const {
    if (guid == Asset::AssetGuidUVE{}) {
        return {};
    }
    return m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(guid);
}

void EditorPrismDetailsUVE::ShowPickerUVE(const PickerSpecUVE& spec, PickerActionsUVE actions, const prism::Rect anchor) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    // The pick lands on the object the menu was opened for, even if the selection moved meanwhile
    // (the file window stays open while the author looks around).
    const Scene::EntityUVE target = m_editor.GetSelectedEntityUVE();
    const auto onTarget = [this, target](std::function<void()> run) {
        return [this, target, run = std::move(run)] {
            if (target != Scene::kInvalidEntityUVE && m_editor.GetSelectedEntityUVE() != target &&
                m_editor.IsDocumentEntityUVE(target)) {
                m_editor.SelectEntityUVE(target);
            }
            run();
        };
    };
    auto shared = std::make_shared<PickerActionsUVE>(std::move(actions));

    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Empty", onTarget([shared] {
        if (shared->empty) {
            shared->empty();
        }
    })));
    if (spec.newMaterial) {
        // A new material in the Content Browser's folder, of the kind asked for, assigned at once.
        struct KindUVE final { const char* label; const char* stem; Asset::MaterialKindUVE kind; };
        static constexpr KindUVE kKinds[] = {
            {"New Standard Material", "M_Standard", Asset::MaterialKindUVE::Standard},
            {"New ORM Material", "M_ORM", Asset::MaterialKindUVE::Orm},
            {"New Shader Material", "M_Shader", Asset::MaterialKindUVE::Shader},
        };
        for (const KindUVE& kind : kKinds) {
            items.push_back(prism::MenuItem::Item(kind.label, onTarget([this, shared, kind] {
                const Asset::ProjectFileSnapshotUVE now = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const std::filesystem::path folder = now.contentRoot / m_editor.m_contentBrowserDirectory;
                std::error_code error;
                std::filesystem::create_directories(folder, error);
                const std::filesystem::path path = EditorUVE::MakeUniqueContentPathUVE(folder, kind.stem, ".uvmaterial");
                Asset::MaterialAssetUVE material{};
                material.kind = kind.kind;
                if (kind.kind == Asset::MaterialKindUVE::Orm) {
                    material.metallic = 1.0F;
                    material.occlusionStrength = 1.0F;
                } else if (kind.kind == Asset::MaterialKindUVE::Shader) {
                    material.useCustomShaderCode = true;
                }
                if (!Asset::SaveMaterialAssetUVE(material, path)) {
                    m_editor.m_contentStatusMessage = "Could not create a material here.";
                    return;
                }
                m_editor.RefreshProjectFileIndexUVE();
                if (shared->file) {
                    shared->file(path);
                }
            })));
        }
    }

    // The project's files of this kind.
    std::vector<prism::MenuItem> project;
    for (const Asset::ProjectFileEntryUVE& file : snapshot.entries) {
        if (file.kind == Asset::ProjectFileEntryKindUVE::Directory || !HasExtensionUVE(spec.listed, file.relativePath)) {
            continue;
        }
        const std::filesystem::path absolute = snapshot.contentRoot / file.relativePath;
        const bool isCurrent = !shared->current.empty() && shared->current == absolute;
        prism::MenuItem item = prism::MenuItem::Check(file.relativePath.generic_string(), isCurrent, onTarget([shared, absolute] {
            if (shared->file) {
                shared->file(absolute);
            }
        }));
        project.push_back(std::move(item));
    }
    items.push_back(prism::MenuItem::Sep());
    if (project.empty()) {
        items.push_back(prism::MenuItem::Item("No " + LowerUVE(spec.title) + " in this project yet", {}).disabled());
    } else if (project.size() <= 14U) {
        for (prism::MenuItem& item : project) {
            items.push_back(std::move(item));
        }
    } else {
        items.push_back(prism::MenuItem::Sub("In this project (" + std::to_string(project.size()) + ")", std::move(project)));
    }

    if (spec.shapes && shared->shape) {
        items.push_back(prism::MenuItem::Sep());
        struct ShapeUVE final { const char* label; Scene::PrimitiveMeshKindUVE kind; };
        static constexpr ShapeUVE kShapes[] = {
            {"Box", Scene::PrimitiveMeshKindUVE::Cube},
            {"Sphere", Scene::PrimitiveMeshKindUVE::UVSphere},
            {"Capsule", Scene::PrimitiveMeshKindUVE::Capsule},
            {"Cylinder", Scene::PrimitiveMeshKindUVE::Cylinder},
            {"Plane", Scene::PrimitiveMeshKindUVE::Plane},
        };
        for (const ShapeUVE& shape : kShapes) {
            const bool isCurrent = shared->currentShape.has_value() && *shared->currentShape == shape.kind;
            prism::MenuItem item = prism::MenuItem::Check(std::string(shape.label) + " (shape)", isCurrent,
                                                          onTarget([shared, kind = shape.kind] { shared->shape(kind); }));
            items.push_back(std::move(item));
        }
    }

    items.push_back(prism::MenuItem::Sep());
    const std::filesystem::path start = !shared->current.empty() ? shared->current.parent_path() : snapshot.contentRoot;
    items.push_back(prism::MenuItem::Item("Load...", [this, spec, shared, start, onTarget] {
        FilePickerRequestUVE request;
        request.mode = FilePickerModeUVE::Open;
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
        m_editor.OpenFilePickerUVE(std::move(request));
    }));
    items.push_back(prism::MenuItem::Item("Import...", [this, spec, shared, onTarget] {
        FilePickerRequestUVE request;
        request.mode = FilePickerModeUVE::Open;
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
        m_editor.OpenFilePickerUVE(std::move(request));
    }));
    prism::MenuItem show = prism::MenuItem::Item("Show in Content Browser", [this, shared] {
        const Asset::ProjectFileSnapshotUVE now = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(shared->current, now.contentRoot, error);
        for (const Asset::ProjectFileEntryUVE& file : now.entries) {
            if (!error && file.relativePath == relative) {
                m_editor.m_contentBrowserDirectory = file.relativePath.parent_path();
                m_editor.m_contentBrowserShelf.clear();
                m_editor.m_contentSelection.ClearUVE();
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
