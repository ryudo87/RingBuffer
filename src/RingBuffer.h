#pragma once

#include <atomic>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// Lock-free SPSC ring buffer
// ─────────────────────────────────────────────────────────────────────────────

template<typename T, size_t Size>
class RingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");
    static constexpr size_t MASK = Size - 1;

    // Each index on its own cache line to prevent false sharing between
    // producer and consumer cores.
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
    alignas(64) T buffer_[Size];

public:
    // ── Producer side ─────────────────────────────────────────────────────
    // Called from exactly ONE producer thread.
    //
    // Memory ordering rationale:
    //   • write_pos_ loaded relaxed  — only this thread ever writes it;
    //     no synchronisation needed with itself.
    //   • read_pos_  loaded acquire  — synchronises with the consumer's
    //     release store of read_pos_, so we see freed slots immediately.
    //   • write_pos_ stored release  — publishes the written element to the
    //     consumer; the consumer's acquire load of write_pos_ (in pop) sees
    //     every store done before this point.
    bool push(const T& item) {
        const size_t write = write_pos_.load(std::memory_order_relaxed);
        const size_t read  = read_pos_.load(std::memory_order_acquire);

        if ((write - read) == Size)      // buffer full (unsigned wrap-safe)
            return false;

        buffer_[write & MASK] = item;
        write_pos_.store(write + 1, std::memory_order_release);
        return true;
    }

    // ── Consumer side ─────────────────────────────────────────────────────
    // Called from exactly ONE consumer thread.
    //
    // Memory ordering rationale (mirror of push):
    //   • read_pos_  loaded relaxed  — only this thread ever writes it.
    //   • write_pos_ loaded acquire  — synchronises with the producer's
    //     release store of write_pos_; guarantees we see the written data.
    //   • read_pos_  stored release  — notifies the producer of freed slots.
    bool pop(T& item) {
        const size_t read  = read_pos_.load(std::memory_order_relaxed);
        const size_t write = write_pos_.load(std::memory_order_acquire);

        if (read == write)               // buffer empty
            return false;

        item = buffer_[read & MASK];
        read_pos_.store(read + 1, std::memory_order_release);
        return true;
    }
};
