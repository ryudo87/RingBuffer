/*
 * Lock-free Single-Producer / Single-Consumer (SPSC) ring buffer
 *
 * Correctness guarantees
 * ──────────────────────
 *  • Power-of-two capacity → index masking, no modulo
 *  • Full condition  : write_pos - read_pos == Size  (unsigned wrap-safe)
 *  • Empty condition : write_pos == read_pos
 *  • Acquire/release pairs create the required happens-before edges so that
 *    the consumer always sees data written before the producer's store, and
 *    the producer always sees the consumer's freed slots.
 *  • No false sharing: each atomic lives on its own cache line.
 *  → race-free and verifiable with -fsanitize=thread (ThreadSanitizer)
 *
 * Consumer-thread tuning (Linux)
 * ──────────────────────────────
 *  • CPU affinity  — dedicated core, no OS task migration
 *  • SCHED_FIFO    — real-time scheduler, no preemption by normal tasks
 *  • mlockall      — wire all pages, zero page-fault latency at runtime
 *  • stack prefault — first-touch every stack page before hot path
 *  • MADV_HUGEPAGE — THP hint for the ring buffer backing store
 */

#include <iostream>
#include <atomic>
#include <thread>

#include "RingBuffer.h"
#include <chrono>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
#include <numeric>

// Linux-specific
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// Consumer thread initialisation
// ─────────────────────────────────────────────────────────────────────────────

static void initializeConsumerThread(int cpu_id) {
    // 1. Pin to a dedicated CPU core — eliminates cross-core migrations and
    //    the associated TLB/cache invalidation jitter.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0)
        std::cerr << "[warn] CPU affinity not set (requires CAP_SYS_NICE)\n";

    // 2. Real-time scheduler — SCHED_FIFO prevents preemption by normal-class
    //    tasks, giving the consumer deterministic CPU access.
    sched_param param{};
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
        std::cerr << "[warn] SCHED_FIFO not set (requires CAP_SYS_NICE)\n";

    // 3. Lock all mapped pages into RAM — mlockall(MCL_CURRENT | MCL_FUTURE)
    //    ensures neither existing nor future pages are ever swapped out,
    //    eliminating major fault latency in the hot path.
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        std::cerr << "[warn] mlockall failed (requires CAP_IPC_LOCK)\n";

    // 4. Prefault the thread stack — touching every page now converts lazy
    //    first-touch faults into a one-time setup cost before the hot path.
    constexpr size_t STACK_PREFAULT = 64 * 1024;   // 64 KiB
    volatile char stk[STACK_PREFAULT];
    std::memset(const_cast<char*>(stk), 0, STACK_PREFAULT);
    (void)stk;
}

// Advise the kernel to back a region with transparent huge pages (2 MiB TLB
// entries) to reduce TLB pressure when iterating over the ring buffer.
static void adviseHugePages(void* addr, size_t size) {
    if (madvise(addr, size, MADV_HUGEPAGE) != 0)
        std::cerr << "[warn] madvise(MADV_HUGEPAGE) failed\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

// Message carries a nanosecond send-timestamp and a sequence number so the
// consumer can verify strict FIFO ordering.
struct Message {
    uint64_t send_ns;
    uint64_t seq;
};

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    constexpr size_t RING_SIZE   = 1 << 14;        // 16 384  slots
    constexpr size_t WARMUP_MSGS = 10'000;
    constexpr size_t BENCH_MSGS  = 200'000;
    constexpr size_t TOTAL_MSGS  = WARMUP_MSGS + BENCH_MSGS;

    // Discover available CPUs; use core 0 for producer, first other for consumer.
    const int producer_cpu = 0;
    const int consumer_cpu = ([]() -> int {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        sched_getaffinity(0, sizeof(cs), &cs);
        for (int i = 1; i < CPU_SETSIZE; ++i)
            if (CPU_ISSET(i, &cs)) return i;
        return 0;   // fallback: same core (still works, lower perf)
    })();

    std::cout << "--Producer CPU : " << producer_cpu
              << "\n--Consumer CPU : " << consumer_cpu << "\n\n";

    using Buffer = RingBuffer<Message, RING_SIZE>;

    // Heap-allocate so we can apply madvise to the whole buffer region.
    auto* rb = new Buffer{};
    adviseHugePages(rb, sizeof(Buffer));

    std::vector<uint64_t> latencies;
    latencies.reserve(BENCH_MSGS);

    std::atomic<bool> consumer_ready{false};

    // ── Consumer thread ───────────────────────────────────────────────────────
    std::thread consumer([&] {
        initializeConsumerThread(consumer_cpu);
        consumer_ready.store(true, std::memory_order_release);

        uint64_t expected_seq = 0;
        size_t   received     = 0;
        Message  msg;

        while (received < TOTAL_MSGS) {
            while (!rb->pop(msg)) { /* spin-wait */ }

            const uint64_t recv_ns = now_ns();
            assert(msg.seq == expected_seq++ && "FIFO order violated!");

            if (received >= WARMUP_MSGS)
                latencies.push_back(recv_ns - msg.send_ns);

            ++received;
        }
    });

    // ── Producer (main thread) ────────────────────────────────────────────────
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(producer_cpu, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }

    while (!consumer_ready.load(std::memory_order_acquire)) { /* spin */ }

    const uint64_t t0 = now_ns();

    for (size_t i = 0; i < TOTAL_MSGS; ++i) {
        Message msg{now_ns(), i};
        while (!rb->push(msg)) { /* spin until slot free */ }
    }

    consumer.join();
    const uint64_t t1 = now_ns();

    // ── Results ───────────────────────────────────────────────────────────────
    std::sort(latencies.begin(), latencies.end());

    const double elapsed_s  = (t1 - t0) / 1e9;
    const double throughput = static_cast<double>(BENCH_MSGS) / elapsed_s;
    const double avg_ns     = std::accumulate(latencies.begin(), latencies.end(),
                                              uint64_t{0}) /
                              static_cast<double>(latencies.size());

    auto pct = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p / 100.0 * latencies.size());
        if (idx >= latencies.size()) idx = latencies.size() - 1;
        return latencies[idx];
    };

    std::cout << "\n\n── SPSC Ring Buffer Benchmark ──────────────────────────\n"
              << "Messages (bench) : " << BENCH_MSGS            << "\n"
              << "Elapsed          : " << elapsed_s * 1e3       << " ms\n"
              << "Throughput       : " << throughput / 1e6      << " Mmsg/s\n"
              << "\nLatency (ns):\n"
              << "  min   : " << latencies.front()   << "\n"
              << "  avg   : " << static_cast<uint64_t>(avg_ns) << "\n"
              << "  p50   : " << pct(50)             << "\n"
              << "  p90   : " << pct(90)             << "\n"
              << "  p99   : " << pct(99)             << "\n"
              << "  p99.9 : " << pct(99.9)           << "\n"
              << "  max   : " << latencies.back()    << "\n"
              << "────────────────────────────────────────────────────────\n";

    delete rb;
    return 0;
}

