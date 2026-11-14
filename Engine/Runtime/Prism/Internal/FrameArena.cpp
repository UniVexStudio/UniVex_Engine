// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/FrameArena.h"
#include <algorithm>

namespace prism {

FrameArena::FrameArena(size_t defaultBlockSize)
    : m_defaultBlockSize(std::max<size_t>(1024, defaultBlockSize)) {
  addBlock(m_defaultBlockSize);
}

FrameArena::~FrameArena() = default;

FrameArena::FrameArena(FrameArena&&) noexcept = default;
FrameArena& FrameArena::operator=(FrameArena&&) noexcept = default;

void FrameArena::addBlock(size_t minSize) {
  size_t sz = std::max(m_defaultBlockSize, minSize);
  Block b;
  b.data = std::make_unique<uint8_t[]>(sz);
  b.size = sz;
  m_totalCapacity += sz;
  m_blocks.push_back(std::move(b));
}

void* FrameArena::allocate(size_t bytes, size_t alignment) {
  if (bytes == 0) return nullptr;
  if (alignment == 0) alignment = alignof(std::max_align_t);

  while (true) {
    if (m_currentBlock >= m_blocks.size()) {
      addBlock(bytes + alignment);
    }

    Block& b = m_blocks[m_currentBlock];
    uintptr_t currentPtr = reinterpret_cast<uintptr_t>(b.data.get() + m_currentOffset);
    uintptr_t alignedPtr = (currentPtr + (alignment - 1)) & ~(alignment - 1);
    size_t padding = alignedPtr - currentPtr;

    if (m_currentOffset + padding + bytes <= b.size) {
      m_currentOffset += padding + bytes;
      m_allocatedBytes += padding + bytes;
      return reinterpret_cast<void*>(alignedPtr);
    }

    // Advance to next block
    m_currentBlock++;
    m_currentOffset = 0;
  }
}

void FrameArena::reset() {
  m_currentBlock = 0;
  m_currentOffset = 0;
  m_allocatedBytes = 0;
}

} // namespace prism
