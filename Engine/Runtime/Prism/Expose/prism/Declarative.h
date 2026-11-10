// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Declarative Construction Syntax & Widget Builders (PNew & fluent builder pattern).
#pragma once
#include "Widget.h"
#include "Attribute.h"
#include <concepts>
#include <memory>
#include <utility>

namespace prism {

// Factory function: constructs a Ref<WidgetType>
template <typename WidgetType, typename... Args>
  requires std::derived_from<WidgetType, Widget>
Ref<WidgetType> makeWidget(Args&&... args) {
  return std::make_shared<WidgetType>(std::forward<Args>(args)...);
}


// Fluent builder extensions for common widget properties
template <typename T>
  requires std::derived_from<T, Widget>
class WidgetBuilder {
public:
  explicit WidgetBuilder(Ref<T> widget) : m_widget(std::move(widget)) {}

  Ref<T> get() const { return m_widget; }
  Ref<T> build() const { return m_widget; }
  operator Ref<T>() const { return m_widget; }
  T* operator->() const { return m_widget.get(); }

  WidgetBuilder& slot(Slot s) {
    if (m_widget) m_widget->slot = s;
    return *this;
  }

  WidgetBuilder& padding(Margin m) {
    if (m_widget) m_widget->slot.pad = m;
    return *this;
  }

  WidgetBuilder& fill(float f) {
    if (m_widget) m_widget->slot.fill = f;
    return *this;
  }

  WidgetBuilder& align(HAlign h, VAlign v = VAlign::Center) {
    if (m_widget) { m_widget->slot.h = h; m_widget->slot.v = v; }
    return *this;
  }

  WidgetBuilder& fixedSize(float w, float h) {
    if (m_widget) m_widget->setFixedSize(w, h);
    return *this;
  }

  WidgetBuilder& tooltip(std::string tip) {
    if (m_widget) m_widget->setTooltip(std::move(tip));
    return *this;
  }

  WidgetBuilder& enabled(bool en) {
    if (m_widget) m_widget->setEnabled(en);
    return *this;
  }

private:
  Ref<T> m_widget;
};

template <typename T>
WidgetBuilder<T> build(Ref<T> w) {
  return WidgetBuilder<T>(std::move(w));
}

} // namespace prism

// Declarative PNew / PAssignNew macros returning a fluent WidgetBuilder
#define PNew(WidgetType, ...) (::prism::build(::prism::makeWidget<WidgetType>(__VA_ARGS__)))
#define PAssignNew(Var, WidgetType, ...) \
  ((Var) = ::prism::build(::prism::makeWidget<WidgetType>(__VA_ARGS__)).get())

