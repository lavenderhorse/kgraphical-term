// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

class QWidget;
class TerminalSession;

enum class RendererBackend {
    Auto,     // pick the best available: OpenGL, then Vulkan, then software
    OpenGL,   // GPU, via QRhi's OpenGL backend
    Vulkan,   // GPU, via QRhi's Vulkan backend
    Software, // QPainter on the CPU
};

bool parseRendererBackend(const QString &name, RendererBackend *out);
QString rendererBackendName(RendererBackend backend);

// Turns Auto into a concrete backend by probing the system. A concrete request
// that cannot be satisfied falls back to the next best backend.
RendererBackend resolveRendererBackend(RendererBackend requested);

// Creates the view widget for `session` and attaches the session to it.
// The session becomes a child of the returned widget.
QWidget *createTerminalView(TerminalSession *session, RendererBackend concreteBackend);
