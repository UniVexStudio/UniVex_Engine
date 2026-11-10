// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Threading Contract & Render Thread Bridge.
//
// Threading Model:
// 1. UI / Game Thread:
//    - Owns the widget tree, event dispatch, and Application instance.
//    - Ticks timers, performs layout, and records frame commands into a DrawList.
//    - Once recorded, the DrawList is immutable and thread-safe to read concurrently.
// 2. Render Thread:
//    - Asynchronously acquires completed DrawLists and submits them to the graphics backend.
//    - Does not touch widgets or Application state.
// 3. RenderThreadBridge:
//    - Coordinates lock-free / thread-safe handoff between producer (game thread) and consumer (render thread).
//    - Double/triple buffers DrawLists with recycled command/vertex pools to guarantee
//      zero steady-state heap allocations.
#pragma once
#include "DrawList.h"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <memory>
#include <atomic>

namespace prism {

class RenderThreadBridge {
public:
  explicit RenderThreadBridge(size_t bufferCount = 3) : m_bufferCount(std::max<size_t>(2, bufferCount)) {
    for (size_t i = 0; i < m_bufferCount; i++) {
      m_freePool.push_back(std::make_unique<DrawList>());
    }
  }

  ~RenderThreadBridge() {
    stop();
  }

  // Called by Game/UI Thread to acquire a clean DrawList buffer for recording.
  // Reuses previously allocated DrawLists to ensure zero per-frame allocations.
  std::unique_ptr<DrawList> acquireRecordBuffer(int width, int height) {
    std::unique_ptr<DrawList> dl;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_freePool.empty()) {
        dl = std::move(m_freePool.back());
        m_freePool.pop_back();
      }
    }
    if (!dl) {
      dl = std::make_unique<DrawList>();
    }
    dl->clear();
    dl->width = width;
    dl->height = height;
    return dl;
  }

  // Called by Game/UI Thread to publish a completed, immutable frame to the render thread.
  void publishFrame(std::unique_ptr<DrawList> frame) {
    if (!frame) return;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_stopped) return;
      m_readyQueue.push(std::move(frame));
    }
    m_cv.notify_one();
  }

  // Called by Render Thread to wait for and acquire the next available frame.
  // Returns nullptr if the bridge is stopped.
  std::unique_ptr<DrawList> acquireRenderFrame(bool wait = true) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (wait) {
      m_cv.wait(lock, [&] { return !m_readyQueue.empty() || m_stopped; });
    }
    if (m_readyQueue.empty()) return nullptr;
    auto frame = std::move(m_readyQueue.front());
    m_readyQueue.pop();
    return frame;
  }

  // Called by Render Thread to recycle the processed DrawList back to the pool.
  void recycleFrame(std::unique_ptr<DrawList> frame) {
    if (!frame) return;
    frame->clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_freePool.size() < m_bufferCount + 2) {
      m_freePool.push_back(std::move(frame));
    }
  }

  // Signals workers to stop and unblocks waiting threads.
  void stop() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stopped = true;
    }
    m_cv.notify_all();
  }

  bool isStopped() const {
    return m_stopped.load();
  }

private:
  size_t m_bufferCount;
  std::atomic<bool> m_stopped{false};
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<std::unique_ptr<DrawList>> m_freePool;
  std::queue<std::unique_ptr<DrawList>> m_readyQueue;
};

} // namespace prism
