// SPDX-License-Identifier: MIT
#include "gpuview.h"

#include "terminalsession.h"

#include <QColor>
#include <QFile>
#include <QMatrix4x4>

#include <rhi/qshader.h>

namespace
{
constexpr int kFloatsPerInstance = 16; // rect, uv, fg, bg
constexpr int kBytesPerInstance = kFloatsPerInstance * int(sizeof(float));
constexpr int kInitialInstanceCapacity = 8192;
constexpr int kAtlasSize = 2048;

// Triangle-strip unit quad
constexpr float kQuad[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};

QShader loadShader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("kgraphical-term: cannot open shader %s", qPrintable(path));
        return QShader();
    }
    return QShader::fromSerialized(file.readAll());
}
} // namespace

GpuView::GpuView(TerminalSession *session, QRhiWidget::Api api, QWidget *parent)
    : QRhiWidget(parent)
    , m_session(session)
    , m_atlas(kAtlasSize)
{
    setApi(api);
}

GpuView::~GpuView() = default;

QSize GpuView::sizeHint() const
{
    return m_session->idealSize();
}

void GpuView::releaseResources()
{
    m_pipeline.reset();
    m_bindings.reset();
    m_sampler.reset();
    m_atlasTexture.reset();
    m_uniformBuffer.reset();
    m_instanceBuffer.reset();
    m_quadBuffer.reset();
    m_rhi = nullptr;

    // The atlas texture is gone, so everything cached for it must be re-uploaded.
    m_metricsGeneration = -1;
    m_atlas.clear();
}

void GpuView::initialize(QRhiCommandBuffer *cb)
{
    if (m_rhi != rhi()) {
        releaseResources();
        m_rhi = rhi();
    }
    if (m_pipeline) {
        return;
    }

    qInfo("kgraphical-term: GPU renderer using %s", m_rhi->backendName());

    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();

    m_quadBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
    m_quadBuffer->create();
    updates->uploadStaticBuffer(m_quadBuffer.get(), kQuad);

    m_instanceBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                            QRhiBuffer::VertexBuffer,
                                            kInitialInstanceCapacity * kBytesPerInstance));
    m_instanceBuffer->create();

    m_uniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    m_uniformBuffer->create();

    m_atlasTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(kAtlasSize, kAtlasSize)));
    m_atlasTexture->create();

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest,
                                      QRhiSampler::Nearest,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge));
    m_sampler->create();

    m_bindings.reset(m_rhi->newShaderResourceBindings());
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniformBuffer.get()),
        QRhiShaderResourceBinding::sampledTexture(1,
                                                  QRhiShaderResourceBinding::FragmentStage,
                                                  m_atlasTexture.get(),
                                                  m_sampler.get()),
    });
    m_bindings->create();

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/cell.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/cell.frag.qsb"))},
    });

    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(2 * sizeof(float)),
        QRhiVertexInputBinding(kBytesPerInstance, QRhiVertexInputBinding::PerInstance),
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float4, 0 * 4 * sizeof(float)),
        QRhiVertexInputAttribute(1, 2, QRhiVertexInputAttribute::Float4, 1 * 4 * sizeof(float)),
        QRhiVertexInputAttribute(1, 3, QRhiVertexInputAttribute::Float4, 2 * 4 * sizeof(float)),
        QRhiVertexInputAttribute(1, 4, QRhiVertexInputAttribute::Float4, 3 * 4 * sizeof(float)),
    });
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_bindings.get());
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->setSampleCount(renderTarget()->sampleCount());
    m_pipeline->create();

    cb->resourceUpdate(updates);
}

void GpuView::pushInstance(float x, float y, float w, float h, const GlyphSlot *glyph, const QColor &fg, const QColor &bg)
{
    const float u0 = glyph ? glyph->u0 : 0.0f;
    const float v0 = glyph ? glyph->v0 : 0.0f;
    const float u1 = glyph ? glyph->u1 : 0.0f;
    const float v1 = glyph ? glyph->v1 : 0.0f;
    // fg.a = 1 tints a monochrome glyph, 0 keeps a colour glyph's own colours.
    const float fgAlpha = (glyph && glyph->colored) ? 0.0f : 1.0f;

    m_instances.insert(m_instances.end(),
                       {x, y, w, h,
                        u0, v0, u1, v1,
                        float(fg.redF()), float(fg.greenF()), float(fg.blueF()), fgAlpha,
                        float(bg.redF()), float(bg.greenF()), float(bg.blueF()), 1.0f});
}

void GpuView::pushRect(float x, float y, float w, float h, const QColor &color)
{
    pushInstance(x, y, w, h, nullptr, color, color);
}

bool GpuView::buildInstances()
{
    const TerminalSession *s = m_session;
    m_instances.clear();

    const int rows = s->rows();
    const int cols = s->cols();
    const int cw = s->cellWidth();
    const int ch = s->cellHeight();
    const int ascent = s->ascent();
    const int first = s->firstVisibleLine();
    const bool focus = hasFocus();
    const bool cursorShown = s->cursorVisible() && s->scrollOffset() == 0;
    const QColor defaultBg = s->defaultBackground();
    const QColor defaultFg = s->defaultForeground();
    const QColor highlight = palette().color(QPalette::Highlight);
    const QColor highlightedText = palette().color(QPalette::HighlightedText);
    const float lineThickness = float(qMax(1, ch / 16));

    m_instances.reserve(size_t(rows) * size_t(cols) * kFloatsPerInstance / 2);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            VTermScreenCell cell;
            if (!s->cellAt(first + r, c, cell)) {
                continue;
            }
            if (cell.chars[0] == static_cast<uint32_t>(-1)) {
                continue; // right half of a wide character
            }

            const int span = qMax(1, int(cell.width));
            QColor fg = s->color(cell.fg);
            QColor bg = s->color(cell.bg);
            if (cell.attrs.reverse) {
                std::swap(fg, bg);
            }

            const bool isCursor = cursorShown && r == s->cursorRow() && c == s->cursorCol();
            if (s->isSelected(first + r, c)) {
                bg = highlight;
                fg = highlightedText;
            } else if (isCursor && focus) {
                std::swap(fg, bg);
            }

            GlyphSlot slot;
            bool hasGlyph = false;
            if (cell.chars[0] != 0 && cell.chars[0] != U' ' && !cell.attrs.conceal) {
                const int style = (cell.attrs.bold ? 1 : 0) | (cell.attrs.italic ? 2 : 0);
                if (!m_atlas.glyph(TerminalSession::cellText(cell), style, span, &slot)) {
                    return false; // atlas full; caller clears it and retries
                }
                hasGlyph = true;
            }

            const float x = float(c * cw);
            const float y = float(r * ch);
            const float w = float(span * cw);

            // The render target is cleared to the default background, so cells
            // that are plain background and empty need no instance at all.
            if (hasGlyph || bg != defaultBg || isCursor) {
                pushInstance(x, y, w, float(ch), hasGlyph ? &slot : nullptr, fg, bg);
            }
            if (cell.attrs.underline) {
                pushRect(x, y + float(ascent + 1), w, lineThickness, fg);
            }
            if (cell.attrs.strike) {
                pushRect(x, y + float(ch / 2), w, lineThickness, fg);
            }
        }
    }

    // Hollow cursor when the terminal is not focused
    if (cursorShown && !focus) {
        const float x = float(s->cursorCol() * cw);
        const float y = float(s->cursorRow() * ch);
        pushRect(x, y, float(cw), 1.0f, defaultFg);
        pushRect(x, y + float(ch - 1), float(cw), 1.0f, defaultFg);
        pushRect(x, y, 1.0f, float(ch), defaultFg);
        pushRect(x + float(cw - 1), y, 1.0f, float(ch), defaultFg);
    }
    return true;
}

void GpuView::render(QRhiCommandBuffer *cb)
{
    // Font or pixel ratio changed: glyphs must be re-rasterised.
    if (m_metricsGeneration != m_session->metricsGeneration()) {
        m_atlas.configure(m_session->glyphFonts(), m_session->cellWidth(), m_session->cellHeight(), m_session->ascent());
        m_metricsGeneration = m_session->metricsGeneration();
    }

    if (!buildInstances()) {
        // Atlas overflowed mid-frame: start over with an empty one.
        m_atlas.clear();
        buildInstances();
    }

    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();

    for (const GlyphAtlas::Upload &upload : m_atlas.takeUploads()) {
        QRhiTextureSubresourceUploadDescription sub(upload.image);
        sub.setDestinationTopLeft(upload.pos);
        updates->uploadTexture(m_atlasTexture.get(), QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, sub)}));
    }

    const QSize outputSize = renderTarget()->pixelSize();
    QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
    mvp.ortho(0.0f, float(outputSize.width()), float(outputSize.height()), 0.0f, -1.0f, 1.0f);
    updates->updateDynamicBuffer(m_uniformBuffer.get(), 0, 64, mvp.constData());

    const int instanceCount = int(m_instances.size() / kFloatsPerInstance);
    const int bytes = instanceCount * kBytesPerInstance;
    if (bytes > m_instanceBuffer->size()) {
        m_instanceBuffer->setSize(qMax(bytes, m_instanceBuffer->size() * 2));
        m_instanceBuffer->create();
    }
    if (instanceCount > 0) {
        updates->updateDynamicBuffer(m_instanceBuffer.get(), 0, bytes, m_instances.data());
    }

    cb->beginPass(renderTarget(), m_session->defaultBackground(), {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport(QRhiViewport(0, 0, float(outputSize.width()), float(outputSize.height())));
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput inputs[] = {
        {m_quadBuffer.get(), 0},
        {m_instanceBuffer.get(), 0},
    };
    cb->setVertexInput(0, 2, inputs);
    if (instanceCount > 0) {
        cb->draw(4, quint32(instanceCount));
    }
    cb->endPass();
}
