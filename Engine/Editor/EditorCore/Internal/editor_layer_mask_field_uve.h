// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/editor/editor_layer_mask_uve.h"

namespace UVE::Editor {

/// What the person did with a layer mask field this frame.
enum class LayerMaskFieldEventUVE {
    None,
    /// `mask` holds the new value.
    Changed,
    /// They asked to edit the layer names.
    EditNames,
};

/// The editor's layer mask field: a combo whose preview names the layers in the mask, opening a
/// checklist of all 32 layers by name with All, None and Invert, and a way to the names
/// themselves. Every tick is applied at once.
[[nodiscard]] LayerMaskFieldEventUVE DrawLayerMaskFieldUVE(const char* id, std::uint32_t& mask,
                                                           const LayerNamesUVE& names);

} // namespace UVE::Editor
