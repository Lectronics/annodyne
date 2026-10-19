#include "core/render/render_cache.h"

#include "core/types/contract.h"

#include <utility>

namespace anno {

namespace {

std::size_t entry_bytes(const std::string& key, const RenderResult& result) {
    std::size_t bytes = sizeof(RenderResult) + key.size() + result.cache_id.size();
    bytes += static_cast<std::size_t>(result.buffer.audio.channel_count()) *
             static_cast<std::size_t>(result.buffer.audio.frame_count().value()) * sizeof(float);
    for (const NoteId& id : result.notes_rendered) {
        bytes += sizeof(NoteId) + id.value.size();
    }
    return bytes;
}

}  // namespace

RenderCache::RenderCache(std::size_t max_bytes) : max_bytes_{max_bytes} {}

std::shared_ptr<const RenderResult> RenderCache::get(std::string_view key) {
    const std::scoped_lock lock{mutex_};
    const auto found = index_.find(std::string{key});
    if (found == index_.end()) {
        return nullptr;
    }
    lru_.splice(lru_.begin(), lru_, found->second);
    return found->second->value;
}

void RenderCache::put(std::string key, std::shared_ptr<const RenderResult> value) {
    ANNO_EXPECTS(value != nullptr);
    const std::scoped_lock lock{mutex_};
    if (const auto old = index_.find(key); old != index_.end()) {
        bytes_ -= old->second->bytes;
        lru_.erase(old->second);
        index_.erase(old);
    }
    const std::size_t bytes = entry_bytes(key, *value);
    if (bytes > max_bytes_) {
        return;
    }
    lru_.push_front(Entry{.key = key, .value = std::move(value), .bytes = bytes});
    index_.emplace(std::move(key), lru_.begin());
    bytes_ += bytes;
    while (bytes_ > max_bytes_) {
        const Entry& oldest = lru_.back();
        bytes_ -= oldest.bytes;
        index_.erase(oldest.key);
        lru_.pop_back();
    }
}

std::size_t RenderCache::size_bytes() const {
    const std::scoped_lock lock{mutex_};
    return bytes_;
}

std::size_t RenderCache::entry_count() const {
    const std::scoped_lock lock{mutex_};
    return lru_.size();
}

void RenderCache::clear() {
    const std::scoped_lock lock{mutex_};
    lru_.clear();
    index_.clear();
    bytes_ = 0;
}

}  // namespace anno
