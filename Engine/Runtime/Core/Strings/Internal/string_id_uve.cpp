// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/strings/string_id_uve.h"

#include <deque>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>

#include "uve/logging/assert_uve.h"

namespace UVE::Strings {
namespace {

/// The process-wide intern table. Index 0 is pre-registered to "" so the default-constructed
/// id always names the empty string. Strings live in a deque (references never invalidate on
/// growth), keyed by views into that same deque. Never shrinks: every index, view and C string
/// handed out stays valid until process exit.
class InternTableUVE final {
public:
    InternTableUVE() {
        m_strings.emplace_back("");
        m_indexByText.emplace(std::string_view(m_strings.back()), 0U);
    }

    std::uint32_t InternUVE(std::string_view text) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (const auto existing = m_indexByText.find(text); existing != m_indexByText.end()) {
            return existing->second;
        }
        const auto index = static_cast<std::uint32_t>(m_strings.size());
        m_strings.emplace_back(text);
        m_indexByText.emplace(std::string_view(m_strings.back()), index);
        return index;
    }

    const std::string& LookupUVE(std::uint32_t index) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        UVE_ASSERT(index < m_strings.size());
        return m_strings[index];
    }

private:
    mutable std::mutex m_mutex;
    std::deque<std::string> m_strings;
    std::unordered_map<std::string_view, std::uint32_t> m_indexByText;
};

InternTableUVE& GetInternTableUVE() {
    static InternTableUVE table;
    return table;
}

}  // namespace

StringIdUVE::StringIdUVE(std::string_view text) : m_index(GetInternTableUVE().InternUVE(text)) {}

StringIdUVE::StringIdUVE(const char* text)
    : m_index(GetInternTableUVE().InternUVE(std::string_view(text))) {}

StringIdUVE::StringIdUVE(const std::string& text)
    : m_index(GetInternTableUVE().InternUVE(std::string_view(text))) {}

std::string_view StringIdUVE::ToStringUVE() const {
    return std::string_view(GetInternTableUVE().LookupUVE(m_index));
}

const char* StringIdUVE::ToCStringUVE() const {
    return GetInternTableUVE().LookupUVE(m_index).c_str();
}

std::ostream& operator<<(std::ostream& stream, const StringIdUVE& id) {
    return stream << id.ToStringUVE();
}

}  // namespace UVE::Strings
