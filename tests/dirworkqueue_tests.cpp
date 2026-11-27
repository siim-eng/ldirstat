#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "dirworkqueue.h"

namespace {

using ldirstat::DirWorkQueue;
using ldirstat::EntryRef;

EntryRef ref(uint16_t index) {
    return EntryRef{0, index};
}

} // namespace

TEST_CASE("take returns the root, then reports completion once it is finished") {
    std::atomic<bool> stop{false};
    DirWorkQueue queue(stop);
    queue.reset(ref(7));

    CHECK(queue.take().value_or(ldirstat::kNoEntry) == ref(7));

    queue.finish({});
    CHECK_FALSE(queue.take().has_value());
}

TEST_CASE("queue stays FIFO across compaction") {
    std::atomic<bool> stop{false};
    DirWorkQueue queue(stop);
    queue.reset(ref(0));
    REQUIRE(queue.take().has_value());

    constexpr uint16_t kCount = static_cast<uint16_t>(DirWorkQueue::kCompactThreshold * 3);
    std::vector<EntryRef> batch;
    for (uint16_t i = 1; i <= kCount; ++i)
        batch.push_back(ref(i));
    queue.finish(batch);

    // Each take() is a new in-progress directory; finish them all at the end.
    for (uint16_t i = 1; i <= kCount; ++i) {
        REQUIRE(queue.take().value_or(ldirstat::kNoEntry) == ref(i));
    }
    for (uint16_t i = 1; i <= kCount; ++i)
        queue.finish({});

    CHECK_FALSE(queue.take().has_value());
}

TEST_CASE("take returns nullopt once stopped, even with queued work") {
    std::atomic<bool> stop{false};
    DirWorkQueue queue(stop);
    queue.reset(ref(1));
    stop = true;
    CHECK_FALSE(queue.take().has_value());
}

TEST_CASE("wakeAll releases a worker waiting on an in-progress directory after stop") {
    using namespace std::chrono_literals;

    std::atomic<bool> stop{false};
    DirWorkQueue queue(stop);
    queue.reset(ref(1));
    REQUIRE(queue.take().has_value()); // root stays in progress, so the next take() waits

    auto waiter = std::async(std::launch::async, [&queue] { return queue.take(); });
    std::this_thread::sleep_for(50ms);
    stop = true;
    queue.wakeAll();
    const bool wokeByStop = waiter.wait_for(5s) == std::future_status::ready;

    // Finish the root like a real worker would, queueing more work: the waiter
    // must still return nullopt because stop is set.
    queue.finish({ref(2)});
    CHECK_FALSE(waiter.get().has_value());
    CHECK(wokeByStop);
}

TEST_CASE("concurrent workers visit every directory exactly once and then stop") {
    // Implicit binary tree: directory n has subdirectories 2n+1 and 2n+2.
    constexpr uint16_t kDirCount = 20000;
    constexpr int kWorkerCount = 8;

    std::atomic<bool> stop{false};
    DirWorkQueue queue(stop);
    queue.reset(ref(0));

    std::vector<std::atomic<int>> visits(kDirCount);
    {
        std::vector<std::jthread> workers;
        for (int w = 0; w < kWorkerCount; ++w) {
            workers.emplace_back([&queue, &visits] {
                std::vector<EntryRef> subdirs;
                while (const auto dir = queue.take()) {
                    visits[dir->index].fetch_add(1, std::memory_order_relaxed);
                    for (uint32_t child = 2u * dir->index + 1; child <= 2u * dir->index + 2 && child < kDirCount; ++child)
                        subdirs.push_back(ref(static_cast<uint16_t>(child)));
                    queue.finish(subdirs);
                    subdirs.clear();
                }
            });
        }
    }

    int wrong = 0;
    for (const auto &count : visits)
        wrong += count.load() == 1 ? 0 : 1;
    CHECK(wrong == 0);
}
