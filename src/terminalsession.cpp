// SPDX-License-Identifier: MIT
#include "terminalsession.h"

#include <KLocalizedString>
#include <KPtyDevice>
#include <KPtyProcess>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>

#include <algorithm>
#include <cstring>

namespace
{
constexpr size_t kMaxScrollback = 10000;

// Breeze-inspired palette
constexpr QRgb kAnsi[16] = {
    0x232627, 0xed1515, 0x11d116, 0xf67400, 0x1d99f3, 0x9b59b6, 0x1abc9c, 0xfcfcfc,
    0x7f8c8d, 0xc0392b, 0x1cdc9a, 0xfdbc4b, 0x3daee9, 0x8e44ad, 0x16a085, 0xffffff,
};
constexpr QRgb kDefaultFg = 0xfcfcfc;
constexpr QRgb kDefaultBg = 0x1b1e20;

VTermColor makeColor(QRgb rgb)
{
    VTermColor c;
    vterm_color_rgb(&c, uint8_t(qRed(rgb)), uint8_t(qGreen(rgb)), uint8_t(qBlue(rgb)));
    return c;
}

VTermModifier vtMods(Qt::KeyboardModifiers m)
{
    int v = VTERM_MOD_NONE;
    if (m & Qt::ShiftModifier) {
        v |= VTERM_MOD_SHIFT;
    }
    if (m & Qt::AltModifier) {
        v |= VTERM_MOD_ALT;
    }
    if (m & Qt::ControlModifier) {
        v |= VTERM_MOD_CTRL;
    }
    return VTermModifier(v);
}

VTermKey mapKey(int key)
{
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VTERM_KEY_ENTER;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return VTERM_KEY_TAB;
    case Qt::Key_Backspace:
        return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape:
        return VTERM_KEY_ESCAPE;
    case Qt::Key_Up:
        return VTERM_KEY_UP;
    case Qt::Key_Down:
        return VTERM_KEY_DOWN;
    case Qt::Key_Left:
        return VTERM_KEY_LEFT;
    case Qt::Key_Right:
        return VTERM_KEY_RIGHT;
    case Qt::Key_Insert:
        return VTERM_KEY_INS;
    case Qt::Key_Delete:
        return VTERM_KEY_DEL;
    case Qt::Key_Home:
        return VTERM_KEY_HOME;
    case Qt::Key_End:
        return VTERM_KEY_END;
    case Qt::Key_PageUp:
        return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown:
        return VTERM_KEY_PAGEDOWN;
    default:
        break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return VTermKey(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    }
    return VTERM_KEY_NONE;
}

const VTermScreenCallbacks &screenCallbacks()
{
    static const VTermScreenCallbacks callbacks = [] {
        VTermScreenCallbacks c;
        std::memset(&c, 0, sizeof c);
        // moverect is left null: libvterm then reports the moved area as damage.
        c.damage = &TerminalSession::onDamage;
        c.movecursor = &TerminalSession::onMoveCursor;
        c.settermprop = &TerminalSession::onSetTermProp;
        c.bell = &TerminalSession::onBell;
        c.sb_pushline = &TerminalSession::onPushLine;
        c.sb_popline = &TerminalSession::onPopLine;
        return c;
    }();
    return callbacks;
}
} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent)
{
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    updateMetrics();

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, &TerminalSession::onOutput, this);

    m_state = vterm_obtain_state(m_vt);
    for (int i = 0; i < 16; ++i) {
        VTermColor c = makeColor(kAnsi[i]);
        vterm_state_set_palette_color(m_state, i, &c);
    }
    VTermColor fg = makeColor(kDefaultFg);
    VTermColor bg = makeColor(kDefaultBg);
    vterm_state_set_default_colors(m_state, &fg, &bg);

    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_set_callbacks(m_screen, &screenCallbacks(), this);
    vterm_screen_reset(m_screen, 1);
    vterm_state_get_default_colors(m_state, &m_defaultFg, &m_defaultBg);
}

TerminalSession::~TerminalSession()
{
    if (m_proc) {
        disconnect(m_proc, nullptr, this, nullptr);
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->terminate();
            m_proc->waitForFinished(200);
        }
    }
    vterm_free(m_vt);
}

void TerminalSession::start(const QString &program, const QStringList &args, const QString &workDir)
{
    m_proc = new KPtyProcess(this);
    m_proc->setPtyChannels(KPtyProcess::AllChannels);
    m_proc->pty()->setWinSize(m_rows, m_cols);
    m_proc->setEnv(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    m_proc->setEnv(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    m_proc->setEnv(QStringLiteral("TERM_PROGRAM"), QStringLiteral("kgraphical-term"));
    if (!workDir.isEmpty()) {
        m_proc->setWorkingDirectory(workDir);
    }
    m_proc->setProgram(program, args);

    connect(m_proc->pty(), &KPtyDevice::readyRead, this, &TerminalSession::onPtyReadyRead);
    connect(m_proc, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        Q_EMIT finished(exitCode);
    });
    m_proc->start();
}

QString TerminalSession::currentWorkingDirectory() const
{
    if (!m_proc || m_proc->processId() <= 0) {
        return QString();
    }
    return QFileInfo(QStringLiteral("/proc/%1/cwd").arg(m_proc->processId())).symLinkTarget();
}

void TerminalSession::attachView(QWidget *view)
{
    m_view = view;
    view->installEventFilter(this);
    view->setFocusPolicy(Qt::StrongFocus);
    view->setMouseTracking(true);
    view->setCursor(Qt::IBeamCursor);
    connect(this, &TerminalSession::damaged, view, [view] {
        view->update();
    });
    updateMetrics();
    updateGrid(view->size());
}

bool TerminalSession::viewHasFocus() const
{
    return m_view && m_view->hasFocus();
}

// ---------------------------------------------------------------------------
// Font and grid
// ---------------------------------------------------------------------------

void TerminalSession::setFont(const QFont &font)
{
    m_font = font;
    updateMetrics();
    updateGrid(m_logicalSize);
}

void TerminalSession::updateMetrics()
{
    m_dpr = m_view ? m_view->devicePixelRatio() : 1.0;
    const qreal dpi = m_view ? m_view->logicalDpiY() : 96.0;

    // Size fonts in device pixels so rasterisation never depends on a paint
    // device's DPI, and so cells are whole device pixels.
    qreal px = 14.0 * m_dpr;
    if (m_font.pointSizeF() > 0) {
        px = m_font.pointSizeF() * dpi / 72.0 * m_dpr;
    } else if (m_font.pixelSize() > 0) {
        px = m_font.pixelSize() * m_dpr;
    }

    QFont base = m_font;
    base.setPixelSize(qMax(1, qRound(px)));
    base.setKerning(false);
    base.setStyleStrategy(QFont::PreferAntialias);

    for (int i = 0; i < 4; ++i) {
        m_fonts[i] = base;
        m_fonts[i].setBold(i & 1);
        m_fonts[i].setItalic(i & 2);
    }

    const QFontMetricsF fm(base);
    m_cellWidth = qMax(1, qRound(fm.horizontalAdvance(QLatin1Char('M'))));
    m_cellHeight = qMax(1, qCeil(fm.height()));
    m_ascent = qRound(fm.ascent());
    ++m_metricsGeneration;
}

void TerminalSession::updateGrid(const QSize &logicalSize)
{
    m_logicalSize = logicalSize;
    const int deviceWidth = qRound(logicalSize.width() * m_dpr);
    const int deviceHeight = qRound(logicalSize.height() * m_dpr);
    const int cols = qMax(1, deviceWidth / m_cellWidth);
    const int rows = qMax(1, deviceHeight / m_cellHeight);

    if (cols != m_cols || rows != m_rows) {
        m_cols = cols;
        m_rows = rows;
        vterm_set_size(m_vt, rows, cols);
        vterm_screen_flush_damage(m_screen);
        if (m_proc) {
            m_proc->pty()->setWinSize(rows, cols);
        }
        clearSelection();
        m_scrollOffset = qMin(m_scrollOffset, scrollbackSize());
    }
    Q_EMIT damaged();
}

QSize TerminalSession::idealSize() const
{
    return QSize(qRound(80.0 * m_cellWidth / m_dpr), qRound(24.0 * m_cellHeight / m_dpr));
}

// ---------------------------------------------------------------------------
// Screen access for views
// ---------------------------------------------------------------------------

VTermScreenCell TerminalSession::blankCell() const
{
    VTermScreenCell cell;
    std::memset(&cell, 0, sizeof cell);
    cell.width = 1;
    cell.fg = m_defaultFg;
    cell.bg = m_defaultBg;
    return cell;
}

bool TerminalSession::cellAt(int line, int col, VTermScreenCell &cell) const
{
    if (line < 0 || col < 0 || col >= m_cols) {
        return false;
    }
    const int sb = scrollbackSize();
    if (line < sb) {
        const std::vector<VTermScreenCell> &row = m_scrollback[size_t(line)];
        cell = col < int(row.size()) ? row[size_t(col)] : blankCell();
        return true;
    }
    const int screenRow = line - sb;
    if (screenRow >= m_rows) {
        return false;
    }
    return vterm_screen_get_cell(m_screen, VTermPos{screenRow, col}, &cell) != 0;
}

QString TerminalSession::cellText(const VTermScreenCell &cell)
{
    QString text;
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
        const char32_t cp = cell.chars[i];
        text += QString::fromUcs4(&cp, 1);
    }
    return text;
}

QColor TerminalSession::color(const VTermColor &c) const
{
    VTermColor tmp = c;
    vterm_screen_convert_color_to_rgb(m_screen, &tmp);
    return QColor(tmp.rgb.red, tmp.rgb.green, tmp.rgb.blue);
}

QColor TerminalSession::defaultForeground() const
{
    return color(m_defaultFg);
}

QColor TerminalSession::defaultBackground() const
{
    return color(m_defaultBg);
}

// ---------------------------------------------------------------------------
// vterm callbacks
// ---------------------------------------------------------------------------

int TerminalSession::onDamage(VTermRect, void *user)
{
    Q_EMIT static_cast<TerminalSession *>(user)->damaged();
    return 1;
}

int TerminalSession::onMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    self->m_cursor = pos;
    self->m_cursorVisible = visible != 0;
    Q_EMIT self->damaged();
    return 1;
}

int TerminalSession::onSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    switch (prop) {
    case VTERM_PROP_TITLE:
        if (val->string.initial) {
            self->m_titleBuffer.clear();
        }
        self->m_titleBuffer.append(val->string.str, qsizetype(val->string.len));
        if (val->string.final) {
            Q_EMIT self->titleChanged(QString::fromUtf8(self->m_titleBuffer));
        }
        break;
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = val->boolean != 0;
        Q_EMIT self->damaged();
        break;
    case VTERM_PROP_ALTSCREEN:
        self->m_altScreen = val->boolean != 0;
        if (self->m_altScreen) {
            self->m_scrollOffset = 0;
        }
        self->clearSelection();
        Q_EMIT self->damaged();
        break;
    case VTERM_PROP_MOUSE:
        self->m_mouseMode = val->number;
        break;
    default:
        break;
    }
    return 1;
}

int TerminalSession::onBell(void *)
{
    QApplication::beep();
    return 1;
}

int TerminalSession::onPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    self->m_scrollback.emplace_back(cells, cells + cols);
    if (self->m_scrollback.size() > kMaxScrollback) {
        self->m_scrollback.pop_front();
    }
    if (self->m_scrollOffset > 0) {
        // Keep the user's view anchored while new output arrives.
        self->m_scrollOffset = qMin(self->m_scrollOffset + 1, self->scrollbackSize());
    }
    self->clearSelection();
    return 1;
}

int TerminalSession::onPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    if (self->m_scrollback.empty()) {
        return 0;
    }
    const std::vector<VTermScreenCell> &line = self->m_scrollback.back();
    for (int i = 0; i < cols; ++i) {
        cells[i] = i < int(line.size()) ? line[size_t(i)] : self->blankCell();
    }
    self->m_scrollback.pop_back();
    self->m_scrollOffset = qMin(self->m_scrollOffset, self->scrollbackSize());
    self->clearSelection();
    return 1;
}

void TerminalSession::onOutput(const char *s, size_t len, void *user)
{
    static_cast<TerminalSession *>(user)->writeToPty(QByteArray(s, qsizetype(len)));
}

// ---------------------------------------------------------------------------
// PTY
// ---------------------------------------------------------------------------

void TerminalSession::onPtyReadyRead()
{
    const QByteArray data = m_proc->pty()->readAll();
    if (data.isEmpty()) {
        return;
    }
    vterm_input_write(m_vt, data.constData(), size_t(data.size()));
    vterm_screen_flush_damage(m_screen);
    Q_EMIT damaged();
}

void TerminalSession::writeToPty(const QByteArray &data)
{
    if (m_proc && !data.isEmpty()) {
        m_proc->pty()->write(data);
    }
}

// ---------------------------------------------------------------------------
// Selection, clipboard, scrolling
// ---------------------------------------------------------------------------

void TerminalSession::clearSelection()
{
    if (m_hasSelection) {
        m_hasSelection = false;
        Q_EMIT damaged();
    }
}

bool TerminalSession::isSelected(int line, int col) const
{
    if (!m_hasSelection) {
        return false;
    }
    Pos a = m_selStart;
    Pos b = m_selEnd;
    if (b < a) {
        std::swap(a, b);
    }
    if (line < a.line || line > b.line) {
        return false;
    }
    if (line == a.line && col < a.col) {
        return false;
    }
    if (line == b.line && col > b.col) {
        return false;
    }
    return true;
}

QString TerminalSession::selectedText() const
{
    if (!m_hasSelection) {
        return QString();
    }
    Pos a = m_selStart;
    Pos b = m_selEnd;
    if (b < a) {
        std::swap(a, b);
    }

    QString out;
    for (int line = a.line; line <= b.line; ++line) {
        const int c0 = line == a.line ? a.col : 0;
        const int c1 = line == b.line ? b.col : m_cols - 1;
        QString row;
        for (int c = c0; c <= c1; ++c) {
            VTermScreenCell cell;
            if (!cellAt(line, c, cell)) {
                break;
            }
            if (cell.chars[0] == static_cast<uint32_t>(-1)) {
                continue; // right half of a wide character
            }
            row += cell.chars[0] == 0 ? QStringLiteral(" ") : cellText(cell);
        }
        if (line != b.line || c1 == m_cols - 1) {
            while (row.endsWith(QLatin1Char(' '))) {
                row.chop(1);
            }
        }
        out += row;
        if (line != b.line) {
            out += QLatin1Char('\n');
        }
    }
    return out;
}

void TerminalSession::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void TerminalSession::pasteClipboard()
{
    pasteText(QApplication::clipboard()->text());
}

void TerminalSession::pasteText(QString text)
{
    if (text.isEmpty()) {
        return;
    }
    text.remove(QChar(0x1b)); // never let pasted text inject escape sequences
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    m_scrollOffset = 0;
    vterm_keyboard_start_paste(m_vt);
    writeToPty(text.toUtf8());
    vterm_keyboard_end_paste(m_vt);
    Q_EMIT damaged();
}

void TerminalSession::scrollBy(int lines)
{
    const int target = qBound(0, m_scrollOffset + lines, scrollbackSize());
    if (target != m_scrollOffset) {
        m_scrollOffset = target;
        Q_EMIT damaged();
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool TerminalSession::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_view) {
        return false;
    }

    switch (event->type()) {
    case QEvent::Resize: {
        if (!qFuzzyCompare(m_view->devicePixelRatio(), m_dpr)) {
            updateMetrics();
        }
        updateGrid(static_cast<QResizeEvent *>(event)->size());
        return false;
    }
    case QEvent::FocusIn:
    case QEvent::FocusOut:
        Q_EMIT damaged();
        return false;
    case QEvent::ShortcutOverride:
        return handleShortcutOverride(static_cast<QKeyEvent *>(event));
    case QEvent::KeyPress:
        keyPress(static_cast<QKeyEvent *>(event));
        return true;
    case QEvent::MouseButtonPress:
        mousePress(static_cast<QMouseEvent *>(event));
        return true;
    case QEvent::MouseMove:
        mouseMove(static_cast<QMouseEvent *>(event));
        return true;
    case QEvent::MouseButtonRelease:
        mouseRelease(static_cast<QMouseEvent *>(event));
        return true;
    case QEvent::Wheel:
        wheel(static_cast<QWheelEvent *>(event));
        return true;
    case QEvent::ContextMenu: {
        auto *ce = static_cast<QContextMenuEvent *>(event);
        if (!forwardingMouse(ce->modifiers())) {
            showContextMenu(ce->globalPos());
        }
        return true;
    }
    default:
        return false;
    }
}

bool TerminalSession::handleShortcutOverride(QKeyEvent *e)
{
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    const int key = e->key();

    // Ctrl+Shift+<key>, F11 and Ctrl +/-/0 stay with the application's own
    // shortcuts (tabs, copy/paste, zoom, fullscreen); everything else goes to
    // the shell.
    const bool appShortcut = mods == (Qt::ControlModifier | Qt::ShiftModifier) || key == Qt::Key_F11
        || (mods == Qt::ControlModifier
            && (key == Qt::Key_Plus || key == Qt::Key_Minus || key == Qt::Key_Equal || key == Qt::Key_0));
    if (appShortcut) {
        return false;
    }
    e->accept();
    return true;
}

void TerminalSession::keyPress(QKeyEvent *e)
{
    const int key = e->key();
    const Qt::KeyboardModifiers qm = e->modifiers();
    VTermModifier mod = vtMods(qm);

    if (qm == Qt::ShiftModifier && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) {
        scrollBy((key == Qt::Key_PageUp ? 1 : -1) * qMax(1, m_rows / 2));
        return;
    }

    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return; // bare modifier keys produce no input
    default:
        break;
    }

    if (m_scrollOffset != 0) {
        m_scrollOffset = 0;
        Q_EMIT damaged();
    }
    clearSelection();

    const VTermKey vk = mapKey(key);
    if (vk != VTERM_KEY_NONE) {
        if (key == Qt::Key_Backtab) {
            mod = VTermModifier(mod | VTERM_MOD_SHIFT);
        }
        vterm_keyboard_key(m_vt, vk, mod);
        return;
    }

    const bool ctrl = qm & Qt::ControlModifier;
    const bool alt = qm & Qt::AltModifier;

    // Write classic control bytes ourselves (Ctrl+[ must be a plain ESC, for
    // example), with an ESC prefix for Alt.
    if (ctrl && key >= Qt::Key_A && key <= Qt::Key_Z) {
        QByteArray bytes;
        if (alt) {
            bytes += '\x1b';
        }
        bytes += char((key - Qt::Key_A) + 1);
        writeToPty(bytes);
        return;
    }
    if (ctrl && key == Qt::Key_Space) {
        writeToPty(QByteArray(1, '\0'));
        return;
    }

    const QString text = e->text();
    if (text.isEmpty()) {
        return;
    }
    const ushort first = text.at(0).unicode();
    if (first < 0x20 || first == 0x7f) {
        QByteArray bytes;
        if (alt) {
            bytes += '\x1b';
        }
        bytes += text.toUtf8();
        writeToPty(bytes);
        return;
    }
    for (const char32_t cp : text.toUcs4()) {
        vterm_keyboard_unichar(m_vt, cp, mod);
    }
}

TerminalSession::Pos TerminalSession::cellPos(const QPointF &logicalPos) const
{
    const int col = qBound(0, int(logicalPos.x() * m_dpr) / m_cellWidth, m_cols - 1);
    const int row = qBound(0, int(logicalPos.y() * m_dpr) / m_cellHeight, m_rows - 1);
    return Pos{firstVisibleLine() + row, col};
}

bool TerminalSession::forwardingMouse(Qt::KeyboardModifiers mods) const
{
    // Shift always lets the user select text, as in most terminals.
    return m_mouseMode != 0 && !(mods & Qt::ShiftModifier);
}

void TerminalSession::mousePress(QMouseEvent *e)
{
    m_view->setFocus();
    const Pos p = cellPos(e->position());

    if (forwardingMouse(e->modifiers())) {
        int button = 0;
        switch (e->button()) {
        case Qt::LeftButton:
            button = 1;
            break;
        case Qt::MiddleButton:
            button = 2;
            break;
        case Qt::RightButton:
            button = 3;
            break;
        default:
            return;
        }
        vterm_mouse_move(m_vt, p.line - firstVisibleLine(), p.col, vtMods(e->modifiers()));
        vterm_mouse_button(m_vt, button, true, vtMods(e->modifiers()));
        return;
    }

    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_selStart = m_selEnd = p;
        const bool hadSelection = m_hasSelection;
        m_hasSelection = false;
        if (hadSelection) {
            Q_EMIT damaged();
        }
    } else if (e->button() == Qt::MiddleButton) {
        if (QApplication::clipboard()->supportsSelection()) {
            pasteText(QApplication::clipboard()->text(QClipboard::Selection));
        }
    }
}

void TerminalSession::mouseMove(QMouseEvent *e)
{
    const Pos p = cellPos(e->position());

    if (forwardingMouse(e->modifiers())) {
        vterm_mouse_move(m_vt, p.line - firstVisibleLine(), p.col, vtMods(e->modifiers()));
        return;
    }
    if (m_dragging) {
        m_selEnd = p;
        m_hasSelection = !(m_selStart == m_selEnd);
        Q_EMIT damaged();
    }
}

void TerminalSession::mouseRelease(QMouseEvent *e)
{
    if (forwardingMouse(e->modifiers())) {
        int button = 0;
        switch (e->button()) {
        case Qt::LeftButton:
            button = 1;
            break;
        case Qt::MiddleButton:
            button = 2;
            break;
        case Qt::RightButton:
            button = 3;
            break;
        default:
            return;
        }
        vterm_mouse_button(m_vt, button, false, vtMods(e->modifiers()));
        return;
    }

    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (m_hasSelection && QApplication::clipboard()->supportsSelection()) {
            QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
        }
    }
}

void TerminalSession::wheel(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        Q_EMIT zoomRequested(e->angleDelta().y() > 0 ? 1 : -1);
        return;
    }

    m_wheelAccum += e->angleDelta().y();
    const int steps = m_wheelAccum / 120;
    m_wheelAccum -= steps * 120;
    if (steps == 0) {
        return;
    }

    if (forwardingMouse(e->modifiers())) {
        const Pos p = cellPos(e->position());
        vterm_mouse_move(m_vt, p.line - firstVisibleLine(), p.col, vtMods(e->modifiers()));
        for (int i = 0; i < qAbs(steps); ++i) {
            vterm_mouse_button(m_vt, steps > 0 ? 4 : 5, true, vtMods(e->modifiers()));
        }
    } else if (m_altScreen) {
        // Full-screen programs without mouse support (less, man): scroll with arrow keys.
        for (int i = 0; i < qAbs(steps) * 3; ++i) {
            vterm_keyboard_key(m_vt, steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        }
    } else {
        scrollBy(steps * 3);
    }
}

void TerminalSession::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(m_view);
    QAction *copy = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy"));
    copy->setEnabled(m_hasSelection);
    connect(copy, &QAction::triggered, this, &TerminalSession::copySelection);
    QAction *paste = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), i18n("Paste"));
    connect(paste, &QAction::triggered, this, &TerminalSession::pasteClipboard);
    menu.exec(globalPos);
}
