// SPDX-License-Identifier: MIT
#include "renderer.h"

#include "gpuview.h"
#include "softwareview.h"
#include "terminalsession.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QtGui/qtguiglobal.h>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

namespace
{
bool probeOpenGL()
{
    QOffscreenSurface surface;
    surface.create();
    if (!surface.isValid()) {
        return false;
    }
    QOpenGLContext context;
    if (!context.create()) {
        return false;
    }
    const bool ok = context.makeCurrent(&surface);
    context.doneCurrent();
    return ok;
}

bool probeVulkan()
{
#if QT_CONFIG(vulkan)
    QVulkanInstance instance;
    const bool ok = instance.create();
    instance.destroy();
    return ok;
#else
    return false;
#endif
}
} // namespace

bool parseRendererBackend(const QString &name, RendererBackend *out)
{
    const QString n = name.trimmed().toLower();
    if (n.isEmpty() || n == QLatin1String("auto")) {
        *out = RendererBackend::Auto;
    } else if (n == QLatin1String("opengl") || n == QLatin1String("gl")) {
        *out = RendererBackend::OpenGL;
    } else if (n == QLatin1String("vulkan") || n == QLatin1String("vk")) {
        *out = RendererBackend::Vulkan;
    } else if (n == QLatin1String("software") || n == QLatin1String("raster") || n == QLatin1String("cpu")) {
        *out = RendererBackend::Software;
    } else {
        return false;
    }
    return true;
}

QString rendererBackendName(RendererBackend backend)
{
    switch (backend) {
    case RendererBackend::Auto:
        return QStringLiteral("auto");
    case RendererBackend::OpenGL:
        return QStringLiteral("opengl");
    case RendererBackend::Vulkan:
        return QStringLiteral("vulkan");
    case RendererBackend::Software:
        return QStringLiteral("software");
    }
    return QStringLiteral("software");
}

RendererBackend resolveRendererBackend(RendererBackend requested)
{
    // Preference order for each request; the first backend that probes OK wins.
    QList<RendererBackend> order;
    switch (requested) {
    case RendererBackend::Software:
        return RendererBackend::Software;
    case RendererBackend::Vulkan:
        order = {RendererBackend::Vulkan, RendererBackend::OpenGL};
        break;
    case RendererBackend::OpenGL:
    case RendererBackend::Auto:
        order = {RendererBackend::OpenGL, RendererBackend::Vulkan};
        break;
    }

    for (const RendererBackend candidate : std::as_const(order)) {
        const bool ok = candidate == RendererBackend::OpenGL ? probeOpenGL() : probeVulkan();
        if (ok) {
            return candidate;
        }
    }
    return RendererBackend::Software;
}

QWidget *createTerminalView(TerminalSession *session, RendererBackend backend)
{
    QWidget *view = nullptr;
    switch (backend) {
    case RendererBackend::OpenGL:
        view = new GpuView(session, QRhiWidget::Api::OpenGL);
        break;
    case RendererBackend::Vulkan:
        view = new GpuView(session, QRhiWidget::Api::Vulkan);
        break;
    case RendererBackend::Auto:
    case RendererBackend::Software:
        view = new SoftwareView(session);
        break;
    }
    session->attachView(view);
    session->setParent(view);
    return view;
}
