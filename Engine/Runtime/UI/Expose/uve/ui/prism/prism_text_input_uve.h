// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// High-performance interactive single-line text input field.
/// Supports cursor navigation, backspace, selection, typing, and placeholder text.
class PrismTextInputUVE : public PrismWidgetUVE {
public:
    explicit PrismTextInputUVE(std::string placeholder = "Enter text...");

    void SetText(std::string text);
    [[nodiscard]] const std::string& GetText() const noexcept { return m_text; }

    void SetPlaceholder(std::string placeholder);
    [[nodiscard]] const std::string& GetPlaceholder() const noexcept { return m_placeholder; }

    void SetOnTextChanged(std::function<void(const std::string&)> callback) {
        m_onTextChanged = std::move(callback);
    }

    void SetOnCommit(std::function<void(const std::string&)> callback) {
        m_onCommit = std::move(callback);
    }

    // --- State overrides ---
    void OnFocusGained() override;
    void OnFocusLost() override;
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;
    PrismEventResultUVE OnCharInput(char32_t codepoint) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

    std::string                             m_text;
    std::string                             m_placeholder;
    std::size_t                             m_cursorIndex = 0;
    std::function<void(const std::string&)> m_onTextChanged;
    std::function<void(const std::string&)> m_onCommit;
};

} // namespace UVE::UI::Prism
