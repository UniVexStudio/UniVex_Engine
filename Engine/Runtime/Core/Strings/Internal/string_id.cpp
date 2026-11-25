// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "string_id.h"

#include <deque>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>

#include "engine_assert.h"

namespace UVE::Strings {
namespace {

/// The process-wide intern table. Index 0 is pre-registered to "" so the default-constructed
/// id always names the empty string. Strings live in a deque (references never invalidate on
/// growth), keyed by views into that same deque. Never shrinks: every index, view and C string
/// handed out stays valid until process exit.
class InternTable final {
public:
    InternTable() {
        m_strings.emplace_back("");
        m_indexByText.emplace(std::string_view(m_strings.back()), 0U);
    }

    std::uint32_t Intern(std::string_view text) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (const auto existing = m_indexByText.find(text); existing != m_indexByText.end()) {
            return existing->second;
        }
        const auto index = static_cast<std::uint32_t>(m_strings.size());
        m_strings.emplace_back(text);
        m_indexByText.emplace(std::string_view(m_strings.back()), index);
        return index;
    }

    const std::string& Lookup(std::uint32_t index) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        UVE_ASSERT(index < m_strings.size());
        return m_strings[index];
    }

private:
    mutable std::mutex m_mutex;
    std::deque<std::string> m_strings;
    std::unordered_map<std::string_view, std::uint32_t> m_indexByText;
};

InternTable& GetInternTable() {
    static InternTable table;
    return table;
}

}  // namespace

StringId::StringId(std::string_view text) : m_index(GetInternTable().Intern(text)) {}

StringId::StringId(const char* text)
    : m_index(GetInternTable().Intern(std::string_view(text))) {}

StringId::StringId(const std::string& text)
    : m_index(GetInternTable().Intern(std::string_view(text))) {}

std::string_view StringId::ToString() const {
    return std::string_view(GetInternTable().Lookup(m_index));
}

const char* StringId::ToCString() const {
    return GetInternTable().Lookup(m_index).c_str();
}

std::ostream& operator<<(std::ostream& stream, const StringId& id) {
    return stream << id.ToString();
}

}  // namespace UVE::Strings
