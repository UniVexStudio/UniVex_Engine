// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ai/utility_ai_uve.h"

#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] float SanitizedInputUVE(const float input) noexcept {
    if (!std::isfinite(input) || input <= 0.0F) {
        return 0.0F;
    }
    return input >= 1.0F ? 1.0F : input;
}

[[nodiscard]] bool IsUsableIdUVE(const std::string_view id, const std::size_t maximumBytes) noexcept {
    return !id.empty() && id.size() <= maximumBytes;
}

[[nodiscard]] bool IsKnownCurveUVE(const AiResponseCurveUVE curve) noexcept {
    return static_cast<std::uint8_t>(curve) <= static_cast<std::uint8_t>(AiResponseCurveUVE::InverseQuadratic);
}

} // namespace

float EvaluateAiResponseCurveUVE(const AiResponseCurveUVE curve, const float input) noexcept {
    const float x = SanitizedInputUVE(input);
    switch (curve) {
        case AiResponseCurveUVE::Linear:
            return x;
        case AiResponseCurveUVE::Quadratic:
            return x * x;
        case AiResponseCurveUVE::InverseLinear:
            return 1.0F - x;
        case AiResponseCurveUVE::InverseQuadratic:
            return (1.0F - x) * (1.0F - x);
    }
    return x;
}

bool IsBlackboardComponentValidUVE(const BlackboardComponentUVE& board) noexcept {
    if (board.entries.size() > kMaximumAiBlackboardEntriesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < board.entries.size(); ++index) {
        const AiBlackboardEntryUVE& entry = board.entries[index];
        if (!IsUsableIdUVE(entry.key, kMaximumAiInputIdBytesUVE) || !std::isfinite(entry.value)) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (board.entries[earlier].key == entry.key) {
                return false;
            }
        }
    }
    if (board.vectors.size() > kMaximumAiBlackboardVectorsUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < board.vectors.size(); ++index) {
        const AiBlackboardVectorEntryUVE& entry = board.vectors[index];
        if (!IsUsableIdUVE(entry.key, kMaximumAiInputIdBytesUVE) || !std::isfinite(entry.value.x) ||
            !std::isfinite(entry.value.y) || !std::isfinite(entry.value.z)) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (board.vectors[earlier].key == entry.key) {
                return false;
            }
        }
    }
    return true;
}

bool IsAiBrainComponentValidUVE(const AiBrainComponentUVE& brain) noexcept {
    if (brain.actions.size() > kMaximumAiActionsUVE || !std::isfinite(brain.hysteresis) ||
        brain.hysteresis < 0.0F || !std::isfinite(brain.currentScore)) {
        return false;
    }
    for (std::size_t index = 0U; index < brain.actions.size(); ++index) {
        const AiActionUVE& action = brain.actions[index];
        if (!IsUsableIdUVE(action.actionId, kMaximumAiActionIdBytesUVE) ||
            !std::isfinite(action.baseScore) || action.baseScore < 0.0F || action.baseScore > 1.0F ||
            action.considerations.size() > kMaximumAiConsiderationsUVE) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (brain.actions[earlier].actionId == action.actionId) {
                return false;
            }
        }
        for (const AiConsiderationUVE& consideration : action.considerations) {
            if (!IsUsableIdUVE(consideration.inputId, kMaximumAiInputIdBytesUVE) ||
                !IsKnownCurveUVE(consideration.curve) || !std::isfinite(consideration.weight) ||
                consideration.weight < 0.0F) {
                return false;
            }
        }
    }
    return true;
}

float GetBlackboardValueUVE(const BlackboardComponentUVE& board, const std::string_view key) noexcept {
    for (const AiBlackboardEntryUVE& entry : board.entries) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return 0.0F;
}

bool SetBlackboardValueUVE(BlackboardComponentUVE& board, std::string key, const float value) {
    if (!IsUsableIdUVE(key, kMaximumAiInputIdBytesUVE) || !std::isfinite(value)) {
        return false;
    }
    for (AiBlackboardEntryUVE& entry : board.entries) {
        if (entry.key == key) {
            entry.value = value;
            return true;
        }
    }
    if (board.entries.size() >= kMaximumAiBlackboardEntriesUVE) {
        return false;
    }
    board.entries.push_back(AiBlackboardEntryUVE{std::move(key), value});
    return true;
}

bool RemoveBlackboardValueUVE(BlackboardComponentUVE& board, const std::string_view key) {
    for (auto entry = board.entries.begin(); entry != board.entries.end(); ++entry) {
        if (entry->key == key) {
            board.entries.erase(entry);
            return true;
        }
    }
    return false;
}

Math::Vector3UVE GetBlackboardVectorUVE(const BlackboardComponentUVE& board,
                                        const std::string_view key) noexcept {
    for (const AiBlackboardVectorEntryUVE& entry : board.vectors) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return Math::Vector3UVE{};
}

bool SetBlackboardVectorUVE(BlackboardComponentUVE& board, std::string key, const Math::Vector3UVE value) {
    if (!IsUsableIdUVE(key, kMaximumAiInputIdBytesUVE) || !std::isfinite(value.x) ||
        !std::isfinite(value.y) || !std::isfinite(value.z)) {
        return false;
    }
    for (AiBlackboardVectorEntryUVE& entry : board.vectors) {
        if (entry.key == key) {
            entry.value = value;
            return true;
        }
    }
    if (board.vectors.size() >= kMaximumAiBlackboardVectorsUVE) {
        return false;
    }
    board.vectors.push_back(AiBlackboardVectorEntryUVE{std::move(key), value});
    return true;
}

bool RemoveBlackboardVectorUVE(BlackboardComponentUVE& board, const std::string_view key) {
    for (auto entry = board.vectors.begin(); entry != board.vectors.end(); ++entry) {
        if (entry->key == key) {
            board.vectors.erase(entry);
            return true;
        }
    }
    return false;
}

float ScoreAiActionUVE(const AiActionUVE& action, const BlackboardComponentUVE& board) noexcept {
    double score = SanitizedInputUVE(action.baseScore);
    for (const AiConsiderationUVE& consideration : action.considerations) {
        const float value = GetBlackboardValueUVE(board, consideration.inputId);
        const double curved = EvaluateAiResponseCurveUVE(consideration.curve, value);
        const double weight =
            std::isfinite(consideration.weight) && consideration.weight >= 0.0F ? consideration.weight : 0.0;
        score *= std::pow(curved, weight);
    }
    return static_cast<float>(score);
}

AiActionSelectionUVE SelectAiActionUVE(const AiBrainComponentUVE& brain,
                                       const BlackboardComponentUVE& board) {
    const AiActionUVE* best = nullptr;
    float bestScore = 0.0F;
    for (const AiActionUVE& action : brain.actions) {
        const float score = ScoreAiActionUVE(action, board);
        if (best == nullptr || score > bestScore) {
            best = &action;
            bestScore = score;
        }
    }
    AiActionSelectionUVE selection;
    if (best == nullptr || bestScore <= 0.0F) {
        selection.changed = !brain.currentAction.empty();
        return selection;
    }
    const float hysteresis =
        std::isfinite(brain.hysteresis) && brain.hysteresis >= 0.0F ? brain.hysteresis : 0.0F;
    float incumbentScore = -1.0F;
    if (!brain.currentAction.empty()) {
        for (const AiActionUVE& action : brain.actions) {
            if (action.actionId == brain.currentAction) {
                incumbentScore = ScoreAiActionUVE(action, board);
                break;
            }
        }
    }
    if (best->actionId == brain.currentAction) {
        selection.actionId = best->actionId;
        selection.score = bestScore;
        return selection;
    }
    if (incumbentScore < 0.0F || bestScore > incumbentScore + hysteresis) {
        selection.actionId = best->actionId;
        selection.score = bestScore;
        selection.changed = true;
        return selection;
    }
    selection.actionId = brain.currentAction;
    selection.score = incumbentScore;
    return selection;
}

} // namespace UVE::Scene
