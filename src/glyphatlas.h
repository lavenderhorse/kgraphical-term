// SPDX-License-Identifier: MIT
#pragma once

#include <QFont>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QString>

#include <vector>

// Normalised texture coordinates of one rasterised glyph in the atlas.
struct GlyphSlot {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool colored = false; // emoji and other colour glyphs keep their own colours
};

// CPU-side glyph cache. Glyphs are rasterised with QPainter into cell-sized
// slots (shelf packing) and queued for upload to a GPU texture.
class GlyphAtlas
{
public:
    struct Upload {
        QPoint pos;
        QImage image; // Format_RGBA8888_Premultiplied
    };

    explicit GlyphAtlas(int size = 2048);

    int size() const
    {
        return m_size;
    }

    // `fonts` is regular, bold, italic, bold-italic (index = bold | italic << 1).
    // Drops all cached glyphs.
    void configure(const QFont *fonts, int cellWidth, int cellHeight, int ascent);
    void clear();

    // Returns false when the atlas is full. The caller must then clear() and
    // rebuild whatever it was drawing, because earlier slots are invalidated.
    bool glyph(const QString &text, int style, int span, GlyphSlot *slot);

    std::vector<Upload> takeUploads();

private:
    int m_size;
    QFont m_fonts[4];
    int m_cellWidth = 1;
    int m_cellHeight = 1;
    int m_ascent = 1;
    int m_x = 0;
    int m_y = 0;
    QHash<QString, GlyphSlot> m_cache;
    std::vector<Upload> m_uploads;
};
