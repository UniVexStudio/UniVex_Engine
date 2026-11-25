// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "utility_ai.h"

#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] float SanitizedInput(const float input) noexcept {
    if (!std::isfinite(input) || input <= 0.0F) {
        return 0.0F;
    }
    return input >= 1.0F ? 1.0F : input;
}

[[nodiscard]] bool IsUsableId(const std::string_view id, const std::size_t maximumBytes) noexcept {
    return !id.empty() && id.size() <= maximumBytes;
}

[[nodiscard]] bool IsKnownCurve(const AiResponseCurve curve) noexcept {
    return static_cast<std::uint8_t>(curve) <= static_cast<std::uint8_t>(AiResponseCurve::InverseQuadratic);
}

} // namespace

float EvaluateAiResponseCurve(const AiResponseCurve curve, const float input) noexcept {
    const float x = SanitizedInput(input);
    switch (curve) {
        case AiResponseCurve::Linear:
            return x;
        case AiResponseCurve::Quadratic:
            return x * x;
        case AiResponseCurve::InverseLinear:
            return 1.0F - x;
        case AiResponseCurve::InverseQuadratic:
            return (1.0F - x) * (1.0F - x);
    }
    return x;
}

bool IsBlackboardComponentValid(const BlackboardComponent& board) noexcept {
    if (board.entries.size() > kMaximumAiBlackboardEntries) {
        return false;
    }
    for (std::size_t index = 0U; index < board.entries.size(); ++index) {
        const AiBlackboardEntry& entry = board.entries[index];
        if (!IsUsableId(entry.key, kMaximumAiInputIdBytes) || !std::isfinite(entry.value)) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (board.entries[earlier].key == entry.key) {
                return false;
            }
        }
    }
    if (board.vectors.size() > kMaximumAiBlackboardVectors) {
        return false;
    }
    for (std::size_t index = 0U; index < board.vectors.size(); ++index) {
        const AiBlackboardVectorEntry& entry = board.vectors[index];
        if (!IsUsableId(entry.key, kMaximumAiInputIdBytes) || !std::isfinite(entry.value.x) ||
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

bool IsAiBrainComponentValid(const AiBrainComponent& brain) noexcept {
    if (brain.actions.size() > kMaximumAiActions || !std::isfinite(brain.hysteresis) ||
        brain.hysteresis < 0.0F || !std::isfinite(brain.currentScore)) {
        return false;
    }
    for (std::size_t index = 0U; index < brain.actions.size(); ++index) {
        const AiAction& action = brain.actions[index];
        if (!IsUsableId(action.actionId, kMaximumAiActionIdBytes) ||
            !std::isfinite(action.baseScore) || action.baseScore < 0.0F || action.baseScore > 1.0F ||
            action.considerations.size() > kMaximumAiConsiderations) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (brain.actions[earlier].actionId == action.actionId) {
                return false;
            }
        }
        for (const AiConsideration& consideration : action.considerations) {
            if (!IsUsableId(consideration.inputId, kMaximumAiInputIdBytes) ||
                !IsKnownCurve(consideration.curve) || !std::isfinite(consideration.weight) ||
                consideration.weight < 0.0F) {
                return false;
            }
        }
    }
    return true;
}

float GetBlackboardValue(const BlackboardComponent& board, const std::string_view key) noexcept {
    for (const AiBlackboardEntry& entry : board.entries) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return 0.0F;
}

bool SetBlackboardValue(BlackboardComponent& board, std::string key, const float value) {
    if (!IsUsableId(key, kMaximumAiInputIdBytes) || !std::isfinite(value)) {
        return false;
    }
    for (AiBlackboardEntry& entry : board.entries) {
        if (entry.key == key) {
            entry.value = value;
            return true;
        }
    }
    if (board.entries.size() >= kMaximumAiBlackboardEntries) {
        return false;
    }
    board.entries.push_back(AiBlackboardEntry{std::move(key), value});
    return true;
}

bool RemoveBlackboardValue(BlackboardComponent& board, const std::string_view key) {
    for (auto entry = board.entries.begin(); entry != board.entries.end(); ++entry) {
        if (entry->key == key) {
            board.entries.erase(entry);
            return true;
        }
    }
    return false;
}

Math::Vector3 GetBlackboardVector(const BlackboardComponent& board,
                                        const std::string_view key) noexcept {
    for (const AiBlackboardVectorEntry& entry : board.vectors) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return Math::Vector3{};
}

bool SetBlackboardVector(BlackboardComponent& board, std::string key, const Math::Vector3 value) {
    if (!IsUsableId(key, kMaximumAiInputIdBytes) || !std::isfinite(value.x) ||
        !std::isfinite(value.y) || !std::isfinite(value.z)) {
        return false;
    }
    for (AiBlackboardVectorEntry& entry : board.vectors) {
        if (entry.key == key) {
            entry.value = value;
            return true;
        }
    }
    if (board.vectors.size() >= kMaximumAiBlackboardVectors) {
        return false;
    }
    board.vectors.push_back(AiBlackboardVectorEntry{std::move(key), value});
    return true;
}

bool RemoveBlackboardVector(BlackboardComponent& board, const std::string_view key) {
    for (auto entry = board.vectors.begin(); entry != board.vectors.end(); ++entry) {
        if (entry->key == key) {
            board.vectors.erase(entry);
            return true;
        }
    }
    return false;
}

float ScoreAiAction(const AiAction& action, const BlackboardComponent& board) noexcept {
    double score = SanitizedInput(action.baseScore);
    std::size_t activeConsiderations = 0U;
    for (const AiConsideration& consideration : action.considerations) {
        const float value = GetBlackboardValue(board, consideration.inputId);
        const double curved = EvaluateAiResponseCurve(consideration.curve, value);
        const double weight =
            std::isfinite(consideration.weight) && consideration.weight >= 0.0F ? consideration.weight : 0.0;
        score *= std::pow(curved, weight);
        if (weight > 0.0) {
            ++activeConsiderations;
        }
    }
    if (action.compensateConsiderationCount && activeConsiderations > 1U && score > 0.0 && score < 1.0) {
        const double modificationFactor = 1.0 - (1.0 / static_cast<double>(activeConsiderations));
        const double makeupValue = (1.0 - score) * modificationFactor;
        score += makeupValue * score;
    }
    return static_cast<float>(score);
}

AiActionSelection SelectAiAction(const AiBrainComponent& brain,
                                       const BlackboardComponent& board) {
    const AiAction* best = nullptr;
    float bestScore = 0.0F;
    for (const AiAction& action : brain.actions) {
        const float score = ScoreAiAction(action, board);
        if (best == nullptr || score > bestScore) {
            best = &action;
            bestScore = score;
        }
    }
    AiActionSelection selection;
    if (best == nullptr || bestScore <= 0.0F) {
        selection.changed = !brain.currentAction.empty();
        return selection;
    }
    const float hysteresis =
        std::isfinite(brain.hysteresis) && brain.hysteresis >= 0.0F ? brain.hysteresis : 0.0F;
    float incumbentScore = -1.0F;
    if (!brain.currentAction.empty()) {
        for (const AiAction& action : brain.actions) {
            if (action.actionId == brain.currentAction) {
                incumbentScore = ScoreAiAction(action, board);
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
