// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Drag-and-Drop Framework.
// Coordinates drag sources, visual decorators, and drop targets.
#pragma once
#include "Canvas.h"
#include <any>
#include <memory>
#include <string>

namespace prism {

enum class DragDropEffect : uint8_t {
  None,
  Copy,
  Move,
  Link,
};

class DragDropOperation {
public:
  DragDropOperation(std::string tag = "") : m_tag(std::move(tag)) {}
  virtual ~DragDropOperation() = default;

  const std::string& tag() const { return m_tag; }
  void setTag(std::string tag) { m_tag = std::move(tag); }

  template <typename T>
  void setPayload(T&& val) {
    m_payload = std::forward<T>(val);
  }

  template <typename T>
  const T* getPayload() const {
    return std::any_cast<T>(&m_payload);
  }

  bool hasPayload() const { return m_payload.has_value(); }

  DragDropEffect effect = DragDropEffect::Move;

  // Custom visual feedback decorator rendered at the mouse position
  virtual void paintDecorator(Canvas& c, Vec2 mousePos) const {
    if (m_decorator) {
      m_decorator(c, mousePos);
    } else {
      // Default sleek pill
      Rect r(mousePos.x + 12, mousePos.y + 12, 110, 24);
      c.shadow(r, 4, 8, Color(0, 0, 0, 180));
      c.fillRoundRect(r, 4.f, Color(45, 55, 70, 240));
      c.strokeRoundRect(r, 4.f, Color(80, 120, 180), 1);
      c.textIn(*theme().ui, r, m_tag.empty() ? "Dragging" : m_tag, Color(240, 240, 240), HAlign::Center);
    }
  }

  void setDecorator(std::function<void(Canvas&, Vec2)> dec) {
    m_decorator = std::move(dec);
  }

private:
  std::string m_tag;
  std::any m_payload;
  std::function<void(Canvas&, Vec2)> m_decorator;
};

struct DragDropEvent {
  Vec2 pos;
  std::shared_ptr<DragDropOperation> operation;
  uint8_t mods = 0;
};

} // namespace prism
