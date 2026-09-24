// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/input_map_document_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "uve/config/config_manager_uve.h"
#include "uve/input/input_names_uve.h"
#include "uve/input/input_system_uve.h"

namespace UVE::Core {
namespace {

using Input::InputActionTypeUVE;
using Input::InputActionUVE;
using Input::InputBindingSourceUVE;
using Input::InputBindingUVE;

constexpr std::int64_t kInputMapVersionUVE = 1;
/// The input system's own per-side limit.
constexpr std::size_t kMaximumBindingsPerSideUVE = 32U;

[[nodiscard]] std::string_view ActionTypeNameUVE(const InputActionTypeUVE type) noexcept {
    return type == InputActionTypeUVE::Axis1D ? "Axis" : "Button";
}

[[nodiscard]] bool IsBindingUsableUVE(const InputBindingUVE& binding) noexcept {
    switch (binding.source) {
    case InputBindingSourceUVE::Keyboard:
        return !Input::GetKeyNameUVE(binding.key).empty();
    case InputBindingSourceUVE::Mouse:
        return !Input::GetMouseButtonNameUVE(binding.mouseButton).empty();
    case InputBindingSourceUVE::GamepadButton:
        return binding.gamepadIndex < Input::kMaximumGamepadCountUVE &&
               !Input::GetGamepadButtonNameUVE(binding.gamepadButton).empty();
    case InputBindingSourceUVE::GamepadAxis:
        return binding.gamepadIndex < Input::kMaximumGamepadCountUVE &&
               !Input::GetGamepadAxisNameUVE(binding.gamepadAxis).empty() && std::isfinite(binding.scale) &&
               binding.scale != 0.0F;
    }
    return false;
}

[[nodiscard]] std::vector<InputBindingUVE> SanitizeBindingsUVE(std::vector<InputBindingUVE> bindings) {
    std::erase_if(bindings, [](const InputBindingUVE& binding) { return !IsBindingUsableUVE(binding); });
    if (bindings.size() > kMaximumBindingsPerSideUVE) {
        bindings.resize(kMaximumBindingsPerSideUVE);
    }
    return bindings;
}

void WriteBindingsUVE(Config::IConfigManagerUVE& store, const std::string& prefix,
                      const std::vector<InputBindingUVE>& bindings) {
    store.SetIntUVE(prefix + ".count", static_cast<std::int64_t>(bindings.size()));
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        const InputBindingUVE& binding = bindings[index];
        const std::string key = prefix + "." + std::to_string(index) + ".";
        store.SetStringUVE(key + "device", std::string(Input::GetInputBindingSourceNameUVE(binding.source)));
        switch (binding.source) {
        case InputBindingSourceUVE::Keyboard:
            store.SetStringUVE(key + "key", std::string(Input::GetKeyNameUVE(binding.key)));
            break;
        case InputBindingSourceUVE::Mouse:
            store.SetStringUVE(key + "button", std::string(Input::GetMouseButtonNameUVE(binding.mouseButton)));
            break;
        case InputBindingSourceUVE::GamepadButton:
            store.SetIntUVE(key + "pad", static_cast<std::int64_t>(binding.gamepadIndex) + 1);
            store.SetStringUVE(key + "button", std::string(Input::GetGamepadButtonNameUVE(binding.gamepadButton)));
            break;
        case InputBindingSourceUVE::GamepadAxis:
            store.SetIntUVE(key + "pad", static_cast<std::int64_t>(binding.gamepadIndex) + 1);
            store.SetStringUVE(key + "axis", std::string(Input::GetGamepadAxisNameUVE(binding.gamepadAxis)));
            store.SetDoubleUVE(key + "scale", static_cast<double>(binding.scale));
            break;
        }
    }
}

[[nodiscard]] std::optional<InputBindingUVE> ReadBindingUVE(const Config::IConfigManagerUVE& store,
                                                            const std::string& key) {
    const std::optional<InputBindingSourceUVE> source =
        Input::ParseInputBindingSourceNameUVE(store.GetStringUVE(key + "device", ""));
    if (!source) {
        return std::nullopt;
    }
    InputBindingUVE binding;
    binding.source = *source;
    // Pads are counted from one in the file, as a person counts them.
    const std::int64_t pad = store.GetIntUVE(key + "pad", 1) - 1;
    binding.gamepadIndex = pad >= 0 ? static_cast<std::size_t>(pad) : Input::kMaximumGamepadCountUVE;
    switch (*source) {
    case InputBindingSourceUVE::Keyboard: {
        const auto parsed = Input::ParseKeyNameUVE(store.GetStringUVE(key + "key", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.key = *parsed;
        break;
    }
    case InputBindingSourceUVE::Mouse: {
        const auto parsed = Input::ParseMouseButtonNameUVE(store.GetStringUVE(key + "button", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.mouseButton = *parsed;
        break;
    }
    case InputBindingSourceUVE::GamepadButton: {
        const auto parsed = Input::ParseGamepadButtonNameUVE(store.GetStringUVE(key + "button", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.gamepadButton = *parsed;
        break;
    }
    case InputBindingSourceUVE::GamepadAxis: {
        const auto parsed = Input::ParseGamepadAxisNameUVE(store.GetStringUVE(key + "axis", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.gamepadAxis = *parsed;
        const double scale = store.GetDoubleUVE(key + "scale", 1.0);
        // A scale beyond float range would not survive the narrowing; it is not a real scale.
        binding.scale = std::abs(scale) <= 1.0e6 ? static_cast<float>(scale) : 0.0F;
        break;
    }
    }
    return IsBindingUsableUVE(binding) ? std::optional<InputBindingUVE>(binding) : std::nullopt;
}

[[nodiscard]] std::vector<InputBindingUVE> ReadBindingsUVE(const Config::IConfigManagerUVE& store,
                                                           const std::string& prefix) {
    const auto count = static_cast<std::size_t>(
        std::clamp<std::int64_t>(store.GetIntUVE(prefix + ".count", 0), 0,
                                 static_cast<std::int64_t>(kMaximumBindingsPerSideUVE)));
    std::vector<InputBindingUVE> bindings;
    for (std::size_t index = 0U; index < count; ++index) {
        if (const std::optional<InputBindingUVE> binding =
                ReadBindingUVE(store, prefix + "." + std::to_string(index) + ".")) {
            bindings.push_back(*binding);
        }
    }
    return bindings;
}

} // namespace

std::vector<InputActionUVE> InputMapDocumentUVE::SanitizeUVE(std::vector<InputActionUVE> actions) {
    std::vector<InputActionUVE> kept;
    std::unordered_set<std::string> names;
    for (InputActionUVE& action : actions) {
        if (kept.size() >= Input::kMaximumInputActionsUVE || action.name.empty() ||
            action.name.size() > Input::kMaximumInputActionNameBytesUVE ||
            action.name.find('\0') != std::string::npos || !names.insert(action.name).second) {
            continue;
        }
        action.positiveBindings = SanitizeBindingsUVE(std::move(action.positiveBindings));
        // Only an axis has a negative direction.
        action.negativeBindings = action.type == InputActionTypeUVE::Axis1D
                                      ? SanitizeBindingsUVE(std::move(action.negativeBindings))
                                      : std::vector<InputBindingUVE>{};
        kept.push_back(std::move(action));
    }
    return kept;
}

bool InputMapDocumentUVE::LoadUVE(const std::filesystem::path& path) {
    m_path = path;
    m_dirty = false;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        m_actions.clear();
        return true;
    }
    Config::ConfigManagerUVE store;
    if (!store.LoadUVE(path)) {
        return false;
    }
    const auto count = static_cast<std::size_t>(std::clamp<std::int64_t>(
        store.GetIntUVE("actions.count", 0), 0, static_cast<std::int64_t>(Input::kMaximumInputActionsUVE)));
    std::vector<InputActionUVE> actions;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string prefix = "actions." + std::to_string(index);
        InputActionUVE action;
        action.name = store.GetStringUVE(prefix + ".name", "");
        const std::string type = store.GetStringUVE(prefix + ".type", "Button");
        if (type != "Button" && type != "Axis") {
            continue;
        }
        action.type = type == "Axis" ? InputActionTypeUVE::Axis1D : InputActionTypeUVE::Button;
        action.positiveBindings = ReadBindingsUVE(store, prefix + ".positive");
        action.negativeBindings = ReadBindingsUVE(store, prefix + ".negative");
        actions.push_back(std::move(action));
    }
    m_actions = SanitizeUVE(std::move(actions));
    return true;
}

bool InputMapDocumentUVE::SaveUVE() {
    if (m_path.empty()) {
        return false;
    }
    // Written into a fresh document, so an action removed since the last save leaves no trace.
    Config::ConfigManagerUVE store;
    store.SetIntUVE("version", kInputMapVersionUVE);
    store.SetIntUVE("actions.count", static_cast<std::int64_t>(m_actions.size()));
    for (std::size_t index = 0U; index < m_actions.size(); ++index) {
        const InputActionUVE& action = m_actions[index];
        const std::string prefix = "actions." + std::to_string(index);
        store.SetStringUVE(prefix + ".name", action.name);
        store.SetStringUVE(prefix + ".type", std::string(ActionTypeNameUVE(action.type)));
        WriteBindingsUVE(store, prefix + ".positive", action.positiveBindings);
        if (action.type == InputActionTypeUVE::Axis1D) {
            WriteBindingsUVE(store, prefix + ".negative", action.negativeBindings);
        }
    }
    if (!store.SaveUVE(m_path)) {
        return false;
    }
    m_dirty = false;
    return true;
}

void InputMapDocumentUVE::SetActionsUVE(std::vector<InputActionUVE> actions) {
    m_actions = SanitizeUVE(std::move(actions));
    m_dirty = true;
}

void InputMapDocumentUVE::ApplyUVE(Input::IInputSystemUVE& inputSystem) {
    std::vector<std::string> applied;
    applied.reserve(m_actions.size());
    for (const InputActionUVE& action : m_actions) {
        InputActionUVE copy = action;
        inputSystem.RegisterActionUVE(std::move(copy));
        applied.push_back(action.name);
    }
    for (const std::string& name : m_appliedNames) {
        if (std::find(applied.begin(), applied.end(), name) == applied.end()) {
            static_cast<void>(inputSystem.UnregisterActionUVE(name));
        }
    }
    m_appliedNames = std::move(applied);
}

} // namespace UVE::Core
