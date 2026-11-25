// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "particle_system_asset.h"

namespace UVE::VFX {

/// Built-in authored particle system templates available from the asset creator and tests.
enum class ParticleTemplateKind : std::uint8_t {
    Empty = 0,
    Fountain,
    Sparks,
    Fire,
    Smoke,
};

struct ParticleTemplateInfo final {
    ParticleTemplateKind kind = ParticleTemplateKind::Empty;
    std::string_view name;
    std::string_view description;
};

/// Returns metadata for all built-in particle system templates.
[[nodiscard]] const std::vector<ParticleTemplateInfo>& GetParticleTemplates() noexcept;

/// Builds a complete, valid `ParticleSystemAsset` for `kind`.
[[nodiscard]] Asset::ParticleSystemAsset CreateParticleTemplate(ParticleTemplateKind kind);

} // namespace UVE::VFX
