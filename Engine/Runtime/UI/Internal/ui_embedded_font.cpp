// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_embedded_font.h"

#include <array>
#include "ui_font_ttf_bytes.inc"

namespace UVE::UI {

std::span<const std::uint8_t> GetEmbeddedUIFontBytesUVE() noexcept {
    return std::span<const std::uint8_t>{uve_ui_runtime_font_ttf_bytes};
}

} // namespace UVE::UI
