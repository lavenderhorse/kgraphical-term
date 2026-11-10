// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include "terminalsession.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KStandardActions>

#include <QAction>
#include <QApplication>
#include <QFontDatabase>
#include <QFontDialog>
#include <QIcon>
#include <QTabBar>
#include <QTabWidget>

namespace
{
QFont defaultTerminalFont()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}
} // namespace

MainWindow::MainWindow(const QStringList &command, const QString &workDir, RendererBackend backend, QWidget *parent)
    : KXmlGuiWindow(parent)
    , m_command(command)
    , m_workDir(workDir)
    , m_backend(backend)
{
    m_font = KSharedConfig::openConfig()->group(QStringLiteral("General")).readEntry("Font", defaultTerminalFont());

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setTabBarAutoHide(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentChanged);
    setCentralWidget(m_tabs);

    setupActions();
    // The size is used on the first run; afterwards the saved window size wins.
    setupGUI(QSize(900, 560), Default, QStringLiteral(":/kgraphical-termui.rc"));

    addTerminalTab(true);
}

void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    auto add = [this, ac](const QString &name, const QString &text, const QString &icon, const QKeySequence &shortcut, auto slot) {
        auto *action = new QAction(QIcon::fromTheme(icon), text, this);
        ac->addAction(name, action);
        ac->setDefaultShortcut(action, shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    add(QStringLiteral("new_tab"), i18n("New Tab"), QStringLiteral("tab-new"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), &MainWindow::newTab);
    add(QStringLiteral("close_tab"), i18n("Close Tab"), QStringLiteral("tab-close"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W), &MainWindow::closeCurrentTab);
    add(QStringLiteral("copy_selection"), i18n("Copy"), QStringLiteral("edit-copy"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), &MainWindow::copySelection);
    add(QStringLiteral("paste_clipboard"), i18n("Paste"), QStringLiteral("edit-paste"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), &MainWindow::paste);
    add(QStringLiteral("prev_tab"), i18n("Previous Tab"), QStringLiteral("go-previous"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Left), &MainWindow::previousTab);
    add(QStringLiteral("next_tab"), i18n("Next Tab"), QStringLiteral("go-next"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Right), &MainWindow::nextTab);
    add(QStringLiteral("zoom_reset"), i18n("Reset Zoom"), QStringLiteral("zoom-original"), QKeySequence(Qt::CTRL | Qt::Key_0), &MainWindow::resetZoom);
    add(QStringLiteral("choose_font"), i18n("Choose Font…"), QStringLiteral("preferences-desktop-font"), QKeySequence(), &MainWindow::chooseFont);
    add(QStringLiteral("toggle_fullscreen"), i18n("Full Screen Mode"), QStringLiteral("view-fullscreen"), QKeySequence(Qt::Key_F11), &MainWindow::toggleFullScreen);

    KStandardActions::quit(qApp, &QApplication::closeAllWindows, ac);
    KStandardActions::zoomIn(this, [this] { zoom(1); }, ac);
    KStandardActions::zoomOut(this, [this] { zoom(-1); }, ac);
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

TerminalSession *MainWindow::sessionOf(QWidget *view)
{
    return view ? view->findChild<TerminalSession *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

TerminalSession *MainWindow::currentSession() const
{
    return sessionOf(m_tabs->currentWidget());
}

void MainWindow::addTerminalTab(bool runCommand)
{
    // New tabs open in the directory of the one you are looking at.
    QString workDir;
    if (TerminalSession *current = currentSession()) {
        workDir = current->currentWorkingDirectory();
    }
    if (m_firstTab && !m_workDir.isEmpty()) {
        workDir = m_workDir;
    }

    auto *session = new TerminalSession;
    session->setFont(m_font);
    QWidget *view = createTerminalView(session, m_backend);

    connect(session, &TerminalSession::titleChanged, this, [this, view](const QString &title) {
        setTabTitle(view, title);
    });
    connect(session, &TerminalSession::finished, this, [this, view] {
        const int index = m_tabs->indexOf(view);
        if (index >= 0) {
            closeTab(index);
        }
    });
    connect(session, &TerminalSession::zoomRequested, this, &MainWindow::zoom);

    QString program;
    QStringList args;
    if (runCommand && !m_command.isEmpty()) {
        program = m_command.first();
        args = m_command.mid(1);
    } else {
        program = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
    }
    session->start(program, args, workDir);

    const int index = m_tabs->addTab(view, i18n("Shell"));
    m_tabs->setCurrentIndex(index);
    view->setFocus();
    m_firstTab = false;
}

void MainWindow::newTab()
{
    addTerminalTab(false);
}

void MainWindow::closeTab(int index)
{
    QWidget *view = m_tabs->widget(index);
    if (!view) {
        return;
    }
    m_tabs->removeTab(index);
    view->deleteLater();
    if (m_tabs->count() == 0) {
        close();
    }
}

void MainWindow::closeCurrentTab()
{
    closeTab(m_tabs->currentIndex());
}

void MainWindow::nextTab()
{
    const int n = m_tabs->count();
    if (n > 1) {
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % n);
    }
}

void MainWindow::previousTab()
{
    const int n = m_tabs->count();
    if (n > 1) {
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + n - 1) % n);
    }
}

void MainWindow::onCurrentChanged(int index)
{
    if (index < 0) {
        return;
    }
    QWidget *view = m_tabs->widget(index);
    view->setFocus();
    setCaption(view->property("terminalTitle").toString());
}

void MainWindow::setTabTitle(QWidget *view, const QString &title)
{
    view->setProperty("terminalTitle", title);
    const int index = m_tabs->indexOf(view);
    if (index < 0) {
        return;
    }
    QString label = title.isEmpty() ? i18n("Shell") : title;
    label.replace(QLatin1Char('&'), QStringLiteral("&&"));
    m_tabs->setTabText(index, label);
    if (index == m_tabs->currentIndex()) {
        setCaption(title);
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::copySelection()
{
    if (TerminalSession *s = currentSession()) {
        s->copySelection();
    }
}

void MainWindow::paste()
{
    if (TerminalSession *s = currentSession()) {
        s->pasteClipboard();
    }
}

void MainWindow::zoom(int steps)
{
    QFont font = m_font;
    const qreal size = font.pointSizeF() > 0 ? font.pointSizeF() : 11.0;
    font.setPointSizeF(qBound(6.0, size + steps, 48.0));
    applyFont(font);
}

void MainWindow::resetZoom()
{
    applyFont(defaultTerminalFont());
}

void MainWindow::chooseFont()
{
    bool ok = false;
    const QFont font = QFontDialog::getFont(&ok, m_font, this, i18n("Select Terminal Font"), QFontDialog::MonospacedFonts);
    if (ok) {
        applyFont(font);
    }
}

void MainWindow::applyFont(const QFont &font)
{
    m_font = font;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (TerminalSession *s = sessionOf(m_tabs->widget(i))) {
            s->setFont(m_font);
        }
    }
    saveFont();
}

void MainWindow::saveFont() const
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("General"));
    group.writeEntry("Font", m_font);
    group.sync();
}

void MainWindow::toggleFullScreen()
{
    setWindowState(windowState() ^ Qt::WindowFullScreen);
}
