// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_internal_entity_component.h"

namespace UVE::Scene {

bool IsEditorInternalEntityComponentValid(
    const EditorInternalEntityComponent& /*component*/) noexcept {
    return true;
}

bool IsEditorInternalEntityTagPresent(
    const EditorInternalEntityComponent* const component) noexcept {
    return component != nullptr;
}

bool ShouldIncludeInDocumentHierarchy(
    const EditorInternalEntityComponent* const component) noexcept {
    return !IsEditorInternalEntityTagPresent(component);
}

bool ShouldSerializeDocumentEntity(
    const EditorInternalEntityComponent* const component) noexcept {
    return !IsEditorInternalEntityTagPresent(component);
}

bool ShouldClearWithDocumentScene(
    const EditorInternalEntityComponent* const component) noexcept {
    return !IsEditorInternalEntityTagPresent(component);
}

} // namespace UVE::Scene
