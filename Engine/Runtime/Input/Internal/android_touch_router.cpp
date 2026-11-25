#include "android_touch_router.h"
#include "i_mobile_gesture_system.h"

namespace UVE::Input {
bool AndroidTouchRouter::Down(const std::int32_t pointerId,
                                    const Math::Vector2 position, const float pressure) {
    if (pointerId < 0) return false;
    for (const auto& contact : m_contacts) {
        if (contact.pointerId == pointerId) return false;
    }
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        if (m_contacts[slot].pointerId != -1) continue;
        if (++m_nextIdentifier == 0U) ++m_nextIdentifier;
        m_contacts[slot] = Contact{pointerId, m_nextIdentifier};
        m_sink.SetTouchState(slot, true, m_nextIdentifier, position, pressure);
        return true;
    }
    return false;
}
bool AndroidTouchRouter::Move(const std::int32_t pointerId,
                                    const Math::Vector2 position, const float pressure) {
    if (pointerId < 0) return false;
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        const auto& contact = m_contacts[slot];
        if (contact.pointerId != pointerId) continue;
        m_sink.SetTouchState(slot, true, contact.identifier, position, pressure);
        return true;
    }
    return false;
}
bool AndroidTouchRouter::Up(const std::int32_t pointerId) {
    if (pointerId < 0) return false;
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        if (m_contacts[slot].pointerId != pointerId) continue;
        m_sink.SetTouchState(slot, false, 0U, {}, 0.0F);
        m_contacts[slot] = Contact{};
        return true;
    }
    return false;
}
void AndroidTouchRouter::Cancel() {
    for (std::size_t slot = 0U; slot < m_contacts.size(); ++slot) {
        if (m_contacts[slot].pointerId == -1) continue;
        m_sink.SetTouchState(slot, false, 0U, {}, 0.0F);
        m_contacts[slot] = Contact{};
    }
}
void CancelAndroidInput(AndroidTouchRouter& router, IMobileGestureSystem& gestures) {
    router.Cancel();
    gestures.Reset();
}
} // namespace UVE::Input
