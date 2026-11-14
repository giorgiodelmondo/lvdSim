// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/tile_cache.hpp"
#include "wave/tile_geometry.hpp"

#include <QColor>
#include <QRect>
#include <QSize>

#include <rhi/qrhi.h>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace lvd::gui {

/// A solid rectangle in target pixels (origin top-left).
struct GpuRect {
    float x = 0, y = 0, w = 0, h = 0;
    Rgba color;
};

/// Where one tile goes. The key identifies its picture (the same key as in the CPU tile cache).
struct GpuTileDraw {
    TileKey key;
    int heightPx = 0;
    float x = 0, y = 0;  ///< top-left corner in target pixels
};

/// Everything needed to draw the wave area once.
struct GpuFrame {
    QSize size;                      ///< size of the render target in pixels
    QColor background;
    std::vector<GpuRect> rects;      ///< drawn first: row backgrounds, selection, grid lines
    std::vector<GpuTileDraw> tiles;
    QRect tileClip;                  ///< tiles are clipped to this rectangle (the wave area)
    /// Builds the picture of a tile that is not on the GPU yet. Called at most once per missing tile and frame.
    std::function<TileGeometry(const GpuTileDraw&)> geometryFor;
};

/// Draws the wave area with the GPU through Qt's rendering hardware interface.
///
/// Tile pictures are rasterised once into texture atlases that stay on the GPU (a least-recently-used cache
/// keyed like the CPU tile cache). Each frame then only draws one textured quad per visible tile, so a
/// repaint after panning or moving a cursor costs a few draw calls instead of copying every tile pixel by
/// pixel on the CPU. The pictures come from buildTileGeometry(), the same source the QPainter renderer
/// uses, so both draw the same shapes.
class GpuWaveRenderer {
public:
    static constexpr int kTileWidth = 256;
    static constexpr int kAtlasWidth = 4096;
    static constexpr int kAtlasHeight = 4096;
    /// Atlases of one row height grow: the first holds 4 lines of 16 tiles, each next one four times as many, up to
    /// the full atlas size (kMaxAtlasesPerHeight of them). A row height used by a few rows (the user resized them)
    /// costs a small texture, not 64 MB.
    static constexpr int kFirstAtlasRows = 4;
    static constexpr std::size_t kMaxAtlasesPerHeight = 6;
    /// Lines of tiles of the atlas number `index` (0-based) of a row height.
    static int atlasRows(int slotHeight, std::size_t index) noexcept;
    /// Upper limit for the memory of all atlases. When a new atlas would go beyond it, the atlases of row heights
    /// that the current frame does not draw are freed first, least recently drawn first.
    static constexpr std::size_t kAtlasBudgetBytes = std::size_t{384} << 20;

    GpuWaveRenderer();
    ~GpuWaveRenderer();
    GpuWaveRenderer(const GpuWaveRenderer&) = delete;
    GpuWaveRenderer& operator=(const GpuWaveRenderer&) = delete;

    /// Creates pipelines for rendering into a target with this render pass descriptor. The atlases use
    /// `colorFormat`, which should be the format of that target. Returns false if the shaders cannot be loaded.
    bool initialize(QRhi* rhi, QRhiRenderPassDescriptor* targetPass, QRhiTexture::Format colorFormat);
    void releaseResources();
    bool isInitialized() const noexcept { return rhi_ != nullptr; }

    /// Records the passes for one frame into `cb` and draws into `target`. Must be called between the
    /// begin and end of a frame.
    void render(QRhiCommandBuffer* cb, QRhiRenderTarget* target, const GpuFrame& frame);

    /// Forget every cached tile (after the data or the palette changed).
    void clearCache();

    struct Stats {
        std::size_t tilesRasterised = 0;  ///< pictures built and drawn into an atlas this frame
        std::size_t tilesReused = 0;      ///< tiles found on the GPU
        std::size_t tilesSkipped = 0;     ///< could not be placed (atlas memory exhausted)
        std::size_t quads = 0;
    };
    const Stats& lastFrame() const noexcept { return stats_; }
    std::size_t cachedTiles() const noexcept { return slots_.size(); }
    std::size_t atlasCount() const noexcept;
    /// GPU memory held by the atlases (RGBA8 textures).
    std::size_t atlasBytes() const noexcept;
    /// Number of row heights that have atlases.
    std::size_t atlasHeights() const noexcept { return atlases_.size(); }

private:
    struct Atlas;
    struct Slot {
        Atlas* atlas = nullptr;
        int index = 0;
        std::uint64_t lastFrame = 0;
        std::list<TileKey>::iterator lru;
    };
    struct Pipelines;

    Atlas* createAtlas(int slotHeight);
    Slot* acquire(const TileKey& key, int heightPx);
    /// Frees the atlases of row heights not drawn in this frame until `needed` more bytes fit in the budget.
    void trimAtlases(std::size_t needed);
    void dropHeight(int heightPx);
    void ensureBuffer(QRhiBuffer*& buf, int& capacity, int bytes);

    QRhi* rhi_ = nullptr;
    QRhiTexture::Format format_ = QRhiTexture::RGBA8;
    QRhiRenderPassDescriptor* targetPass_ = nullptr;
    std::unique_ptr<Pipelines> pipelines_;

    QRhiBuffer* mainUbo_ = nullptr;
    QRhiBuffer* fillVbuf_ = nullptr;   ///< triangles for atlas passes and for the background rectangles
    QRhiBuffer* quadVbuf_ = nullptr;
    int fillCapacity_ = 0;
    int quadCapacity_ = 0;
    QRhiSampler* sampler_ = nullptr;

    std::map<int, std::vector<std::unique_ptr<Atlas>>> atlases_;  // by slot height
    std::map<int, std::uint64_t> heightLastFrame_;                  // the last frame that drew tiles of a height
    const Atlas* anchor_ = nullptr;  // the atlas the pipelines were created against: never freed
    std::unordered_map<TileKey, Slot, TileKeyHash> slots_;
    std::list<TileKey> lru_;  // front is the most recently used
    std::uint64_t frameNumber_ = 0;
    Stats stats_;
};

/// A GPU context without a window, for tests, benchmarks and as a probe: renders frames into a texture and
/// reads the result back as an image.
class GpuOffscreen {
public:
    /// nullptr if no GPU backend could be created; the reason is put in *error.
    static std::unique_ptr<GpuOffscreen> create(QString* error = nullptr);
    ~GpuOffscreen();

    GpuWaveRenderer& renderer() noexcept { return renderer_; }
    QRhi* rhi() const noexcept { return rhi_.get(); }
    QString backendName() const;

    /// Draws a frame and returns it as premultiplied ARGB, row 0 at the top.
    QImage render(const GpuFrame& frame);
    /// Same without the readback, for timing the GPU work (waits for it to finish).
    void renderAndWait(const GpuFrame& frame);

private:
    GpuOffscreen() = default;
    bool ensureTarget(const QSize& size);

    std::unique_ptr<QRhi> rhi_;
    GpuWaveRenderer renderer_;
    QRhiTexture* texture_ = nullptr;
    QRhiTextureRenderTarget* target_ = nullptr;
    QRhiRenderPassDescriptor* pass_ = nullptr;
    QSize size_;
};

}  // namespace lvd::gui
