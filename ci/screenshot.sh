#!/usr/bin/env bash
# Runs kgraphical-term under Xvfb with the given rendering backend, screenshots
# the screen, and checks that something was actually drawn.
#
# Usage: ci/screenshot.sh <software|opengl|vulkan>
set -uo pipefail

backend="${1:?usage: screenshot.sh <software|opengl|vulkan>}"
out="${OUT:-ci-out}"
mkdir -p "$out"
shot="$out/screenshot-$backend.png"
log="$out/app-$backend.log"

export DISPLAY=:99
export QT_QPA_PLATFORM=xcb
export LIBGL_ALWAYS_SOFTWARE=1
export QT_FORCE_STDERR_LOGGING=1
export QT_LOGGING_RULES="qt.rhi.*=true;qt.qpa.gl=true"
export XDG_RUNTIME_DIR=/tmp/runtime-ci
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

app=""
Xvfb :99 -screen 0 1024x640x24 -nolisten tcp >"$out/xvfb-$backend.log" 2>&1 &
xvfb=$!
cleanup() {
    [ -n "$app" ] && kill "$app" 2>/dev/null
    kill "$xvfb" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT
sleep 2

fail() {
    echo "::error title=Render check failed ($backend)::$*"
    echo "---- application log ----"
    cat "$log"
    exit 1
}

build/bin/kgraphical-term --renderer "$backend" -w /tmp -- bash "$PWD/ci/demo.sh" >"$log" 2>&1 &
app=$!
sleep 10

kill -0 "$app" 2>/dev/null || fail "application exited early"

import -window root "$shot" || fail "could not capture the screen"
colors="$(identify -format %k "$shot")"
echo "[$backend] screenshot has $colors distinct colours"

grep -q "renderer = $backend" "$log" || fail "backend '$backend' was not selected (fell back?)"
if [ "$backend" != software ]; then
    grep -q "GPU renderer using" "$log" || fail "the GPU renderer never initialised"
fi
[ "$colors" -gt 30 ] || fail "screenshot looks blank ($colors colours)"

echo "---- application log ----"
cat "$log"
