// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_embedded_font_uve.h"

#include "ui_font_ttf_bytes.inc"

namespace UVE::UI {

std::span<const std::uint8_t> GetEmbeddedUIFontBytesUVE() noexcept {
    return std::span<const std::uint8_t>{uve_ui_runtime_font_ttf_bytes};
}

} // namespace UVE::UI
