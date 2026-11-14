// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/tile_cache.hpp"

#include <cstring>
#include <functional>

namespace lvd::gui {

std::uint64_t scaleBitsOf(double timePerPixel) noexcept {
    std::uint64_t bits;
    std::memcpy(&bits, &timePerPixel, sizeof bits);
    return bits;
}

std::size_t TileKeyHash::operator()(const TileKey& k) const noexcept {
    std::size_t h = std::hash<const void*>{}(k.row);
    auto mix = [&h](std::uint64_t v) {
        h ^= std::hash<std::uint64_t>{}(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    mix(k.revision);
    mix(k.scaleBits);
    mix(static_cast<std::uint64_t>(k.tile));
    mix(static_cast<std::uint64_t>(k.heightPx));
    return h;
}

const QImage* TileCache::find(const TileKey& key) {
    auto it = map_.find(key);
    if (it == map_.end()) {
        ++misses_;
        return nullptr;
    }
    ++hits_;
    order_.splice(order_.begin(), order_, it->second);
    return &it->second->image;
}

void TileCache::put(const TileKey& key, QImage image) {
    auto it = map_.find(key);
    if (it != map_.end()) {
        bytes_ -= static_cast<std::size_t>(it->second->image.sizeInBytes());
        bytes_ += static_cast<std::size_t>(image.sizeInBytes());
        it->second->image = std::move(image);
        order_.splice(order_.begin(), order_, it->second);
    } else {
        bytes_ += static_cast<std::size_t>(image.sizeInBytes());
        order_.push_front({key, std::move(image)});
        map_[key] = order_.begin();
    }
    // Evict the least recently used entries, but never the one just put.
    while (map_.size() > 1 && (map_.size() > capacity_ || (maxBytes_ != 0 && bytes_ > maxBytes_))) {
        bytes_ -= static_cast<std::size_t>(order_.back().image.sizeInBytes());
        map_.erase(order_.back().key);
        order_.pop_back();
    }
}

void TileCache::clear() {
    map_.clear();
    order_.clear();
    bytes_ = 0;
}

} // namespace lvd::gui
