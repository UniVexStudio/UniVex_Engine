#pragma once

#include "uve/input/i_mobile_input_system_uve.h"

namespace UVE::Input {
// Single platform-thread adapter. Android pointer IDs are identities, not snapshot slots.
class AndroidTouchRouterUVE final {
public:
    explicit AndroidTouchRouterUVE(IMobileInputSystemUVE& sink) : m_sink(sink) {}
    bool DownUVE(std::int32_t pointerId, Math::Vector2UVE position, float pressure);
    bool MoveUVE(std::int32_t pointerId, Math::Vector2UVE position, float pressure);
    bool UpUVE(std::int32_t pointerId);
    void CancelUVE();
private:
    struct Contact { std::int32_t pointerId = -1; std::uint64_t identifier = 0U; };
    IMobileInputSystemUVE& m_sink;
    std::array<Contact, kMaximumTouchCountUVE> m_contacts{};
    std::uint64_t m_nextIdentifier = 0U;
};
} // namespace UVE::Input
