// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_viewport_host.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "camera_preview_viewport.h"
#include "entity_viewport.h"
#include "level_viewport.h"
#include "montage_viewport.h"
#include "retarget_viewport.h"
#include "vector2.h"
#include "vfx_viewport.h"
#include "widget_preview_viewport.h"

namespace UVE::App::Viewports {

struct EditorViewportHostUVE::ImplUVE final {
    ImplUVE(Editor::EditorUVE& editorRef, Core::EngineCoreUVE& engineRef)
        : editor(editorRef), engine(engineRef) {}

    Editor::EditorUVE& editor;
    Core::EngineCoreUVE& engine;
    std::unique_ptr<LevelViewportUVE> level;
    std::unique_ptr<CameraPreviewViewportUVE> cameraPreview;
    std::unique_ptr<EntityViewportUVE> entity;
    std::unique_ptr<WidgetPreviewViewportUVE> widgetPreview;
    std::unique_ptr<RetargetViewportUVE> retarget;
    std::unique_ptr<MontageViewportUVE> montage;
    std::unique_ptr<VfxViewportUVE> vfx;
};

EditorViewportHostUVE::EditorViewportHostUVE(Editor::EditorUVE& editor, Core::EngineCoreUVE& engine)
    : m_impl(std::make_unique<ImplUVE>(editor, engine)) {}

EditorViewportHostUVE::~EditorViewportHostUVE() {
    if (m_impl != nullptr) {
        m_impl->editor.ClearEditorViewportsUVE();
    }
}

void EditorViewportHostUVE::BindToEditorUVE(Editor::EditorUVE& editor) {
    ImplUVE* const impl = m_impl.get();
    Editor::EditorUVE::EditorViewportsUVE viewports{};

    viewports.level = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                             const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->level == nullptr || impl->level->ServicesUVE() != &services) {
            impl->level.reset();
            impl->level = std::make_unique<LevelViewportUVE>(impl->editor, impl->engine, services);
        }
        return impl->level->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    viewports.cameraPreview = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                                     const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->cameraPreview == nullptr || impl->cameraPreview->ServicesUVE() != &services) {
            impl->cameraPreview.reset();
            impl->cameraPreview = std::make_unique<CameraPreviewViewportUVE>(impl->editor, services);
        }
        return impl->cameraPreview->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    viewports.entity = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                              const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->entity == nullptr || impl->entity->ServicesUVE() != &services) {
            impl->entity.reset();
            impl->entity = std::make_unique<EntityViewportUVE>(impl->editor, services);
        }
        return impl->entity->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    viewports.widgetPreview = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                                     const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->widgetPreview == nullptr || impl->widgetPreview->ServicesUVE() != &services) {
            impl->widgetPreview.reset();
            impl->widgetPreview = std::make_unique<WidgetPreviewViewportUVE>(impl->editor, services);
        }
        return impl->widgetPreview->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    viewports.retarget = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                                const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->retarget == nullptr || impl->retarget->ServicesUVE() != &services) {
            impl->retarget.reset();
            impl->retarget = std::make_unique<RetargetViewportUVE>(impl->editor, services);
        }
        return impl->retarget->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    viewports.montage = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                               const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->montage == nullptr || impl->montage->ServicesUVE() != &services) {
            impl->montage.reset();
            impl->montage = std::make_unique<MontageViewportUVE>(impl->editor, services);
        }
        return impl->montage->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    viewports.vfx = [impl](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                           const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) -> std::uint64_t {
        Core::EngineServicesUVE& services = impl->editor.GetActiveServicesUVE();
        if (impl->vfx == nullptr || impl->vfx->ServicesUVE() != &services) {
            impl->vfx.reset();
            impl->vfx = std::make_unique<VfxViewportUVE>(impl->editor, services);
        }
        return impl->vfx->RenderUVE(availableSize, outUsedSize, overlayState);
    };

    editor.SetEditorViewportsUVE(std::move(viewports));
}

void EditorViewportHostUVE::UnbindFromEditorUVE(Editor::EditorUVE& editor) {
    editor.ClearEditorViewportsUVE();
}

void EditorViewportHostUVE::ForgetServicesUVE(const Core::EngineServicesUVE& services) {
    const auto forget = [&services](auto& viewport) {
        if (viewport != nullptr && viewport->ServicesUVE() == &services) {
            viewport.reset();
        }
    };
    forget(m_impl->level);
    forget(m_impl->cameraPreview);
    forget(m_impl->entity);
    forget(m_impl->widgetPreview);
    forget(m_impl->retarget);
    forget(m_impl->montage);
    forget(m_impl->vfx);
}

} // namespace UVE::App::Viewports
