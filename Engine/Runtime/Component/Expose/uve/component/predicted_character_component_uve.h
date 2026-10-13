// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {

/// Empty tag component marking a Character3D as driven by the network predictor instead of the
/// local mover. StepCharacter3DUVE() refuses a tagged body (PredictedElsewhere) and leaves it
/// exactly where it is, so the predictor's TickPredictedPawnUVE()/SyncPredictedPawnUVE() own its
/// motion alone and the two never double-step it. The tag's lifetime is the predictor's: the app
/// adds it when it spawns the NetworkPredictionUVE for the pawn and removes it when prediction
/// stops - the local mover resumes on the next step, recomputing its contact state from the
/// world as it always does.
/// Runtime-only, like the session it mirrors: never serialized, never inspected, and not part
/// of any object definition.
/// Thread-safety: empty value type; trivially safe to copy/move.
struct PredictedCharacterComponentUVE final {};

} // namespace UVE::Scene
