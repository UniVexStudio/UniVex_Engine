// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/marker_3d_uve.h"

namespace UVE::Scene {

/// Hard storage bound for one marker query. Fixed-size so a caller can hold the list without a
/// heap allocation of its own, and overflow is reported rather than pretending extra markers do
/// not exist. 32 is past any hand-authored scene's marker count; a caller that needs every marker
/// past the bound asks again after handling this page rather than growing the list per frame.
inline constexpr std::size_t kMaximumMarker3DQueryResultsUVE = 32U;

/// What one query looks for. `name` is matched exactly against the authored `markerName`; an empty
/// name means every live marker. Duplicate names are legal - they all come back, in content order.
struct Marker3DQueryUVE final {
    std::string name;
};

/// One marker as the query resolves it. `pose` is the object's WORLD pose composed with the
/// authored local offset (Marker3DPoseUVE's contract), so a caller can look through it or place
/// something at it without reading LocalPosition/Rotation itself.
struct Marker3DQueryResultUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    std::string name;
    Marker3DPoseUVE pose{};
};

/// The bounded result list. Results are sorted by (entity.index, entity.generation) ascending -
/// stable content order, never pool order - so a saved scene answers the same way on every
/// machine, and `results[0]` is the deterministic pick when several markers share a name.
struct Marker3DQueryResultsUVE final {
    std::array<Marker3DQueryResultUVE, kMaximumMarker3DQueryResultsUVE> results{};
    std::size_t count = 0U;
    bool overflowed = false;

    [[nodiscard]] bool HasAnyUVE() const noexcept { return count != 0U; }

    /// The first result in stable content order, or nullptr when the query found nothing.
    [[nodiscard]] const Marker3DQueryResultUVE* FirstUVE() const noexcept {
        return count == 0U ? nullptr : &results[0];
    }
};

/// Finds live markers from authored data.
///
/// A marker participates when it is enabled, passes its own validator, carries a world transform
/// (the scene graph has swept since it was authored), matches the query's name, and composes to a
/// finite pose. Everything else is skipped rather than approximated: a disabled marker is not a
/// viewpoint, and a marker whose authored rotation is degenerate is not handed out as garbage.
/// Skipping is per-marker, so one malformed marker never hides the rest of the scene's.
[[nodiscard]] Marker3DQueryResultsUVE QueryMarkers3DUVE(IEntityManagerUVE& entityManager,
                                                        const Marker3DQueryUVE& query);

/// The one-liner: the first live marker whose authored name is exactly `name`, in content order.
/// An empty name, a name that matches nothing, and a scene with no live markers all yield no value.
[[nodiscard]] std::optional<Marker3DQueryResultUVE> FindMarker3DUVE(IEntityManagerUVE& entityManager,
                                                                    std::string_view name);

} // namespace UVE::Scene
