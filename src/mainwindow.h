// SPDX-License-Identifier: MIT
#pragma once

#include "renderer.h"

#include <KXmlGuiWindow>

#include <QFont>
#include <QStringList>

class QTabWidget;
class TerminalSession;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT
public:
    MainWindow(const QStringList &command, const QString &workDir, RendererBackend backend, QWidget *parent = nullptr);

private Q_SLOTS:
    void newTab();
    void closeTab(int index);
    void closeCurrentTab();
    void nextTab();
    void previousTab();
    void copySelection();
    void paste();
    void zoom(int steps);
    void resetZoom();
    void chooseFont();
    void toggleFullScreen();
    void onCurrentChanged(int index);

private:
    void setupActions();
    TerminalSession *currentSession() const;
    static TerminalSession *sessionOf(QWidget *view);
    void addTerminalTab(bool runCommand);
    void applyFont(const QFont &font);
    void saveFont() const;
    void setTabTitle(QWidget *view, const QString &title);

    QTabWidget *m_tabs = nullptr;
    QStringList m_command;
    QString m_workDir;
    RendererBackend m_backend;
    QFont m_font;
    bool m_firstTab = true;
};
