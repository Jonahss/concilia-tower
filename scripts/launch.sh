#!/bin/bash
# launch.sh — Start ConcilliaTower on Xvfb:99 with VNC access
# Usage: ./scripts/launch.sh [--rebuild]
#
# VNC access: http://192.168.1.145:6080/vnc.html
# Process survives shell exit via setsid+nohup+disown.

set -e
cd "$(dirname "$0")/.."
PROJECT_DIR="$(pwd)"
EXE_PATH="${PROJECT_DIR}/../OpenSkyscraper/data/SIMTOWER.EXE"
DISPLAY_NUM=:99
LOG="/tmp/simtower.log"

# Rebuild if requested or binary missing
if [[ "$1" == "--rebuild" ]] || [[ ! -f "${PROJECT_DIR}/simtower" ]]; then
    echo "Building ConcilliaTower..."
    make -j4
fi

# Kill any existing instance
pkill -f "simtower.*SIMTOWER" 2>/dev/null && sleep 0.5 || true

# Ensure Xvfb is running
if ! pgrep -f "Xvfb ${DISPLAY_NUM}" > /dev/null 2>&1; then
    echo "Starting Xvfb on ${DISPLAY_NUM}..."
    setsid Xvfb ${DISPLAY_NUM} -screen 0 960x720x24 </dev/null >/dev/null 2>&1 &
    disown
    sleep 1
fi

# Ensure x11vnc is running
if ! pgrep -f "x11vnc.*display ${DISPLAY_NUM}" > /dev/null 2>&1; then
    echo "Starting x11vnc..."
    setsid x11vnc -display ${DISPLAY_NUM} -nopw -forever -shared -rfbport 5900 </dev/null >/dev/null 2>&1 &
    disown
    sleep 0.5
fi

# Ensure noVNC/websockify is running
if ! pgrep -f "websockify.*6080" > /dev/null 2>&1; then
    echo "Starting noVNC websockify on port 6080..."
    setsid websockify --web /usr/share/novnc 6080 localhost:5900 </dev/null >/dev/null 2>&1 &
    disown
    sleep 0.5
fi

# Launch ConcilliaTower
echo "Launching ConcilliaTower..."
DISPLAY=${DISPLAY_NUM} setsid nohup "${PROJECT_DIR}/simtower" "${EXE_PATH}" \
    </dev/null >"${LOG}" 2>&1 &
disown
GAME_PID=$!

sleep 1

# Verify it's running
if kill -0 ${GAME_PID} 2>/dev/null; then
    echo "✅ ConcilliaTower running (PID ${GAME_PID})"
    echo "   Log: ${LOG}"
    echo "   VNC: http://192.168.1.145:6080/vnc.html"
else
    echo "❌ ConcilliaTower failed to start. Check ${LOG}"
    tail -20 "${LOG}"
    exit 1
fi
