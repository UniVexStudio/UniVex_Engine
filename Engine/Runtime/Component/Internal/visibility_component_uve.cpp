// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/visibility_component_uve.h"

namespace UVE::Scene {

bool IsVisibilityComponentValidUVE(const VisibilityComponentUVE&) noexcept {
    // Two plain bools: there is no representable invalid state. Defined out of line rather than
    // made constexpr in the header so the component matches every other one's shape - a validator
    // that lives in a different place for one component is a trap for generic code.
    return true;
}

} // namespace UVE::Scene
