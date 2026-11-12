// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <algorithm>
#include <utility>

namespace UVE::Editor {

/// Holds its content to the width it is given: a long name ends in "..." instead of widening the
/// whole column (a ScrollBox otherwise lays its content out at its widest). For a PropertyGrid in a
/// Details column.
class FitWidthUVE final : public prism::Widget {
public:
    explicit FitWidthUVE(prism::Ref<prism::Widget> content) : m_content(std::move(content)) { addChild(m_content); }
    prism::Vec2 measure(const prism::Vec2 avail) override {
        const prism::Vec2 inner = m_content->measure(avail);
        return m_desired = constrain({std::min(inner.x, std::max(1.0F, avail.x)), inner.y});
    }
    void arrange(const prism::Rect r) override {
        m_rect = r;
        m_content->arrange(r);
    }
    void paint(prism::Canvas& c) override { m_content->paint(c); }

private:
    prism::Ref<prism::Widget> m_content;
};

} // namespace UVE::Editor
