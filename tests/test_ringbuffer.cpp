#include <gtest/gtest.h>
#include "../src/RingBuffer.h"

// ─────────────────────────────────────────────────────────────────────────────
// RingBuffer::push() unit tests
// ─────────────────────────────────────────────────────────────────────────────

// push() returns true on an empty buffer
TEST(RingBufferPush, ReturnsTrueWhenEmpty) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push(42));
}

// Pushed value is retrievable via pop()
TEST(RingBufferPush, ValueIsStoredCorrectly) {
    RingBuffer<int, 4> rb;
    rb.push(99);
    int out = 0;
    ASSERT_TRUE(rb.pop(out));
    EXPECT_EQ(out, 99);
}

// push() fills all capacity slots successfully
TEST(RingBufferPush, FillsToCapacity) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_TRUE(rb.push(3));
    EXPECT_TRUE(rb.push(4));
}

// push() returns false when buffer is full
TEST(RingBufferPush, ReturnsFalseWhenFull) {
    RingBuffer<int, 4> rb;
    rb.push(1);
    rb.push(2);
    rb.push(3);
    rb.push(4);
    EXPECT_FALSE(rb.push(5));
}

// Order of elements is FIFO
TEST(RingBufferPush, FIFOOrdering) {
    RingBuffer<int, 4> rb;
    rb.push(10);
    rb.push(20);
    rb.push(30);

    int out = 0;
    rb.pop(out); EXPECT_EQ(out, 10);
    rb.pop(out); EXPECT_EQ(out, 20);
    rb.pop(out); EXPECT_EQ(out, 30);
}

// push() succeeds again after slots are freed (wrap-around)
TEST(RingBufferPush, WrapsAroundAfterPop) {
    RingBuffer<int, 4> rb;
    // Fill
    rb.push(1); rb.push(2); rb.push(3); rb.push(4);
    // Free two slots
    int out = 0;
    rb.pop(out); rb.pop(out);
    // Should accept two more
    EXPECT_TRUE(rb.push(5));
    EXPECT_TRUE(rb.push(6));
    // Still full — no more room
    EXPECT_FALSE(rb.push(7));
}

// push() works correctly with non-trivial types
TEST(RingBufferPush, WorksWithNonTrivialType) {
    RingBuffer<std::string, 4> rb;
    EXPECT_TRUE(rb.push("hello"));
    std::string out;
    ASSERT_TRUE(rb.pop(out));
    EXPECT_EQ(out, "hello");
}

// push() never accepts more than Size elements concurrently in the buffer
TEST(RingBufferPush, NeverExceedsCapacity) {
    constexpr size_t CAP = 8;
    RingBuffer<int, CAP> rb;
    size_t pushed = 0;
    for (size_t i = 0; i < CAP + 4; ++i) {
        if (rb.push(static_cast<int>(i))) ++pushed;
    }
    EXPECT_EQ(pushed, CAP);
}
