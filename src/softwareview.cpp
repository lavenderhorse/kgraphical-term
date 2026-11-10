// SPDX-License-Identifier: MIT
#include "softwareview.h"

#include "terminalsession.h"

#include <QPainter>
#include <QPalette>

#include <utility>

SoftwareView::SoftwareView(TerminalSession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QSize SoftwareView::sizeHint() const
{
    return m_session->idealSize();
}

void SoftwareView::paintEvent(QPaintEvent *)
{
    const TerminalSession *s = m_session;
    const QColor defaultBg = s->defaultBackground();
    const QColor defaultFg = s->defaultForeground();
    const QPalette pal = palette();

    QPainter p(this);
    p.fillRect(rect(), defaultBg);

    // Work in device pixels: the session's metrics are device pixels.
    const qreal dpr = devicePixelRatio();
    p.scale(1.0 / dpr, 1.0 / dpr);

    const int cw = s->cellWidth();
    const int ch = s->cellHeight();
    const int ascent = s->ascent();
    const int first = s->firstVisibleLine();
    const bool focus = hasFocus();
    const bool cursorShown = s->cursorVisible() && s->scrollOffset() == 0;
    const int lineThickness = qMax(1, ch / 16);

    for (int r = 0; r < s->rows(); ++r) {
        for (int c = 0; c < s->cols(); ++c) {
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
                bg = pal.color(QPalette::Highlight);
                fg = pal.color(QPalette::HighlightedText);
            } else if (isCursor && focus) {
                std::swap(fg, bg);
            }

            const QRect cellRect(c * cw, r * ch, span * cw, ch);
            if (bg != defaultBg || isCursor) {
                p.fillRect(cellRect, bg);
            }

            if (cell.chars[0] != 0 && cell.chars[0] != U' ' && !cell.attrs.conceal) {
                const int style = (cell.attrs.bold ? 1 : 0) | (cell.attrs.italic ? 2 : 0);
                p.setFont(s->glyphFonts()[style]);
                p.setPen(fg);
                p.drawText(QPoint(cellRect.x(), cellRect.y() + ascent), TerminalSession::cellText(cell));
            }
            if (cell.attrs.underline) {
                p.fillRect(QRect(cellRect.x(), cellRect.y() + ascent + 1, cellRect.width(), lineThickness), fg);
            }
            if (cell.attrs.strike) {
                p.fillRect(QRect(cellRect.x(), cellRect.y() + ch / 2, cellRect.width(), lineThickness), fg);
            }
        }
    }

    if (cursorShown && !focus) {
        p.setPen(defaultFg);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(s->cursorCol() * cw, s->cursorRow() * ch, cw - 1, ch - 1));
    }
}
