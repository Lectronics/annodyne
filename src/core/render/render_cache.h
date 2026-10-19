#pragma once

#include "core/render/render_result.h"

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

// RenderCache: LRU of finished renders keyed by the canonical render key (architecture spec §4.6,
// ADR-0006). Design: docs/design/phase2-render-skeleton.md.

namespace anno {

/// Invariant: size_bytes() <= max_bytes. Safe to share between threads (one mutex); it allocates
/// and locks, so it is NOT real-time safe (Phase 9 adds a lock-free reader).
class RenderCache {
public:
    explicit RenderCache(std::size_t max_bytes);

    /// Null on a miss. Marks the entry most recently used. Thread-safe; not real-time safe.
    [[nodiscard]] std::shared_ptr<const RenderResult> get(std::string_view key);

    /// Inserts or replaces an entry, then evicts least-recently-used entries while over budget. An
    /// entry larger than the whole budget is dropped silently. Pre: value is not null.
    /// Thread-safe; not real-time safe.
    void put(std::string key, std::shared_ptr<const RenderResult> value);

    /// Bytes counted: the audio samples, the key and the note id strings of every entry.
    [[nodiscard]] std::size_t size_bytes() const;
    [[nodiscard]] std::size_t entry_count() const;
    void clear();

private:
    struct Entry {
        std::string key;
        std::shared_ptr<const RenderResult> value;
        std::size_t bytes;
    };

    mutable std::mutex mutex_;
    std::list<Entry> lru_;  // front = most recently used
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
    std::size_t bytes_ = 0;
    std::size_t max_bytes_;
};

}  // namespace anno
