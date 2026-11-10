// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <deque>
#include <vector>

#include <vterm.h>

class KPtyProcess;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QWidget;

// One terminal: a child process on a pty, a libvterm screen, scrollback,
// selection and all keyboard/mouse handling. It knows nothing about how it is
// drawn: a view widget (software or GPU) is attached and renders its state.
//
// All pixel metrics exposed here are DEVICE pixels, so views can draw 1:1 on
// high-DPI screens.
class TerminalSession : public QObject
{
    Q_OBJECT
public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    void start(const QString &program, const QStringList &args, const QString &workDir);
    QString currentWorkingDirectory() const;

    // Installs an event filter on `view` and renders into it.
    void attachView(QWidget *view);
    QWidget *view() const
    {
        return m_view;
    }
    bool viewHasFocus() const;

    // Font and metrics
    void setFont(const QFont &font);
    QFont font() const
    {
        return m_font;
    }
    // regular, bold, italic, bold-italic (index = bold | italic << 1), sized in device pixels
    const QFont *glyphFonts() const
    {
        return m_fonts;
    }
    int cellWidth() const
    {
        return m_cellWidth;
    }
    int cellHeight() const
    {
        return m_cellHeight;
    }
    int ascent() const
    {
        return m_ascent;
    }
    // Bumps whenever the font or pixel ratio changes, so views can drop caches.
    int metricsGeneration() const
    {
        return m_metricsGeneration;
    }
    QSize idealSize() const; // logical pixels, 80x24 cells

    // Grid
    int rows() const
    {
        return m_rows;
    }
    int cols() const
    {
        return m_cols;
    }
    int scrollbackSize() const
    {
        return int(m_scrollback.size());
    }
    int scrollOffset() const
    {
        return m_scrollOffset;
    }
    // Absolute line shown in the first visible row (scrollback lines come first).
    int firstVisibleLine() const
    {
        return scrollbackSize() - m_scrollOffset;
    }
    bool cellAt(int line, int col, VTermScreenCell &cell) const;
    static QString cellText(const VTermScreenCell &cell);

    // Colours
    QColor color(const VTermColor &c) const;
    QColor defaultForeground() const;
    QColor defaultBackground() const;

    // Cursor and selection
    bool cursorVisible() const
    {
        return m_cursorVisible;
    }
    int cursorRow() const
    {
        return m_cursor.row;
    }
    int cursorCol() const
    {
        return m_cursor.col;
    }
    bool isSelected(int line, int col) const;
    bool hasSelection() const
    {
        return m_hasSelection;
    }

    void copySelection();
    void pasteClipboard();
    void scrollBy(int lines);

    // libvterm callbacks (public only so the callback table can name them)
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldPos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int onBell(void *user);
    static int onPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int onPopLine(int cols, VTermScreenCell *cells, void *user);
    static void onOutput(const char *s, size_t len, void *user);

Q_SIGNALS:
    void titleChanged(const QString &title);
    void finished(int exitCode);
    void zoomRequested(int steps);
    void damaged(); // something changed; the view should repaint

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Pos {
        int line = 0;
        int col = 0;
        bool operator<(const Pos &o) const
        {
            return line != o.line ? line < o.line : col < o.col;
        }
        bool operator==(const Pos &o) const
        {
            return line == o.line && col == o.col;
        }
    };

    void updateMetrics();
    void updateGrid(const QSize &logicalSize);
    void onPtyReadyRead();
    void writeToPty(const QByteArray &data);
    void pasteText(QString text);
    void clearSelection();
    QString selectedText() const;
    VTermScreenCell blankCell() const;
    Pos cellPos(const QPointF &logicalPos) const;
    bool forwardingMouse(Qt::KeyboardModifiers mods) const;

    bool handleShortcutOverride(QKeyEvent *e);
    void keyPress(QKeyEvent *e);
    void mousePress(QMouseEvent *e);
    void mouseMove(QMouseEvent *e);
    void mouseRelease(QMouseEvent *e);
    void wheel(QWheelEvent *e);
    void showContextMenu(const QPoint &globalPos);

    QWidget *m_view = nullptr;
    KPtyProcess *m_proc = nullptr;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    VTermState *m_state = nullptr;
    VTermColor m_defaultFg{};
    VTermColor m_defaultBg{};

    QFont m_font;
    QFont m_fonts[4];
    qreal m_dpr = 1.0;
    int m_cellWidth = 8;
    int m_cellHeight = 16;
    int m_ascent = 12;
    int m_metricsGeneration = 0;

    QSize m_logicalSize;
    int m_rows = 24;
    int m_cols = 80;

    std::deque<std::vector<VTermScreenCell>> m_scrollback;
    int m_scrollOffset = 0;

    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true;
    bool m_altScreen = false;
    int m_mouseMode = 0;
    QByteArray m_titleBuffer;

    bool m_hasSelection = false;
    bool m_dragging = false;
    Pos m_selStart;
    Pos m_selEnd;
    int m_wheelAccum = 0;
};
