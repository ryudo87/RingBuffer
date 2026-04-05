#pragma once

#include <atomic>


template<typename T, size_t Capacity, bool PlacementNew = true>
class RingBufferV2
{
    struct ByteBuffer
    {
        alignas(alignof(T)) std::byte data[sizeof(T) * Capacity];
    };
    using Storage = std::conditional_t<PlacementNew, ByteBuffer, std::array<T, Capacity>>;


    alignas(64) std::atomic<size_t> read_;
    alignas(64) std::atomic<size_t> write_;
    alignas(64) Storage storage_;

public:
    RingBufferV2() : read_(0), write_(0) {}
    ~RingBufferV2() {
        if constexpr (PlacementNew) {
            size_t read = read_.load(std::memory_order_relaxed);
            size_t write = write_.load(std::memory_order_relaxed);
            while (read != write) {
                T* item = std::launder(reinterpret_cast<T*>(storage_.data + (read % Capacity) * sizeof(T)));
                item->~T();
                ++read;
            }
        }
    }
}; 