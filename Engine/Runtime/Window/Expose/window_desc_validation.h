// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include "window_desc.h"
namespace UVE::Window {
/// Validates the value-only construction contract for a desktop WindowDesc.
/// This does not create a window, select a backend, or inspect platform lifecycle state.
[[nodiscard]] bool ValidateWindowDesc(const WindowDesc& desc) noexcept;
} // namespace UVE::Window
