// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "memory_manager.h"

#include "engine_assert.h"
#include "logging_macros.h"

namespace UVE::Memory {

MemoryManager::MemoryManager() : m_defaultAllocator(this, "MemoryManager::DefaultAllocator") {}

void MemoryManager::RecordAllocation(void* pointer, std::size_t sizeBytes,
                                            std::size_t alignment, std::string_view allocatorTag,
                                            const char* sourceFile, int sourceLine) {
    AllocationRecord record;
    record.pointer = pointer;
    record.sizeBytes = sizeBytes;
    record.alignment = alignment;
    record.allocatorTag = std::string(allocatorTag);
    record.sourceFile = sourceFile;
    record.sourceLine = sourceLine;
    record.allocationId = m_nextAllocationId.fetch_add(1, std::memory_order_relaxed);

    const std::lock_guard<std::mutex> lock(m_mutex);
    m_activeAllocations.emplace(pointer, record);
    m_activeBytes += sizeBytes;
    ++m_totalAllocationsEver;
    if (m_activeBytes > m_peakBytes) {
        m_peakBytes = m_activeBytes;
    }
}

void MemoryManager::RecordDeallocation(void* pointer) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto activeIt = m_activeAllocations.find(pointer);
    UVE_ASSERT(activeIt != m_activeAllocations.end());
    m_activeBytes -= activeIt->second.sizeBytes;
    m_activeAllocations.erase(activeIt);
}

IAllocator& MemoryManager::GetDefaultAllocator() {
    return m_defaultAllocator;
}

std::size_t MemoryManager::GetActiveAllocationCount() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeAllocations.size();
}

std::size_t MemoryManager::GetActiveBytes() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeBytes;
}

std::size_t MemoryManager::GetPeakBytes() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_peakBytes;
}

std::uint64_t MemoryManager::GetTotalAllocationsEver() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalAllocationsEver;
}

bool MemoryManager::HasLeaks() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return !m_activeAllocations.empty();
}

std::vector<AllocationRecord> MemoryManager::GetLeakedAllocations() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AllocationRecord> leaks;
    leaks.reserve(m_activeAllocations.size());
    for (const auto& activeEntry : m_activeAllocations) {
        leaks.push_back(activeEntry.second);
    }
    return leaks;
}

void MemoryManager::LogLeakReport() {
    const std::vector<AllocationRecord> leaks = GetLeakedAllocations();
    for (const AllocationRecord& leak : leaks) {
        UVE_ERROR("Memory leak: allocation #{} ({} bytes, allocator=\"{}\", source={}:{})",
                  leak.allocationId, leak.sizeBytes, leak.allocatorTag,
                  leak.sourceFile != nullptr ? leak.sourceFile : "<unknown>", leak.sourceLine);
    }
    if (!leaks.empty()) {
        UVE_ERROR("Memory leak summary: {} leaked allocation(s), {} byte(s) total", leaks.size(),
                  GetActiveBytes());
    }
}

} // namespace UVE::Memory
