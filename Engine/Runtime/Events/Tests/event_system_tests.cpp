// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "event_system.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Events::Tests {
namespace {

struct TestEvent {
    int value = 0;
};

struct OtherTestEvent {
    std::string text;
};

class HandlerHost {
public:
    void OnTestEvent(const TestEvent& event) { receivedValue = event.value; }
    int receivedValue = -1;
};

TEST(EventSystemTest, Publish_InvokesSubscribedHandler) {
    EventSystem eventSystem;
    int received = -1;
    eventSystem.Subscribe<TestEvent>([&received](const TestEvent& event) { received = event.value; });

    eventSystem.Publish(TestEvent{42});

    EXPECT_EQ(received, 42);
}

TEST(EventSystemTest, Publish_MultipleSubscribers_AllInvoked) {
    EventSystem eventSystem;
    int callCount = 0;
    for (int i = 0; i < 3; ++i) {
        eventSystem.Subscribe<TestEvent>([&callCount](const TestEvent&) { ++callCount; });
    }

    eventSystem.Publish(TestEvent{1});

    EXPECT_EQ(callCount, 3);
}

TEST(EventSystemTest, Publish_DifferentEventTypes_DoNotCrossFire) {
    EventSystem eventSystem;
    bool testEventFired = false;
    bool otherEventFired = false;
    eventSystem.Subscribe<TestEvent>([&testEventFired](const TestEvent&) { testEventFired = true; });
    eventSystem.Subscribe<OtherTestEvent>(
        [&otherEventFired](const OtherTestEvent&) { otherEventFired = true; });

    eventSystem.Publish(TestEvent{1});

    EXPECT_TRUE(testEventFired);
    EXPECT_FALSE(otherEventFired);
}

TEST(EventSystemTest, Unsubscribe_StopsFutureDelivery) {
    EventSystem eventSystem;
    int callCount = 0;
    const EventSubscription subscription =
        eventSystem.Subscribe<TestEvent>([&callCount](const TestEvent&) { ++callCount; });

    eventSystem.Unsubscribe(subscription);
    eventSystem.Publish(TestEvent{1});

    EXPECT_EQ(callCount, 0);
}

TEST(EventSystemTest, Unsubscribe_InvalidHandle_IsNoOp) {
    EventSystem eventSystem;
    int callCount = 0;
    eventSystem.Subscribe<TestEvent>([&callCount](const TestEvent&) { ++callCount; });

    eventSystem.Unsubscribe(EventSubscription{});
    eventSystem.Publish(TestEvent{1});

    EXPECT_EQ(callCount, 1);
}

TEST(EventSystemTest, MemberFunctionSubscribe_Invoked) {
    EventSystem eventSystem;
    HandlerHost host;
    eventSystem.Subscribe<TestEvent>(&host, &HandlerHost::OnTestEvent);

    eventSystem.Publish(TestEvent{7});

    EXPECT_EQ(host.receivedValue, 7);
}

TEST(EventSystemTest, QueueEvent_NotDeliveredUntilDispatchQueued) {
    EventSystem eventSystem;
    int callCount = 0;
    eventSystem.Subscribe<TestEvent>([&callCount](const TestEvent&) { ++callCount; });

    eventSystem.QueueEvent(TestEvent{1});
    EXPECT_EQ(callCount, 0);

    eventSystem.DispatchQueued();
    EXPECT_EQ(callCount, 1);
}

TEST(EventSystemTest, DispatchQueued_PreservesSamePriorityOrder) {
    EventSystem eventSystem;
    std::vector<int> order;
    eventSystem.Subscribe<TestEvent>([&order](const TestEvent& event) { order.push_back(event.value); });

    eventSystem.QueueEvent(TestEvent{1});
    eventSystem.QueueEvent(TestEvent{2});
    eventSystem.QueueEvent(TestEvent{3});
    eventSystem.DispatchQueued();

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(EventSystemTest, DispatchQueued_CriticalBeforeHighBeforeNormal) {
    EventSystem eventSystem;
    std::vector<int> order;
    eventSystem.Subscribe<TestEvent>([&order](const TestEvent& event) { order.push_back(event.value); });

    eventSystem.QueueEvent(TestEvent{1}, EventPriority::Normal);
    eventSystem.QueueEvent(TestEvent{2}, EventPriority::Critical);
    eventSystem.QueueEvent(TestEvent{3}, EventPriority::High);
    eventSystem.QueueEvent(TestEvent{4}, EventPriority::Critical);
    eventSystem.DispatchQueued();

    EXPECT_EQ(order, (std::vector<int>{2, 4, 3, 1}));
}

TEST(EventSystemTest, DispatchQueued_ClearsQueueAfterDispatch) {
    EventSystem eventSystem;
    int callCount = 0;
    eventSystem.Subscribe<TestEvent>([&callCount](const TestEvent&) { ++callCount; });

    eventSystem.QueueEvent(TestEvent{1});
    eventSystem.DispatchQueued();
    eventSystem.DispatchQueued();

    EXPECT_EQ(callCount, 1);
}

TEST(EventSystemTest, Clear_RemovesSubscriptionsAndQueue) {
    EventSystem eventSystem;
    int callCount = 0;
    eventSystem.Subscribe<TestEvent>([&callCount](const TestEvent&) { ++callCount; });
    eventSystem.QueueEvent(TestEvent{1});

    eventSystem.Clear();
    eventSystem.DispatchQueued();
    eventSystem.Publish(TestEvent{1});

    EXPECT_EQ(callCount, 0);
}

} // namespace
} // namespace UVE::Events::Tests
