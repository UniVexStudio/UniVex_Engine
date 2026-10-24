#include "uve/input/android_touch_router_uve.h"

namespace UVE::Input {
bool AndroidTouchRouterUVE::DownUVE(const std::int32_t pointerId,
                                    const Math::Vector2UVE position, const float pressure) {
    if (pointerId < 0) return false;
    for (const auto& contact : m_contacts) {
        if (contact.pointerId == pointerId) return false;
    }
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        if (m_contacts[slot].pointerId != -1) continue;
        if (++m_nextIdentifier == 0U) ++m_nextIdentifier;
        m_contacts[slot] = Contact{pointerId, m_nextIdentifier};
        m_sink.SetTouchStateUVE(slot, true, m_nextIdentifier, position, pressure);
        return true;
    }
    return false;
}
bool AndroidTouchRouterUVE::MoveUVE(const std::int32_t pointerId,
                                    const Math::Vector2UVE position, const float pressure) {
    if (pointerId < 0) return false;
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        const auto& contact = m_contacts[slot];
        if (contact.pointerId != pointerId) continue;
        m_sink.SetTouchStateUVE(slot, true, contact.identifier, position, pressure);
        return true;
    }
    return false;
}
bool AndroidTouchRouterUVE::UpUVE(const std::int32_t pointerId) {
    if (pointerId < 0) return false;
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        if (m_contacts[slot].pointerId != pointerId) continue;
        m_sink.SetTouchStateUVE(slot, false, 0U, {}, 0.0F);
        m_contacts[slot] = Contact{};
        return true;
    }
    return false;
}
void AndroidTouchRouterUVE::CancelUVE() {
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        if (m_contacts[slot].pointerId == -1) continue;
        m_sink.SetTouchStateUVE(slot, false, 0U, {}, 0.0F);
        m_contacts[slot] = Contact{};
    }
}
} // namespace UVE::Input
