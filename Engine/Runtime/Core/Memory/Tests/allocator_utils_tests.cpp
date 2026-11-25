// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "allocator_utils.h"

#include <gtest/gtest.h>

#include "heap_allocator.h"

namespace UVE::Memory::Tests {
namespace {

struct TestObject {
    TestObject(int valueIn, bool* destructorFlagIn) : value(valueIn), destructorFlag(destructorFlagIn) {}
    ~TestObject() {
        if (destructorFlag != nullptr) {
            *destructorFlag = true;
        }
    }

    int value;
    bool* destructorFlag;
};

TEST(AllocatorUtilsTest, Construct_RunsConstructorWithForwardedArgs) {
    HeapAllocator allocator;
    bool destructorCalled = false;

    TestObject* const object =
        Construct<TestObject>(allocator, __FILE__, __LINE__, 42, &destructorCalled);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->value, 42);
    EXPECT_FALSE(destructorCalled);

    Destroy(allocator, object);
}

TEST(AllocatorUtilsTest, Destroy_CallsDestructorBeforeDeallocating) {
    HeapAllocator allocator;
    bool destructorCalled = false;
    TestObject* const object =
        Construct<TestObject>(allocator, __FILE__, __LINE__, 7, &destructorCalled);
    ASSERT_EQ(allocator.GetAllocatedBytes(), sizeof(TestObject));

    Destroy(allocator, object);
    EXPECT_TRUE(destructorCalled);
    EXPECT_EQ(allocator.GetAllocatedBytes(), 0U);
}

TEST(AllocatorUtilsTest, Destroy_NullObject_IsNoOp) {
    HeapAllocator allocator;
    Destroy<TestObject>(allocator, nullptr); // must not crash
}

TEST(AllocatorUtilsTest, UveConstructMacro_CapturesSourceLocationAutomatically) {
    HeapAllocator allocator;
    bool destructorCalled = false;
    TestObject* const object = UVE_CONSTRUCT(allocator, TestObject, 5, &destructorCalled);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->value, 5);
    Destroy(allocator, object);
}

} // namespace
} // namespace UVE::Memory::Tests
