// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>

#include <cstdint>
#include <list>
#include <unordered_map>

namespace lvd::gui {

/// Identifies a rendered tile. `row` is any stable identity of the row and `revision` changes whenever
/// the row's appearance or data changes, which makes older tiles unreachable (they age out of the LRU).
struct TileKey {
    const void* row = nullptr;
    std::uint64_t revision = 0;
    std::uint64_t scaleBits = 0; ///< bit pattern of timePerPixel
    long long tile = 0;
    int heightPx = 0;

    bool operator==(const TileKey& o) const noexcept {
        return row == o.row && revision == o.revision && scaleBits == o.scaleBits && tile == o.tile &&
               heightPx == o.heightPx;
    }
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const noexcept;
};

std::uint64_t scaleBitsOf(double timePerPixel) noexcept;

/// Least-recently-used cache of tile images with a fixed entry limit.
class TileCache {
public:
    explicit TileCache(std::size_t capacity = 512)
        : capacity_(capacity) {}

    /// Returns the cached image and marks it recently used, or nullptr. The pointer is valid until the
    /// next call that modifies the cache.
    const QImage* find(const TileKey& key);
    void put(const TileKey& key, QImage image);
    void clear();
    std::size_t size() const noexcept { return map_.size(); }
    std::size_t hits() const noexcept { return hits_; }
    std::size_t misses() const noexcept { return misses_; }

private:
    struct Entry {
        TileKey key;
        QImage image;
    };
    std::size_t capacity_;
    std::list<Entry> order_; // front = most recently used
    std::unordered_map<TileKey, std::list<Entry>::iterator, TileKeyHash> map_;
    std::size_t hits_ = 0, misses_ = 0;
};

} // namespace lvd::gui
