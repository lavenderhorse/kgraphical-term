// SPDX-License-Identifier: MIT
#include "mainwindow.h"
#include "renderer.h"

#include <KAboutData>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("kgraphical-term");

    KAboutData about(QStringLiteral("kgraphical-term"),
                     i18n("KGraphical-Term"),
                     QStringLiteral(KGRAPHICALTERM_VERSION));
    about.setShortDescription(i18n("A GPU-accelerated terminal emulator built on KDE Frameworks 6 and Qt 6."));
    about.setDesktopFileName(QStringLiteral("kgraphical-term"));
    KAboutData::setApplicationData(about);
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("kgraphical-term"), QIcon::fromTheme(QStringLiteral("utilities-terminal"))));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addOption(QCommandLineOption({QStringLiteral("w"), QStringLiteral("workdir")},
                                        i18n("Start in the directory <dir>."),
                                        QStringLiteral("dir")));
    parser.addOption(QCommandLineOption({QStringLiteral("r"), QStringLiteral("renderer")},
                                        i18n("Rendering backend: auto, opengl, vulkan or software."),
                                        QStringLiteral("backend")));
    parser.addPositionalArgument(QStringLiteral("command"),
                                 i18n("Program to run instead of the login shell, followed by its arguments."),
                                 QStringLiteral("[command [args...]]"));
    parser.process(app);
    about.processCommandLine(&parser);

    // Backend precedence: --renderer, then the config file, then auto.
    QString backendName = parser.value(QStringLiteral("renderer"));
    if (backendName.isEmpty()) {
        backendName = KSharedConfig::openConfig()->group(QStringLiteral("General")).readEntry("Renderer", QStringLiteral("auto"));
    }
    RendererBackend requested = RendererBackend::Auto;
    if (!parseRendererBackend(backendName, &requested)) {
        qWarning("kgraphical-term: unknown renderer '%s', using auto", qPrintable(backendName));
        requested = RendererBackend::Auto;
    }
    const RendererBackend backend = resolveRendererBackend(requested);
    if (requested != RendererBackend::Auto && backend != requested) {
        qWarning("kgraphical-term: %s is not available here, falling back to %s",
                 qPrintable(rendererBackendName(requested)),
                 qPrintable(rendererBackendName(backend)));
    }
    // Plain stderr on purpose: release builds compile qInfo()/qDebug() out.
    std::fprintf(stderr, "kgraphical-term: renderer = %s\n", qPrintable(rendererBackendName(backend)));

    auto *window = new MainWindow(parser.positionalArguments(), parser.value(QStringLiteral("workdir")), backend);
    window->show();

    return app.exec();
}
