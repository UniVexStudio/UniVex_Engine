// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "texture_handle.h"

namespace UVE::Render {

inline constexpr std::uint32_t kInvalidBindlessTextureIndexUVE = 0xFFFFFFFFU;
inline constexpr std::uint32_t kDefaultMaxBindlessTexturesUVE = 4096U;

/// Fixed-capacity O(1) bindless descriptor slot table mapping live TextureHandleUVE values to
/// compact 32-bit shader indices for descriptor-indexing / bindless pipelines.
class BindlessTextureTableUVE final {
public:
    explicit BindlessTextureTableUVE(std::uint32_t capacity = kDefaultMaxBindlessTexturesUVE) noexcept
        : m_capacity(capacity) {}

    [[nodiscard]] std::uint32_t CapacityUVE() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t ActiveCountUVE() const noexcept { return m_textureToSlot.size(); }

    /// Registers `texture` and returns its bindless table index. Idempotent for an already
    /// registered texture. Returns kInvalidBindlessTextureIndexUVE if invalid or full.
    [[nodiscard]] std::uint32_t RegisterUVE(const TextureHandleUVE texture) {
        if (texture == kInvalidTextureHandleUVE || m_capacity == 0U) {
            return kInvalidBindlessTextureIndexUVE;
        }
        const auto existing = m_textureToSlot.find(texture.value);
        if (existing != m_textureToSlot.end()) {
            return existing->second;
        }
        std::uint32_t slot = kInvalidBindlessTextureIndexUVE;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_slots[slot] = texture;
        } else {
            if (m_slots.size() >= m_capacity) {
                return kInvalidBindlessTextureIndexUVE;
            }
            slot = static_cast<std::uint32_t>(m_slots.size());
            m_slots.push_back(texture);
        }
        m_textureToSlot.emplace(texture.value, slot);
        return slot;
    }

    void UnregisterSlotUVE(const std::uint32_t slot) {
        if (slot >= m_slots.size() || m_slots[slot] == kInvalidTextureHandleUVE) {
            return;
        }
        m_textureToSlot.erase(m_slots[slot].value);
        m_slots[slot] = kInvalidTextureHandleUVE;
        m_freeSlots.push_back(slot);
    }

    void UnregisterTextureUVE(const TextureHandleUVE texture) {
        if (texture == kInvalidTextureHandleUVE) {
            return;
        }
        const auto it = m_textureToSlot.find(texture.value);
        if (it == m_textureToSlot.end()) {
            return;
        }
        const std::uint32_t slot = it->second;
        m_textureToSlot.erase(it);
        if (slot < m_slots.size()) {
            m_slots[slot] = kInvalidTextureHandleUVE;
            m_freeSlots.push_back(slot);
        }
    }

    [[nodiscard]] TextureHandleUVE ResolveSlotUVE(const std::uint32_t slot) const noexcept {
        if (slot >= m_slots.size()) {
            return kInvalidTextureHandleUVE;
        }
        return m_slots[slot];
    }

    void ClearUVE() noexcept {
        m_slots.clear();
        m_freeSlots.clear();
        m_textureToSlot.clear();
    }

private:
    std::uint32_t m_capacity = kDefaultMaxBindlessTexturesUVE;
    std::vector<TextureHandleUVE> m_slots;
    std::vector<std::uint32_t> m_freeSlots;
    std::unordered_map<std::uint64_t, std::uint32_t> m_textureToSlot;
};

} // namespace UVE::Render
