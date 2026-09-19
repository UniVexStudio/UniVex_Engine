// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/name_component_uve.h"

#include <cstddef>
#include <string>

namespace UVE::Scene {

[[nodiscard]] bool IsNameComponentValidUVE(const NameComponentUVE& component) noexcept {
    return component.name.size() <= kMaximumEntityNameBytesUVE &&
           component.name.find('\0') == std::string::npos;
}

} // namespace UVE::Scene
