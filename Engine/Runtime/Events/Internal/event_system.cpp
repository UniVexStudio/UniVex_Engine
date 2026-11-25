// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "event_system.h"

#include <algorithm>
#include <utility>

namespace UVE::Events {

namespace {
constexpr std::size_t PriorityIndex(EventPriority priority) {
    return static_cast<std::size_t>(priority);
}
} // namespace

std::uint64_t EventSystem::SubscribeErased(std::type_index eventType,
                                                std::function<void(const void*)> handler) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const std::uint64_t id = m_nextId++;
    m_handlers[eventType].push_back(HandlerEntry{id, std::move(handler)});
    return id;
}

void EventSystem::Unsubscribe(const EventSubscription& subscription) {
    if (!subscription.IsValid()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto handlerListIt = m_handlers.find(subscription.eventType);
    if (handlerListIt == m_handlers.end()) {
        return;
    }
    std::vector<HandlerEntry>& entries = handlerListIt->second;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                  [&subscription](const HandlerEntry& entry) {
                                      return entry.id == subscription.id;
                                  }),
                  entries.end());
}

void EventSystem::PublishErased(std::type_index eventType, const void* eventPtr) {
    // Snapshot the handler list under lock, then invoke outside the lock.
    // This makes Unsubscribe-during-dispatch safe: a handler unsubscribing
    // itself or another handler mid-Publish never corrupts internal state.
    // Documented trade-off: a handler that unsubscribes after the snapshot
    // was already taken still fires once more in this same Publish call.
    std::vector<HandlerEntry> snapshot;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        const auto handlerListIt = m_handlers.find(eventType);
        if (handlerListIt == m_handlers.end()) {
            return;
        }
        snapshot = handlerListIt->second;
    }
    for (const HandlerEntry& entry : snapshot) {
        entry.callback(eventPtr);
    }
}

void EventSystem::QueueErased(std::type_index /*eventType*/, EventPriority priority,
                                  std::function<void()> dispatchClosure) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_queuesByPriority[PriorityIndex(priority)].push_back(std::move(dispatchClosure));
}

void EventSystem::DispatchQueued() {
    static constexpr EventPriority kDispatchOrder[] = {
        EventPriority::Critical, EventPriority::High, EventPriority::Normal};

    for (const EventPriority priority : kDispatchOrder) {
        std::deque<std::function<void()>> drained;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            drained.swap(m_queuesByPriority[PriorityIndex(priority)]);
        }
        for (const std::function<void()>& dispatchClosure : drained) {
            dispatchClosure();
        }
    }
}

void EventSystem::Clear() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers.clear();
    for (std::deque<std::function<void()>>& queue : m_queuesByPriority) {
        queue.clear();
    }
}

} // namespace UVE::Events
