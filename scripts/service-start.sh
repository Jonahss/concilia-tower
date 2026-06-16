#!/bin/bash
# service-start.sh — systemd entrypoint for ConcilliaTower + VNC stack.
#
# Brings up (idempotently) the display chain, then execs the game in the
# FOREGROUND so systemd owns it: Xvfb :99 -> x11vnc :5900 -> websockify :6080 -> game.
# On a unit restart (game crash), helpers that are still alive are left as-is
# and only the game is re-exec'd. Helpers are backgrounded children that
# survive being orphaned when this script exec()s the game.
#
# Remote access: http://<tailscale-ip>:6080/vnc.html  (no password, 960x720)

set -u
cd "$(dirname "$0")/.."
PROJECT_DIR="$(pwd)"

DISPLAY_NUM=":99"
GEOM="960x720x24"
LOG="/tmp/simtower.log"
export DISPLAY="${DISPLAY_NUM}"

# Locate SIMTOWER.EXE (assets). Override with $SIMTOWER_EXE, else known paths.
find_exe() {
    local c
    for c in "${SIMTOWER_EXE:-}" \
             "${PROJECT_DIR}/../OpenSkyscraper/data/SIMTOWER.EXE" \
             "${HOME}/.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE"; do
        [[ -n "$c" && -f "$c" ]] && { echo "$c"; return 0; }
    done
    echo "ERROR: SIMTOWER.EXE not found. Set \$SIMTOWER_EXE." >&2
    return 1
}
EXE_PATH="$(find_exe)" || exit 1

# Build if the binary is missing (don't rebuild on every restart).
[[ -x "${PROJECT_DIR}/simtower" ]] || make -j4

# --- Xvfb :99 ---------------------------------------------------------------
if ! pgrep -f "Xvfb ${DISPLAY_NUM}" >/dev/null 2>&1; then
    rm -f "/tmp/.X${DISPLAY_NUM#:}-lock" "/tmp/.X11-unix/X${DISPLAY_NUM#:}" 2>/dev/null || true
    Xvfb "${DISPLAY_NUM}" -screen 0 "${GEOM}" </dev/null >/tmp/xvfb.log 2>&1 &
    # wait for the display socket to be ready
    for _ in $(seq 1 20); do
        [[ -S "/tmp/.X11-unix/X${DISPLAY_NUM#:}" ]] && break
        sleep 0.25
    done
fi

# --- x11vnc :5900 -----------------------------------------------------------
if ! pgrep -f "x11vnc.*${DISPLAY_NUM}" >/dev/null 2>&1; then
    x11vnc -display "${DISPLAY_NUM}" -nopw -forever -shared -rfbport 5900 \
        </dev/null >/tmp/x11vnc.log 2>&1 &
    sleep 0.5
fi

# --- websockify / noVNC :6080 ----------------------------------------------
if ! pgrep -f "websockify.*6080" >/dev/null 2>&1; then
    websockify --web /usr/share/novnc 6080 localhost:5900 \
        </dev/null >/tmp/websockify.log 2>&1 &
    sleep 0.5
fi

# --- Game (foreground; systemd tracks this) ---------------------------------
echo "Launching ConcilliaTower on ${DISPLAY_NUM} (assets: ${EXE_PATH})"
exec stdbuf -oL "${PROJECT_DIR}/simtower" "${EXE_PATH}" </dev/null >"${LOG}" 2>&1
