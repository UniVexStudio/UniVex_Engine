// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "particle_system_asset.h"

namespace UVE::VFX {

/// Built-in authored particle system templates available from the asset creator and tests.
enum class ParticleTemplateKindUVE : std::uint8_t {
    Empty = 0,
    Fountain,
    Sparks,
    Fire,
    Smoke,
};

struct ParticleTemplateInfoUVE final {
    ParticleTemplateKindUVE kind = ParticleTemplateKindUVE::Empty;
    std::string_view name;
    std::string_view description;
};

/// Returns metadata for all built-in particle system templates.
[[nodiscard]] const std::vector<ParticleTemplateInfoUVE>& GetParticleTemplatesUVE() noexcept;

/// Builds a complete, valid `ParticleSystemAssetUVE` for `kind`.
[[nodiscard]] Asset::ParticleSystemAssetUVE CreateParticleTemplateUVE(ParticleTemplateKindUVE kind);

} // namespace UVE::VFX
