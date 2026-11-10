// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - monotonic bump allocator for per-frame scratch memory.
// Steady-state frames reuse pre-allocated blocks with zero OS/heap allocations.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace prism {

class FrameArena {
public:
  explicit FrameArena(size_t defaultBlockSize = 64 * 1024);
  ~FrameArena();

  FrameArena(const FrameArena&) = delete;
  FrameArena& operator=(const FrameArena&) = delete;
  FrameArena(FrameArena&&) noexcept;
  FrameArena& operator=(FrameArena&&) noexcept;

  // Allocate raw bytes aligned to alignment.
  void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));

  // Construct T inside arena memory (note: destructors are not invoked on reset).
  template <typename T, typename... Args>
  T* create(Args&&... args) {
    void* ptr = allocate(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
  }

  // Allocate an array of Ts.
  template <typename T>
  T* allocateArray(size_t count) {
    return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
  }

  // Reset the allocation watermark back to zero without freeing existing blocks.
  void reset();

  size_t allocatedBytes() const { return m_allocatedBytes; }
  size_t capacity() const { return m_totalCapacity; }
  size_t blockCount() const { return m_blocks.size(); }

private:
  struct Block {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
  };

  size_t m_defaultBlockSize;
  std::vector<Block> m_blocks;
  size_t m_currentBlock = 0;
  size_t m_currentOffset = 0;
  size_t m_allocatedBytes = 0;
  size_t m_totalCapacity = 0;

  void addBlock(size_t minSize);
};

} // namespace prism
