// SPDX-License-Identifier: MIT
#include "glyphatlas.h"

#include <QPainter>

GlyphAtlas::GlyphAtlas(int size)
    : m_size(size)
{
}

void GlyphAtlas::configure(const QFont *fonts, int cellWidth, int cellHeight, int ascent)
{
    for (int i = 0; i < 4; ++i) {
        m_fonts[i] = fonts[i];
    }
    m_cellWidth = qMax(1, cellWidth);
    m_cellHeight = qMax(1, cellHeight);
    m_ascent = ascent;
    clear();
}

void GlyphAtlas::clear()
{
    m_cache.clear();
    m_uploads.clear();
    m_x = 0;
    m_y = 0;
}

bool GlyphAtlas::glyph(const QString &text, int style, int span, GlyphSlot *slot)
{
    span = qBound(1, span, 2);
    style = qBound(0, style, 3);

    const QString key = QString::number(style * 4 + span) + QLatin1Char(':') + text;
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) {
        *slot = it.value();
        return true;
    }

    const int width = m_cellWidth * span;
    if (m_x + width > m_size) {
        m_x = 0;
        m_y += m_cellHeight;
    }
    if (m_y + m_cellHeight > m_size) {
        return false;
    }

    QImage image(width, m_cellHeight, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(m_fonts[style]);
        painter.setPen(Qt::white);
        painter.drawText(QPoint(0, m_ascent), text);
    }

    const char32_t first = text.isEmpty() ? U'\0' : text.toUcs4().value(0);

    GlyphSlot result;
    const float inv = 1.0f / float(m_size);
    result.u0 = float(m_x) * inv;
    result.v0 = float(m_y) * inv;
    result.u1 = float(m_x + width) * inv;
    result.v1 = float(m_y + m_cellHeight) * inv;
    result.colored = first >= 0x1F300 && first <= 0x1FAFF;

    m_uploads.push_back({QPoint(m_x, m_y), image});
    m_x += width;

    m_cache.insert(key, result);
    *slot = result;
    return true;
}

std::vector<GlyphAtlas::Upload> GlyphAtlas::takeUploads()
{
    std::vector<Upload> out;
    out.swap(m_uploads);
    return out;
}
