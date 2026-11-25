#pragma once

#include "i_mobile_input_system.h"

namespace UVE::Input {
class IMobileGestureSystem;
// Single platform-thread adapter. Android pointer IDs are identities, not snapshot slots.
class AndroidTouchRouter final {
public:
    explicit AndroidTouchRouter(IMobileInputSystem& sink) : m_sink(sink) {}
    bool Down(std::int32_t pointerId, Math::Vector2 position, float pressure);
    bool Move(std::int32_t pointerId, Math::Vector2 position, float pressure);
    bool Up(std::int32_t pointerId);
    void Cancel();
private:
    struct Contact { std::int32_t pointerId = -1; std::uint64_t identifier = 0U; };
    IMobileInputSystem& m_sink;
    std::array<Contact, kMaximumTouchCount> m_contacts{};
    std::uint64_t m_nextIdentifier = 0U;
};
// Cancellation clears gesture history so an interrupted contact cannot become a tap.
void CancelAndroidInput(AndroidTouchRouter& router, IMobileGestureSystem& gestures);
} // namespace UVE::Input
