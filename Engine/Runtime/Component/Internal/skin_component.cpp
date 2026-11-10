// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "skin_component.h"

namespace UVE::Scene {

[[nodiscard]] bool IsSkinComponentValidUVE(const SkinComponentUVE& skin) noexcept {
    return skin.bindCount == static_cast<std::uint32_t>(skin.boneBinds.size());
}

} // namespace UVE::Scene
