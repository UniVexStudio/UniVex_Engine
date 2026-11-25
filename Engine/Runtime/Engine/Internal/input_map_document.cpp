// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "input_map_document.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "config_manager.h"
#include "input_names.h"
#include "input_system.h"

namespace UVE::Core {
namespace {

using Input::InputActionType;
using Input::InputAction;
using Input::InputBindingSource;
using Input::InputBinding;

constexpr std::int64_t kInputMapVersion = 1;
/// The input system's own per-side limit.
constexpr std::size_t kMaximumBindingsPerSide = 32U;

[[nodiscard]] std::string_view ActionTypeName(const InputActionType type) noexcept {
    return type == InputActionType::Axis1D ? "Axis" : "Button";
}

[[nodiscard]] bool IsBindingUsable(const InputBinding& binding) noexcept {
    switch (binding.source) {
    case InputBindingSource::Keyboard:
        return !Input::GetKeyName(binding.key).empty();
    case InputBindingSource::Mouse:
        return !Input::GetMouseButtonName(binding.mouseButton).empty();
    case InputBindingSource::GamepadButton:
        return binding.gamepadIndex < Input::kMaximumGamepadCount &&
               !Input::GetGamepadButtonName(binding.gamepadButton).empty();
    case InputBindingSource::GamepadAxis:
        return binding.gamepadIndex < Input::kMaximumGamepadCount &&
               !Input::GetGamepadAxisName(binding.gamepadAxis).empty() && std::isfinite(binding.scale) &&
               binding.scale != 0.0F;
    }
    return false;
}

[[nodiscard]] std::vector<InputBinding> SanitizeBindings(std::vector<InputBinding> bindings) {
    std::erase_if(bindings, [](const InputBinding& binding) { return !IsBindingUsable(binding); });
    if (bindings.size() > kMaximumBindingsPerSide) {
        bindings.resize(kMaximumBindingsPerSide);
    }
    return bindings;
}

void WriteBindings(Config::IConfigManager& store, const std::string& prefix,
                      const std::vector<InputBinding>& bindings) {
    store.SetInt(prefix + ".count", static_cast<std::int64_t>(bindings.size()));
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        const InputBinding& binding = bindings[index];
        const std::string key = prefix + "." + std::to_string(index) + ".";
        store.SetString(key + "device", std::string(Input::GetInputBindingSourceName(binding.source)));
        switch (binding.source) {
        case InputBindingSource::Keyboard:
            store.SetString(key + "key", std::string(Input::GetKeyName(binding.key)));
            break;
        case InputBindingSource::Mouse:
            store.SetString(key + "button", std::string(Input::GetMouseButtonName(binding.mouseButton)));
            break;
        case InputBindingSource::GamepadButton:
            store.SetInt(key + "pad", static_cast<std::int64_t>(binding.gamepadIndex) + 1);
            store.SetString(key + "button", std::string(Input::GetGamepadButtonName(binding.gamepadButton)));
            break;
        case InputBindingSource::GamepadAxis:
            store.SetInt(key + "pad", static_cast<std::int64_t>(binding.gamepadIndex) + 1);
            store.SetString(key + "axis", std::string(Input::GetGamepadAxisName(binding.gamepadAxis)));
            store.SetDouble(key + "scale", static_cast<double>(binding.scale));
            break;
        }
    }
}

[[nodiscard]] std::optional<InputBinding> ReadBinding(const Config::IConfigManager& store,
                                                            const std::string& key) {
    const std::optional<InputBindingSource> source =
        Input::ParseInputBindingSourceName(store.GetString(key + "device", ""));
    if (!source) {
        return std::nullopt;
    }
    InputBinding binding;
    binding.source = *source;
    // Pads are counted from one in the file, as a person counts them.
    const std::int64_t pad = store.GetInt(key + "pad", 1) - 1;
    binding.gamepadIndex = pad >= 0 ? static_cast<std::size_t>(pad) : Input::kMaximumGamepadCount;
    switch (*source) {
    case InputBindingSource::Keyboard: {
        const auto parsed = Input::ParseKeyName(store.GetString(key + "key", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.key = *parsed;
        break;
    }
    case InputBindingSource::Mouse: {
        const auto parsed = Input::ParseMouseButtonName(store.GetString(key + "button", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.mouseButton = *parsed;
        break;
    }
    case InputBindingSource::GamepadButton: {
        const auto parsed = Input::ParseGamepadButtonName(store.GetString(key + "button", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.gamepadButton = *parsed;
        break;
    }
    case InputBindingSource::GamepadAxis: {
        const auto parsed = Input::ParseGamepadAxisName(store.GetString(key + "axis", ""));
        if (!parsed) {
            return std::nullopt;
        }
        binding.gamepadAxis = *parsed;
        const double scale = store.GetDouble(key + "scale", 1.0);
        // A scale beyond float range would not survive the narrowing; it is not a real scale.
        binding.scale = std::abs(scale) <= 1.0e6 ? static_cast<float>(scale) : 0.0F;
        break;
    }
    }
    return IsBindingUsable(binding) ? std::optional<InputBinding>(binding) : std::nullopt;
}

[[nodiscard]] std::vector<InputBinding> ReadBindings(const Config::IConfigManager& store,
                                                           const std::string& prefix) {
    const auto count = static_cast<std::size_t>(
        std::clamp<std::int64_t>(store.GetInt(prefix + ".count", 0), 0,
                                 static_cast<std::int64_t>(kMaximumBindingsPerSide)));
    std::vector<InputBinding> bindings;
    for (std::size_t index = 0U; index < count; ++index) {
        if (const std::optional<InputBinding> binding =
                ReadBinding(store, prefix + "." + std::to_string(index) + ".")) {
            bindings.push_back(*binding);
        }
    }
    return bindings;
}

} // namespace

std::vector<InputAction> InputMapDocument::Sanitize(std::vector<InputAction> actions) {
    std::vector<InputAction> kept;
    std::unordered_set<std::string> names;
    for (InputAction& action : actions) {
        if (kept.size() >= Input::kMaximumInputActions || action.name.empty() ||
            action.name.size() > Input::kMaximumInputActionNameBytes ||
            action.name.contains('\0') || !names.insert(action.name).second) {
            continue;
        }
        action.positiveBindings = SanitizeBindings(std::move(action.positiveBindings));
        // Only an axis has a negative direction.
        action.negativeBindings = action.type == InputActionType::Axis1D
                                      ? SanitizeBindings(std::move(action.negativeBindings))
                                      : std::vector<InputBinding>{};
        kept.push_back(std::move(action));
    }
    return kept;
}

bool InputMapDocument::Load(const std::filesystem::path& path) {
    m_path = path;
    m_dirty = false;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        m_actions.clear();
        return true;
    }
    Config::ConfigManager store;
    if (!store.Load(path)) {
        return false;
    }
    const auto count = static_cast<std::size_t>(std::clamp<std::int64_t>(
        store.GetInt("actions.count", 0), 0, static_cast<std::int64_t>(Input::kMaximumInputActions)));
    std::vector<InputAction> actions;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string prefix = "actions." + std::to_string(index);
        InputAction action;
        action.name = store.GetString(prefix + ".name", "");
        const std::string type = store.GetString(prefix + ".type", "Button");
        if (type != "Button" && type != "Axis") {
            continue;
        }
        action.type = type == "Axis" ? InputActionType::Axis1D : InputActionType::Button;
        action.positiveBindings = ReadBindings(store, prefix + ".positive");
        action.negativeBindings = ReadBindings(store, prefix + ".negative");
        actions.push_back(std::move(action));
    }
    m_actions = Sanitize(std::move(actions));
    return true;
}

bool InputMapDocument::Save() {
    if (m_path.empty()) {
        return false;
    }
    // Written into a fresh document, so an action removed since the last save leaves no trace.
    Config::ConfigManager store;
    store.SetInt("version", kInputMapVersion);
    store.SetInt("actions.count", static_cast<std::int64_t>(m_actions.size()));
    for (std::size_t index = 0U; index < m_actions.size(); ++index) {
        const InputAction& action = m_actions[index];
        const std::string prefix = "actions." + std::to_string(index);
        store.SetString(prefix + ".name", action.name);
        store.SetString(prefix + ".type", std::string(ActionTypeName(action.type)));
        WriteBindings(store, prefix + ".positive", action.positiveBindings);
        if (action.type == InputActionType::Axis1D) {
            WriteBindings(store, prefix + ".negative", action.negativeBindings);
        }
    }
    if (!store.Save(m_path)) {
        return false;
    }
    m_dirty = false;
    return true;
}

void InputMapDocument::SetActions(std::vector<InputAction> actions) {
    m_actions = Sanitize(std::move(actions));
    m_dirty = true;
}

void InputMapDocument::Apply(Input::IInputSystem& inputSystem) {
    std::vector<std::string> applied;
    applied.reserve(m_actions.size());
    for (const InputAction& action : m_actions) {
        InputAction copy = action;
        inputSystem.RegisterAction(std::move(copy));
        applied.push_back(action.name);
    }
    for (const std::string& name : m_appliedNames) {
        if (std::find(applied.begin(), applied.end(), name) == applied.end()) {
            static_cast<void>(inputSystem.UnregisterAction(name));
        }
    }
    m_appliedNames = std::move(applied);
}

} // namespace UVE::Core
