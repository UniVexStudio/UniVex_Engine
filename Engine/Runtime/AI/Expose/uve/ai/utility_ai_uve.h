// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/math/vector3_uve.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Utility AI --------------------------------------------------------------------------
//
// NPC decision-making as scoring, not scripting: every action states what it wants through
// considerations ("flee wants low health, scored steeply"), each frame every action is scored
// against the blackboard, and the best one wins. A blackboard is working memory - floats the game,
// scripts, and (once it lands) perception write; a brain is the scored action set that reads it.
//
// SCORING, STATED ONCE. An action's score is its base score times every consideration's score
// raised to its weight: a zero anywhere vetoes the action, a weight of zero ignores that
// consideration, and higher weights sharpen it. Inputs read 0..1 (clamped); a consideration the
// board does not feed reads zero and vetoes under product scoring - feed everything your actions
// read. Selection keeps the incumbent unless a challenger beats its FRESH score by more than the
// hysteresis, which is what stops an NPC dithering between two equally-scored actions. When
// nothing scores above zero, nothing is selected: an NPC with no valid action does nothing rather
// than a zero-scored something.

inline constexpr std::size_t kMaximumAiActionsUVE = 32U;
inline constexpr std::size_t kMaximumAiConsiderationsUVE = 8U;
inline constexpr std::size_t kMaximumAiBlackboardEntriesUVE = 64U;
inline constexpr std::size_t kMaximumAiBlackboardVectorsUVE = 32U;
inline constexpr std::size_t kMaximumAiActionIdBytesUVE = 64U;
inline constexpr std::size_t kMaximumAiInputIdBytesUVE = 64U;

/// How a 0..1 input becomes a 0..1 score.
enum class AiResponseCurveUVE : std::uint8_t {
    /// y = x: desire tracks the input (patrol eagerness against boredom).
    Linear = 0,
    /// y = x^2: only a high input scores (an attack that wants a full magazine).
    Quadratic,
    /// y = 1 - x: desire fades as the input grows.
    InverseLinear,
    /// y = (1 - x)^2: urgent only when the input is nearly gone (fleeing on low health).
    InverseQuadratic,
};

struct AiBlackboardEntryUVE final {
    std::string key;
    float value = 0.0F;

    [[nodiscard]] bool operator==(const AiBlackboardEntryUVE&) const = default;
};

struct AiBlackboardVectorEntryUVE final {
    std::string key;
    Math::Vector3UVE value{};

    [[nodiscard]] bool operator==(const AiBlackboardVectorEntryUVE&) const = default;
};

/// Working memory: named floats plus named positions, first match wins on read within each list,
/// duplicates invalid. Floats score brains; vectors steer bodies (a seek target is a position, not
/// a score). The two lists are separate namespaces - a float and a vector may share a key.
struct BlackboardComponentUVE final {
    std::vector<AiBlackboardEntryUVE> entries;
    std::vector<AiBlackboardVectorEntryUVE> vectors;

    [[nodiscard]] bool operator==(const BlackboardComponentUVE&) const = default;
};

struct AiConsiderationUVE final {
    std::string inputId;
    AiResponseCurveUVE curve = AiResponseCurveUVE::Linear;
    /// Exponent on this consideration's score: 1 weighs it plainly, 0 ignores it, higher
    /// sharpens it. Never negative.
    float weight = 1.0F;

    [[nodiscard]] bool operator==(const AiConsiderationUVE&) const = default;
};

struct AiActionUVE final {
    std::string actionId;
    /// Prior multiplied into the score: 1 takes the considerations at face value, lower dims the
    /// action, 0 disables it without deleting it. An action with no considerations scores this,
    /// which is what makes board-less priority brains work.
    float baseScore = 1.0F;
    std::vector<AiConsiderationUVE> considerations;

    [[nodiscard]] bool operator==(const AiActionUVE&) const = default;
};

struct AiBrainComponentUVE final {
    std::vector<AiActionUVE> actions;
    /// A challenger takes over only by beating the incumbent's fresh score by more than this.
    float hysteresis = 0.1F;
    /// Selection state, written by the sync; never saved. A loaded brain rethinks on its first frame.
    std::string currentAction;
    float currentScore = 0.0F;

    [[nodiscard]] bool operator==(const AiBrainComponentUVE&) const = default;
};

struct AiActionSelectionUVE final {
    std::string actionId;
    float score = 0.0F;
    bool changed = false;

    [[nodiscard]] bool operator==(const AiActionSelectionUVE&) const = default;
};

/// `curve` applied to `input`, sanitized to 0..1 first (non-finite reads as zero).
[[nodiscard]] float EvaluateAiResponseCurveUVE(AiResponseCurveUVE curve, float input) noexcept;

/// Entries and vectors within cap, keys usable and unique within each list, values finite.
[[nodiscard]] bool IsBlackboardComponentValidUVE(const BlackboardComponentUVE& board) noexcept;
/// Actions within cap with usable unique ids, base scores in 0..1, considerations within cap with
/// usable input ids, known curves and finite non-negative weights, hysteresis finite and
/// non-negative, selection score finite.
[[nodiscard]] bool IsAiBrainComponentValidUVE(const AiBrainComponentUVE& brain) noexcept;

/// The first entry for `key`, or 0 when the board has none.
[[nodiscard]] float GetBlackboardValueUVE(const BlackboardComponentUVE& board, std::string_view key) noexcept;

/// Sets `key`, replacing the first existing entry or appending when absent. False for an empty or
/// oversized key, a non-finite value, or a full board - though updating an existing key always
/// succeeds, full or not.
[[nodiscard]] bool SetBlackboardValueUVE(BlackboardComponentUVE& board, std::string key, float value);

/// Removes the first entry for `key`. False when the board has none.
[[nodiscard]] bool RemoveBlackboardValueUVE(BlackboardComponentUVE& board, std::string_view key);

/// The first vector for `key`, or zero when the board has none.
[[nodiscard]] Math::Vector3UVE GetBlackboardVectorUVE(const BlackboardComponentUVE& board,
                                                      std::string_view key) noexcept;

/// Sets the vector for `key`, replacing or appending like the float setter. False for an empty or
/// oversized key, a non-finite vector, or a full vector list - though updating an existing key
/// always succeeds, full or not.
[[nodiscard]] bool SetBlackboardVectorUVE(BlackboardComponentUVE& board, std::string key,
                                          Math::Vector3UVE value);

/// Removes the first vector for `key`. False when the board has none.
[[nodiscard]] bool RemoveBlackboardVectorUVE(BlackboardComponentUVE& board, std::string_view key);

/// Base score times every consideration's curved score raised to its weight. Never NaN.
[[nodiscard]] float ScoreAiActionUVE(const AiActionUVE& action, const BlackboardComponentUVE& board) noexcept;

/// Scores every action and applies hysteresis against the incumbent's fresh score: keeps it on a
/// tie or a within-margin challenge, switches past the margin, drops to nothing when no action
/// scores above zero. Pure - the caller applies `changed` and publishes the event.
[[nodiscard]] AiActionSelectionUVE SelectAiActionUVE(const AiBrainComponentUVE& brain,
                                                     const BlackboardComponentUVE& board);

} // namespace UVE::Scene
