#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

#include "direntry.h"

namespace ldirstat {

// FIFO for scanner workers. A worker must call finish() once for each directory
// it takes, even after a stop request. The scan is complete when the queue is
// empty and no directories are in progress.
class DirWorkQueue {
public:
    explicit DirWorkQueue(const std::atomic<bool> &stopFlag)
        : stop_(stopFlag) {}

    // Replace all work with root before starting workers.
    void reset(EntryRef root) {
        std::lock_guard lock(mutex_);
        pending_.clear();
        pending_.push_back(root);
        next_ = 0;
        inProgress_ = 0;
    }

    // Wait for a directory. Return nullopt on completion or stop, even if
    // directories are still queued when the stop flag is set.
    std::optional<EntryRef> take() {
        std::unique_lock lock(mutex_);
        while (true) {
            if (stop_.load(std::memory_order_relaxed)) return std::nullopt;
            if (next_ < pending_.size()) {
                const EntryRef dir = pending_[next_++];
                compactLocked();
                ++inProgress_;
                return dir;
            }
            if (inProgress_ == 0) return std::nullopt;
            cv_.wait(lock);
        }
    }

    // Queue the discovered subdirectories and mark their parent finished.
    void finish(const std::vector<EntryRef> &subdirs) {
        std::lock_guard lock(mutex_);
        pending_.insert(pending_.end(), subdirs.begin(), subdirs.end());
        --inProgress_;
        // Even an empty batch can complete the scan and release all waiters.
        cv_.notify_all();
    }

    // Call after setting the stop flag. A worker that misses this wakeup can
    // only wait while a directory is in progress, so finish() will wake it.
    void wakeAll() { cv_.notify_all(); }

    static constexpr size_t kCompactThreshold = 4096;

private:
    // Remove consumed entries in batches to avoid shifting the vector on every take().
    void compactLocked() {
        if (next_ == 0) return;
        if (next_ == pending_.size()) {
            pending_.clear();
            next_ = 0;
            return;
        }

        if (next_ < kCompactThreshold && next_ * 2 < pending_.size()) return;

        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(next_));
        next_ = 0;
    }

    const std::atomic<bool> &stop_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<EntryRef> pending_;
    size_t next_ = 0; // Index of the first queued directory.
    int inProgress_ = 0;
};

} // namespace ldirstat
