#!/usr/bin/env bash
# Packages the build in ./build as an AppImage using linuxdeploy and its Qt plugin.
# Result: appimage-out/kgraphical-term-<version>-x86_64.AppImage
set -euo pipefail

root="$PWD"
version="$(sed -n 's/^project(kgraphical-term VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
arch="x86_64"
bin_dir="/usr/local/bin"

# Containers have no FUSE, so AppImages are run by extracting them first.
export APPIMAGE_EXTRACT_AND_RUN=1
# Arch's binutils are newer than the strip bundled with linuxdeploy.
export NO_STRIP=1
export QMAKE=/usr/bin/qmake6
export OUTPUT="kgraphical-term-${version}-${arch}.AppImage"
export LDAI_OUTPUT="$OUTPUT"
export LINUXDEPLOY_OUTPUT_VERSION="$version"

for tool in linuxdeploy linuxdeploy-plugin-qt linuxdeploy-plugin-appimage; do
    url="https://github.com/linuxdeploy/${tool}/releases/download/continuous/${tool}-${arch}.AppImage"
    echo "Downloading $url"
    wget -q -O "${bin_dir}/${tool}-${arch}.AppImage" "$url"
    chmod +x "${bin_dir}/${tool}-${arch}.AppImage"
done

rm -rf "$root/AppDir"
DESTDIR="$root/AppDir" cmake --install build --prefix /usr

mkdir -p "$root/appimage-out"
cd "$root/appimage-out"
"${bin_dir}/linuxdeploy-${arch}.AppImage" \
    --appdir "$root/AppDir" \
    --desktop-file "$root/AppDir/usr/share/applications/kgraphical-term.desktop" \
    --icon-file "$root/AppDir/usr/share/icons/hicolor/scalable/apps/kgraphical-term.svg" \
    --plugin qt \
    --output appimage

ls -la "$root/appimage-out"
test -f "$root/appimage-out/$OUTPUT"
