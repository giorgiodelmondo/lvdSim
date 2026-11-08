// SPDX-License-Identifier: GPL-3.0-or-later
#include "rhi/gpu_wave_renderer.hpp"

#include <QFile>
#include <QMatrix4x4>
#include <QOffscreenSurface>

#include <rhi/qrhi_platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lvd::gui {

namespace {

struct FillVertex {
    float x, y;
    float r, g, b, a;  // premultiplied
};
struct QuadVertex {
    float x, y, u, v;
};
struct UniformData {
    float clip[16];
    float size[2];
    float pad[2];
};
static_assert(sizeof(UniformData) == 80);

QShader loadShader(const QString& name) {
    QFile f(QStringLiteral(":/lvdsim/shaders/") + name + ".qsb");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(f.readAll());
}

void premultiply(Rgba c, float out[4]) {
    const float a = c.a / 255.0f;
    out[0] = c.r / 255.0f * a;
    out[1] = c.g / 255.0f * a;
    out[2] = c.b / 255.0f * a;
    out[3] = a;
}

void pushTriangle(std::vector<FillVertex>& v, float x0, float y0, float x1, float y1, float x2, float y2, const float c[4]) {
    v.push_back({x0, y0, c[0], c[1], c[2], c[3]});
    v.push_back({x1, y1, c[0], c[1], c[2], c[3]});
    v.push_back({x2, y2, c[0], c[1], c[2], c[3]});
}

void pushRect(std::vector<FillVertex>& v, float x0, float y0, float x1, float y1, const float c[4]) {
    pushTriangle(v, x0, y0, x1, y0, x0, y1, c);
    pushTriangle(v, x1, y0, x1, y1, x0, y1, c);
}

// Appends the triangles of a tile's picture, moved to (ox, oy).
void appendGeometry(std::vector<FillVertex>& out, const TileGeometry& g, float ox, float oy) {
    for (const Prim& p : g.prims) {
        float c[4];
        premultiply(p.color, c);
        switch (p.kind) {
            case PrimKind::Rect:
                pushRect(out, p.x0 + ox, p.y0 + oy, p.x1 + ox, p.y1 + oy, c);
                break;
            case PrimKind::Poly:
                for (std::uint32_t i = 1; i + 1 < p.count; ++i) {
                    const float* a = &g.points[p.first];
                    const float* b = &g.points[p.first + 2 * i];
                    const float* d = &g.points[p.first + 2 * (i + 1)];
                    pushTriangle(out, a[0] + ox, a[1] + oy, b[0] + ox, b[1] + oy, d[0] + ox, d[1] + oy, c);
                }
                break;
        }
    }
}

QRhiGraphicsPipeline* makePipeline(QRhi* rhi, QRhiShaderResourceBindings* srb, QRhiRenderPassDescriptor* rpd, const QShader& vs,
                                   const QShader& fs, bool quad, bool blend) {
    QRhiGraphicsPipeline* ps = rhi->newGraphicsPipeline();
    ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});

    QRhiVertexInputLayout layout;
    layout.setBindings({quad ? QRhiVertexInputBinding(sizeof(QuadVertex)) : QRhiVertexInputBinding(sizeof(FillVertex))});
    if (quad) {
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
    } else {
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)}});
    }
    ps->setVertexInputLayout(layout);
    ps->setTopology(QRhiGraphicsPipeline::Triangles);
    ps->setCullMode(QRhiGraphicsPipeline::None);
    ps->setFlags(QRhiGraphicsPipeline::UsesScissor);
    if (blend) {
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::One;
        b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::One;
        b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        ps->setTargetBlends({b});
    }
    ps->setShaderResourceBindings(srb);
    ps->setRenderPassDescriptor(rpd);
    if (!ps->create()) {
        delete ps;
        return nullptr;
    }
    return ps;
}

}  // namespace

// ---------------------------------------------------------------------------------------------

struct GpuWaveRenderer::Pipelines {
    QShader fillVs, fillFs, quadVs, quadFs;
    QRhiShaderResourceBindings* mainFillSrb = nullptr;
    QRhiGraphicsPipeline* fillMain = nullptr;
    QRhiGraphicsPipeline* quadMain = nullptr;
    QRhiGraphicsPipeline* fillAtlas = nullptr;
    QRhiGraphicsPipeline* replaceAtlas = nullptr;
    bool atlasReady = false;

    ~Pipelines() {
        delete quadMain;
        delete fillMain;
        delete replaceAtlas;
        delete fillAtlas;
        delete mainFillSrb;
    }
};

struct GpuWaveRenderer::Atlas {
    int slotHeight = 0, cols = 0, rows = 0;
    QRhiTexture* tex = nullptr;
    QRhiTextureRenderTarget* rt = nullptr;
    QRhiRenderPassDescriptor* rpd = nullptr;
    QRhiTextureRenderTarget* clearRt = nullptr;
    QRhiRenderPassDescriptor* clearRpd = nullptr;
    QRhiBuffer* ubo = nullptr;
    QRhiShaderResourceBindings* fillSrb = nullptr;
    QRhiShaderResourceBindings* quadSrb = nullptr;
    bool cleared = false;
    bool uboWritten = false;
    std::vector<int> freeSlots;

    ~Atlas() {
        delete quadSrb;
        delete fillSrb;
        delete ubo;
        delete clearRt;
        delete clearRpd;
        delete rt;
        delete rpd;
        delete tex;
    }
};

GpuWaveRenderer::GpuWaveRenderer() = default;
GpuWaveRenderer::~GpuWaveRenderer() { releaseResources(); }

std::size_t GpuWaveRenderer::atlasCount() const noexcept {
    std::size_t n = 0;
    for (const auto& [h, v] : atlases_) n += v.size();
    return n;
}

bool GpuWaveRenderer::initialize(QRhi* rhi, QRhiRenderPassDescriptor* targetPass, QRhiTexture::Format colorFormat) {
    // QRhiWidget initialises again when its colour texture is recreated (every resize). Nothing to redo when the
    // device, pass layout and format are unchanged, and the tiles on the GPU stay valid.
    if (pipelines_ && rhi_ == rhi && format_ == colorFormat && targetPass_ == targetPass) return true;
    releaseResources();
    rhi_ = rhi;
    targetPass_ = targetPass;
    format_ = colorFormat;
    pipelines_ = std::make_unique<Pipelines>();

    pipelines_->fillVs = loadShader("fill.vert");
    pipelines_->fillFs = loadShader("fill.frag");
    pipelines_->quadVs = loadShader("quad.vert");
    pipelines_->quadFs = loadShader("quad.frag");
    if (!pipelines_->fillVs.isValid() || !pipelines_->fillFs.isValid() || !pipelines_->quadVs.isValid() ||
        !pipelines_->quadFs.isValid()) {
        releaseResources();
        return false;
    }

    mainUbo_ = rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UniformData));
    sampler_ = rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                QRhiSampler::ClampToEdge);
    if (!mainUbo_->create() || !sampler_->create()) {
        releaseResources();
        return false;
    }

    pipelines_->mainFillSrb = rhi_->newShaderResourceBindings();
    pipelines_->mainFillSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, mainUbo_)});
    pipelines_->mainFillSrb->create();
    pipelines_->fillMain = makePipeline(rhi_, pipelines_->mainFillSrb, targetPass_, pipelines_->fillVs, pipelines_->fillFs, false, true);
    if (pipelines_->fillMain == nullptr) {
        releaseResources();
        return false;
    }
    return true;
}

void GpuWaveRenderer::releaseResources() {
    atlases_.clear();
    slots_.clear();
    lru_.clear();
    pipelines_.reset();
    delete fillVbuf_;
    delete quadVbuf_;
    delete mainUbo_;
    delete sampler_;
    fillVbuf_ = quadVbuf_ = nullptr;
    mainUbo_ = nullptr;
    sampler_ = nullptr;
    fillCapacity_ = quadCapacity_ = 0;
    rhi_ = nullptr;
    targetPass_ = nullptr;
}

void GpuWaveRenderer::clearCache() {
    // Slots go back to the free lists; the atlas contents are cleared again when a slot is reused.
    for (auto& [h, list] : atlases_) {
        for (auto& a : list) {
            a->freeSlots.clear();
            for (int i = a->cols * a->rows - 1; i >= 0; --i) a->freeSlots.push_back(i);
        }
    }
    slots_.clear();
    lru_.clear();
}

GpuWaveRenderer::Atlas* GpuWaveRenderer::createAtlas(int slotHeight) {
    auto a = std::make_unique<Atlas>();
    a->slotHeight = slotHeight;
    a->cols = kAtlasWidth / kTileWidth;
    a->rows = std::max(1, kAtlasHeight / slotHeight);

    a->tex = rhi_->newTexture(format_, QSize(kAtlasWidth, a->rows * slotHeight), 1, QRhiTexture::RenderTarget);
    if (!a->tex->create()) return nullptr;

    QRhiTextureRenderTargetDescription desc{QRhiColorAttachment(a->tex)};
    a->rt = rhi_->newTextureRenderTarget(desc, QRhiTextureRenderTarget::PreserveColorContents);
    a->rpd = a->rt->newCompatibleRenderPassDescriptor();
    a->rt->setRenderPassDescriptor(a->rpd);
    a->clearRt = rhi_->newTextureRenderTarget(desc);
    a->clearRpd = a->clearRt->newCompatibleRenderPassDescriptor();
    a->clearRt->setRenderPassDescriptor(a->clearRpd);
    if (!a->rt->create() || !a->clearRt->create()) return nullptr;

    a->ubo = rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UniformData));
    if (!a->ubo->create()) return nullptr;

    a->fillSrb = rhi_->newShaderResourceBindings();
    a->fillSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, a->ubo)});
    a->fillSrb->create();

    // Tiles are drawn with the frame's uniforms (target size) and sample this atlas.
    a->quadSrb = rhi_->newShaderResourceBindings();
    a->quadSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, mainUbo_),
                             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, a->tex,
                                                                       sampler_)});
    a->quadSrb->create();

    for (int i = a->cols * a->rows - 1; i >= 0; --i) a->freeSlots.push_back(i);

    // The pipelines that render into atlases and the one that draws tiles need a layout and a pass to be
    // created against; the first atlas provides both.
    if (!pipelines_->atlasReady) {
        pipelines_->fillAtlas = makePipeline(rhi_, a->fillSrb, a->rpd, pipelines_->fillVs, pipelines_->fillFs, false, true);
        pipelines_->replaceAtlas = makePipeline(rhi_, a->fillSrb, a->rpd, pipelines_->fillVs, pipelines_->fillFs, false, false);
        pipelines_->quadMain = makePipeline(rhi_, a->quadSrb, targetPass_, pipelines_->quadVs, pipelines_->quadFs, true, true);
        if (pipelines_->fillAtlas == nullptr || pipelines_->replaceAtlas == nullptr || pipelines_->quadMain == nullptr) return nullptr;
        pipelines_->atlasReady = true;
    }

    Atlas* raw = a.get();
    atlases_[slotHeight].push_back(std::move(a));
    return raw;
}

GpuWaveRenderer::Slot* GpuWaveRenderer::acquire(const TileKey& key, int heightPx) {
    constexpr std::size_t kMaxAtlasesPerHeight = 4;
    auto& list = atlases_[heightPx];

    Atlas* atlas = nullptr;
    for (auto& a : list)
        if (!a->freeSlots.empty()) {
            atlas = a.get();
            break;
        }
    if (atlas == nullptr && list.size() < kMaxAtlasesPerHeight) atlas = createAtlas(heightPx);

    int index = -1;
    if (atlas != nullptr) {
        index = atlas->freeSlots.back();
        atlas->freeSlots.pop_back();
    } else {
        // Evict the least recently used tile of this height that this frame does not need.
        for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
            auto s = slots_.find(*it);
            if (s == slots_.end() || s->second.atlas->slotHeight != heightPx || s->second.lastFrame == frameNumber_) continue;
            atlas = s->second.atlas;
            index = s->second.index;
            lru_.erase(std::next(it).base());
            slots_.erase(s);
            break;
        }
        if (atlas == nullptr) return nullptr;
    }

    lru_.push_front(key);
    Slot slot;
    slot.atlas = atlas;
    slot.index = index;
    slot.lastFrame = frameNumber_;
    slot.lru = lru_.begin();
    return &(slots_[key] = slot);
}

void GpuWaveRenderer::ensureBuffer(QRhiBuffer*& buf, int& capacity, int bytes) {
    if (buf != nullptr && capacity >= bytes) return;
    int cap = std::max(bytes, 64 * 1024);
    if (buf != nullptr) cap = std::max(cap, capacity * 2);
    delete buf;
    buf = rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, cap);
    buf->create();
    capacity = cap;
}

void GpuWaveRenderer::render(QRhiCommandBuffer* cb, QRhiRenderTarget* target, const GpuFrame& frame) {
    ++frameNumber_;
    stats_ = {};
    const float W = static_cast<float>(frame.size.width()), H = static_cast<float>(frame.size.height());
    const QMatrix4x4 clip = rhi_->clipSpaceCorrMatrix();

    struct Placed {
        Slot* slot;
        GpuTileDraw draw;
    };
    std::vector<Placed> placed;
    std::vector<std::pair<Slot*, TileGeometry>> jobs;

    // Tiles already on the GPU are marked as used first so that making room for new ones cannot evict them.
    for (const GpuTileDraw& t : frame.tiles) {
        auto it = slots_.find(t.key);
        if (it == slots_.end()) continue;
        it->second.lastFrame = frameNumber_;
        lru_.splice(lru_.begin(), lru_, it->second.lru);
    }
    for (const GpuTileDraw& t : frame.tiles) {
        auto it = slots_.find(t.key);
        if (it != slots_.end()) {
            ++stats_.tilesReused;
            placed.push_back({&it->second, t});
            continue;
        }
        Slot* s = acquire(t.key, t.heightPx);
        if (s == nullptr) {
            ++stats_.tilesSkipped;
            continue;
        }
        jobs.emplace_back(s, frame.geometryFor(t));
        placed.push_back({s, t});
        ++stats_.tilesRasterised;
    }

    // ---- vertex data for the whole frame ---------------------------------------------------------
    std::vector<FillVertex> fillVerts;
    struct JobRange {
        Slot* slot;
        int clearFirst, geoFirst, geoCount;
    };
    std::vector<JobRange> ranges;
    const float clear[4] = {0, 0, 0, 0};
    for (auto& [slot, geometry] : jobs) {
        const float ox = static_cast<float>((slot->index % slot->atlas->cols) * kTileWidth);
        const float oy = static_cast<float>((slot->index / slot->atlas->cols) * slot->atlas->slotHeight);
        JobRange r{slot, static_cast<int>(fillVerts.size()), 0, 0};
        pushRect(fillVerts, ox, oy, ox + kTileWidth, oy + static_cast<float>(slot->atlas->slotHeight), clear);
        r.geoFirst = static_cast<int>(fillVerts.size());
        appendGeometry(fillVerts, geometry, ox, oy);
        r.geoCount = static_cast<int>(fillVerts.size()) - r.geoFirst;
        ranges.push_back(r);
    }

    const int rectFirst = static_cast<int>(fillVerts.size());
    for (const GpuRect& rc : frame.rects) {
        float c[4];
        premultiply(rc.color, c);
        pushRect(fillVerts, rc.x, rc.y, rc.x + rc.w, rc.y + rc.h, c);
    }
    const int rectCount = static_cast<int>(fillVerts.size()) - rectFirst;

    // Quads grouped by atlas so that each atlas is bound once.
    const bool yUp = rhi_->isYUpInFramebuffer();
    std::map<Atlas*, std::vector<QuadVertex>> quads;
    for (const Placed& p : placed) {
        const Atlas* a = p.slot->atlas;
        const float ax = static_cast<float>((p.slot->index % a->cols) * kTileWidth);
        const float ay = static_cast<float>((p.slot->index / a->cols) * a->slotHeight);
        const float aw = static_cast<float>(kAtlasWidth), ah = static_cast<float>(a->rows * a->slotHeight);
        const float u0 = ax / aw, u1 = (ax + kTileWidth) / aw;
        const float vTop = yUp ? 1.0f - ay / ah : ay / ah;
        const float vBottom = yUp ? 1.0f - (ay + a->slotHeight) / ah : (ay + a->slotHeight) / ah;
        const float x0 = p.draw.x, y0 = p.draw.y, x1 = p.draw.x + kTileWidth, y1 = p.draw.y + static_cast<float>(p.draw.heightPx);
        auto& q = quads[p.slot->atlas];
        q.push_back({x0, y0, u0, vTop});
        q.push_back({x1, y0, u1, vTop});
        q.push_back({x0, y1, u0, vBottom});
        q.push_back({x1, y0, u1, vTop});
        q.push_back({x1, y1, u1, vBottom});
        q.push_back({x0, y1, u0, vBottom});
        ++stats_.quads;
    }
    std::vector<QuadVertex> quadVerts;
    struct QuadRange {
        Atlas* atlas;
        int first, count;
    };
    std::vector<QuadRange> quadRanges;
    for (auto& [atlas, v] : quads) {
        quadRanges.push_back({atlas, static_cast<int>(quadVerts.size()), static_cast<int>(v.size())});
        quadVerts.insert(quadVerts.end(), v.begin(), v.end());
    }

    // ---- uploads -----------------------------------------------------------------------------------
    QRhiResourceUpdateBatch* updates = rhi_->nextResourceUpdateBatch();
    UniformData main{};
    std::memcpy(main.clip, clip.constData(), sizeof(main.clip));
    main.size[0] = W;
    main.size[1] = H;
    updates->updateDynamicBuffer(mainUbo_, 0, sizeof(main), &main);

    if (!fillVerts.empty()) {
        ensureBuffer(fillVbuf_, fillCapacity_, static_cast<int>(fillVerts.size() * sizeof(FillVertex)));
        updates->updateDynamicBuffer(fillVbuf_, 0, static_cast<quint32>(fillVerts.size() * sizeof(FillVertex)), fillVerts.data());
    }
    if (!quadVerts.empty()) {
        ensureBuffer(quadVbuf_, quadCapacity_, static_cast<int>(quadVerts.size() * sizeof(QuadVertex)));
        updates->updateDynamicBuffer(quadVbuf_, 0, static_cast<quint32>(quadVerts.size() * sizeof(QuadVertex)), quadVerts.data());
    }
    for (auto& [h, list] : atlases_) {
        for (auto& a : list) {
            if (a->uboWritten) continue;
            UniformData u{};
            std::memcpy(u.clip, clip.constData(), sizeof(u.clip));
            u.size[0] = static_cast<float>(kAtlasWidth);
            u.size[1] = static_cast<float>(a->rows * a->slotHeight);
            updates->updateDynamicBuffer(a->ubo, 0, sizeof(u), &u);
            a->uboWritten = true;
        }
    }

    // ---- atlas passes: rasterise the new tiles ---------------------------------------------------
    std::map<Atlas*, std::vector<const JobRange*>> byAtlas;
    for (const JobRange& r : ranges) byAtlas[r.slot->atlas].push_back(&r);

    for (auto& [atlas, list] : byAtlas) {
        if (!atlas->cleared) {
            cb->beginPass(atlas->clearRt, QColor(0, 0, 0, 0), {1.0f, 0}, updates);
            updates = nullptr;
            cb->endPass();
            atlas->cleared = true;
        }
        const float ah = static_cast<float>(atlas->rows * atlas->slotHeight);
        cb->beginPass(atlas->rt, QColor(0, 0, 0, 0), {1.0f, 0}, updates);
        updates = nullptr;
        cb->setViewport(QRhiViewport(0, 0, static_cast<float>(kAtlasWidth), ah));

        auto scissorFor = [&](const Slot* s) {
            const int x = (s->index % atlas->cols) * kTileWidth;
            const int y = (s->index / atlas->cols) * atlas->slotHeight;
            return QRhiScissor(x, static_cast<int>(ah) - y - atlas->slotHeight, kTileWidth, atlas->slotHeight);  // origin bottom-left
        };

        const QRhiCommandBuffer::VertexInput vi(fillVbuf_, 0);
        cb->setGraphicsPipeline(pipelines_->replaceAtlas);
        cb->setShaderResources(atlas->fillSrb);
        cb->setVertexInput(0, 1, &vi);
        for (const JobRange* r : list) {
            cb->setScissor(scissorFor(r->slot));
            cb->draw(6, 1, static_cast<quint32>(r->clearFirst), 0);
        }
        cb->setGraphicsPipeline(pipelines_->fillAtlas);
        cb->setShaderResources(atlas->fillSrb);
        cb->setVertexInput(0, 1, &vi);
        for (const JobRange* r : list) {
            if (r->geoCount == 0) continue;
            cb->setScissor(scissorFor(r->slot));
            cb->draw(static_cast<quint32>(r->geoCount), 1, static_cast<quint32>(r->geoFirst), 0);
        }
        cb->endPass();
    }

    // ---- main pass ---------------------------------------------------------------------------------
    cb->beginPass(target, frame.background, {1.0f, 0}, updates);
    // Frame coordinates are logical pixels. On a high-DPI window the target has more pixels than the frame:
    // the viewport covers all of the target, so the geometry is scaled up, as the QPainter path does.
    const QSize targetPx = target->pixelSize();
    const float sx = W > 0 ? static_cast<float>(targetPx.width()) / W : 1.0f;
    const float sy = H > 0 ? static_cast<float>(targetPx.height()) / H : 1.0f;
    cb->setViewport(QRhiViewport(0, 0, static_cast<float>(targetPx.width()), static_cast<float>(targetPx.height())));

    if (rectCount > 0) {
        cb->setGraphicsPipeline(pipelines_->fillMain);
        cb->setScissor(QRhiScissor(0, 0, targetPx.width(), targetPx.height()));
        cb->setShaderResources(pipelines_->mainFillSrb);
        const QRhiCommandBuffer::VertexInput vi(fillVbuf_, static_cast<quint32>(rectFirst * sizeof(FillVertex)));
        cb->setVertexInput(0, 1, &vi);
        cb->draw(static_cast<quint32>(rectCount));
    }

    if (!quadRanges.empty() && pipelines_->quadMain != nullptr) {
        cb->setGraphicsPipeline(pipelines_->quadMain);
        const QRect c = frame.tileClip.intersected(QRect(QPoint(0, 0), frame.size));
        const int cx = static_cast<int>(std::floor(c.x() * sx)), cw = static_cast<int>(std::ceil(c.width() * sx));
        const int cy = static_cast<int>(std::floor(c.y() * sy)), ch = static_cast<int>(std::ceil(c.height() * sy));
        cb->setScissor(QRhiScissor(cx, targetPx.height() - cy - ch, std::max(0, cw), std::max(0, ch)));
        for (const QuadRange& q : quadRanges) {
            cb->setShaderResources(q.atlas->quadSrb);
            const QRhiCommandBuffer::VertexInput vi(quadVbuf_, static_cast<quint32>(q.first * sizeof(QuadVertex)));
            cb->setVertexInput(0, 1, &vi);
            cb->draw(static_cast<quint32>(q.count));
        }
    }
    cb->endPass();
}

// ---------------------------------------------------------------------------------------------
// Offscreen GPU context

std::unique_ptr<GpuOffscreen> GpuOffscreen::create(QString* error) {
    auto setError = [&](const QString& e) {
        if (error != nullptr) *error = e;
    };

    std::unique_ptr<QRhi> rhi;
#if defined(Q_OS_WIN)
    QRhiD3D11InitParams params;
    rhi.reset(QRhi::create(QRhi::D3D11, &params));
    if (!rhi) {
        setError(QStringLiteral("Direct3D 11 is not available"));
        return nullptr;
    }
#elif defined(Q_OS_MACOS)
    QRhiMetalInitParams params;
    rhi.reset(QRhi::create(QRhi::Metal, &params));
    if (!rhi) {
        setError(QStringLiteral("Metal is not available"));
        return nullptr;
    }
#else
    setError(QStringLiteral("headless GPU rendering is only set up for Direct3D 11 and Metal"));
    return nullptr;
#endif

    std::unique_ptr<GpuOffscreen> g(new GpuOffscreen);
    g->rhi_ = std::move(rhi);
    return g;
}

GpuOffscreen::~GpuOffscreen() {
    renderer_.releaseResources();
    delete target_;
    delete pass_;
    delete texture_;
}

QString GpuOffscreen::backendName() const { return rhi_ ? QString::fromLatin1(rhi_->backendName()) : QString(); }

bool GpuOffscreen::ensureTarget(const QSize& size) {
    if (texture_ != nullptr && size == size_) return true;

    // Reuse the render pass descriptor: pipelines stay valid because targets with the same format are compatible.
    delete target_;
    delete texture_;
    target_ = nullptr;
    texture_ = rhi_->newTexture(QRhiTexture::BGRA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    if (!texture_->create()) return false;
    target_ = rhi_->newTextureRenderTarget(QRhiTextureRenderTargetDescription(QRhiColorAttachment(texture_)));
    if (pass_ == nullptr) pass_ = target_->newCompatibleRenderPassDescriptor();
    target_->setRenderPassDescriptor(pass_);
    if (!target_->create()) return false;
    size_ = size;

    if (!renderer_.isInitialized()) return renderer_.initialize(rhi_.get(), pass_, QRhiTexture::BGRA8);
    return true;
}

QImage GpuOffscreen::render(const GpuFrame& frame) {
    if (!ensureTarget(frame.size)) return {};

    QRhiCommandBuffer* cb = nullptr;
    if (rhi_->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return {};
    renderer_.render(cb, target_, frame);

    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
    u->readBackTexture(QRhiReadbackDescription(texture_), &result);
    cb->resourceUpdate(u);
    rhi_->endOffscreenFrame();

    if (result.data.isEmpty()) return {};
    QImage img(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
               QImage::Format_ARGB32_Premultiplied);
    img = img.copy();
    // QImage::flipped replaced mirrored in Qt 6.9, which deprecates mirrored; Debian ships Qt 6.8.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return rhi_->isYUpInFramebuffer() ? img.flipped(Qt::Vertical) : img;
#else
    return rhi_->isYUpInFramebuffer() ? img.mirrored(false, true) : img;
#endif
}

void GpuOffscreen::renderAndWait(const GpuFrame& frame) {
    if (!ensureTarget(frame.size)) return;
    QRhiCommandBuffer* cb = nullptr;
    if (rhi_->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return;
    renderer_.render(cb, target_, frame);
    rhi_->endOffscreenFrame();  // waits for the GPU
}

}  // namespace lvd::gui
