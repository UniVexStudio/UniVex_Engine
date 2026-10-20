// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/skin_component_uve.h"

namespace UVE::Scene {

[[nodiscard]] bool IsSkinComponentValidUVE(const SkinComponentUVE& skin) noexcept {
    return skin.bindCount == static_cast<std::uint32_t>(skin.boneBinds.size());
}

} // namespace UVE::Scene
