cmake_minimum_required(VERSION 3.20)

project(kgraphical-term VERSION 0.1.0 LANGUAGES CXX)

# QRhiWidget (the GPU rendering path) needs Qt 6.7 or newer.
set(QT_MIN_VERSION "6.7.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# GuiPrivate provides <rhi/qrhi.h>; ShaderTools provides qsb / qt_add_shaders.
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Widgets ShaderTools)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS GuiPrivate)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    Config
    ConfigWidgets
    I18n
    XmlGui
    Pty
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm>=0.3)

add_executable(kgraphical-term
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/terminalsession.cpp
    src/terminalsession.h
    src/softwareview.cpp
    src/softwareview.h
    src/glyphatlas.cpp
    src/glyphatlas.h
    src/gpuview.cpp
    src/gpuview.h
    src/renderer.cpp
    src/renderer.h
)

qt_add_resources(kgraphical-term "ui"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    FILES data/kgraphical-termui.rc
)

# Compiled to :/shaders/cell.vert.qsb and :/shaders/cell.frag.qsb
# (SPIR-V for Vulkan, GLSL 150 / 120 / 100 es for OpenGL, plus HLSL and MSL).
qt_add_shaders(kgraphical-term "shaders"
    PREFIX "/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    FILES
        shaders/cell.vert
        shaders/cell.frag
)

target_compile_definitions(kgraphical-term PRIVATE
    KGRAPHICALTERM_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(kgraphical-term PRIVATE
    Qt6::Widgets
    Qt6::GuiPrivate
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigWidgets
    KF6::I18n
    KF6::XmlGui
    KF6::Pty
    PkgConfig::VTERM
)

install(TARGETS kgraphical-term ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS data/kgraphical-term.desktop DESTINATION ${KDE_INSTALL_APPDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
