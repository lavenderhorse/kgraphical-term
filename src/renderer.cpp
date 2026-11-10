// SPDX-License-Identifier: MIT
#include "renderer.h"

#include "gpuview.h"
#include "softwareview.h"
#include "terminalsession.h"

#include <QLibrary>
#include <QOffscreenSurface>
#include <QOpenGLContext>

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

// Checks that a Vulkan loader exists and that it can see at least one physical
// device. Done by hand through the loader's C API so it needs no Vulkan headers
// and no particular Qt Vulkan configuration.
bool probeVulkan()
{
    using CreateInstanceFn = int (*)(const void *createInfo, const void *allocator, void **instance);
    using DestroyInstanceFn = void (*)(void *instance, const void *allocator);
    using EnumeratePhysicalDevicesFn = int (*)(void *instance, unsigned *count, void *devices);

    QLibrary loader(QStringLiteral("vulkan"), 1);
    if (!loader.load()) {
        return false;
    }
    const auto create = reinterpret_cast<CreateInstanceFn>(loader.resolve("vkCreateInstance"));
    const auto destroy = reinterpret_cast<DestroyInstanceFn>(loader.resolve("vkDestroyInstance"));
    const auto enumerate = reinterpret_cast<EnumeratePhysicalDevicesFn>(loader.resolve("vkEnumeratePhysicalDevices"));
    if (!create || !destroy || !enumerate) {
        return false;
    }

    // A zeroed VkInstanceCreateInfo with sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO (1).
    alignas(8) char createInfo[128] = {};
    *reinterpret_cast<int *>(createInfo) = 1;

    void *instance = nullptr;
    if (create(createInfo, nullptr, &instance) != 0 || !instance) {
        return false;
    }
    unsigned deviceCount = 0;
    const bool ok = enumerate(instance, &deviceCount, nullptr) == 0 && deviceCount > 0;
    destroy(instance, nullptr);
    return ok;
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
