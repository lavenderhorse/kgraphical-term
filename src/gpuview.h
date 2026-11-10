// SPDX-License-Identifier: MIT
#pragma once

#include "glyphatlas.h"

#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <memory>
#include <vector>

class QColor;
class TerminalSession;

// GPU renderer. Uses QRhiWidget, so the same code and the same shaders run on
// OpenGL or Vulkan depending on the Api passed in.
//
// Each frame the visible cells are turned into a flat array of instances
// (position, glyph atlas rectangle, foreground, background) and drawn with a
// single instanced draw call. Glyphs are rasterised once into an atlas texture.
class GpuView : public QRhiWidget
{
    Q_OBJECT
public:
    GpuView(TerminalSession *session, QRhiWidget::Api api, QWidget *parent = nullptr);
    ~GpuView() override;

    QSize sizeHint() const override;

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

private:
    bool buildInstances();
    void pushInstance(float x, float y, float w, float h, const GlyphSlot *glyph, const QColor &fg, const QColor &bg);
    void pushRect(float x, float y, float w, float h, const QColor &color);

    TerminalSession *m_session;
    GlyphAtlas m_atlas;
    int m_metricsGeneration = -1;

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_quadBuffer;
    std::unique_ptr<QRhiBuffer> m_instanceBuffer;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiTexture> m_atlasTexture;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;

    std::vector<float> m_instances;
};
