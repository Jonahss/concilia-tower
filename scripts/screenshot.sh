#!/bin/bash
# screenshot.sh — Take a screenshot of ConcilliaTower via xdotool F12
# Usage: ./scripts/screenshot.sh [output.png]
#
# If the game isn't running, uses --screenshot mode to render a frame.
# Output is always PNG (converted from BMP via ffmpeg).

set -e
cd "$(dirname "$0")/.."
PROJECT_DIR="$(pwd)"

# Locate SIMTOWER.EXE (assets). Override with $SIMTOWER_EXE, else try known paths.
find_exe() {
    local c
    for c in "${SIMTOWER_EXE}" \
             "${PROJECT_DIR}/../OpenSkyscraper/data/SIMTOWER.EXE" \
             "${HOME}/.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE"; do
        [[ -n "$c" && -f "$c" ]] && { echo "$c"; return 0; }
    done
    echo "ERROR: SIMTOWER.EXE not found. Set \$SIMTOWER_EXE." >&2
    return 1
}
EXE_PATH="$(find_exe)" || exit 1
DISPLAY_NUM=:99
BMP_PATH="/tmp/simtower_screenshot.bmp"
OUT="${1:-/tmp/ct_screenshot.png}"

if pgrep -f "simtower.*SIMTOWER" > /dev/null 2>&1; then
    # Game is running — send F12 to capture
    echo "Game running, sending F12..."
    DISPLAY=${DISPLAY_NUM} xdotool key F12
    sleep 1
    if [[ -f "${BMP_PATH}" ]]; then
        ffmpeg -y -i "${BMP_PATH}" "${OUT}" 2>/dev/null
        echo "✅ Screenshot saved: ${OUT}"
    else
        echo "❌ F12 didn't produce screenshot. Try --screenshot mode."
        exit 1
    fi
else
    # Game not running — use headless screenshot mode
    echo "Game not running, using headless --screenshot mode (200 frames of sim)..."
    
    # Ensure Xvfb
    if ! pgrep -f "Xvfb ${DISPLAY_NUM}" > /dev/null 2>&1; then
        setsid Xvfb ${DISPLAY_NUM} -screen 0 960x720x24 </dev/null >/dev/null 2>&1 &
        disown; sleep 1
    fi
    
    DISPLAY=${DISPLAY_NUM} "${PROJECT_DIR}/simtower" "${EXE_PATH}" --screenshot "${BMP_PATH}"
    ffmpeg -y -i "${BMP_PATH}" "${OUT}" 2>/dev/null
    echo "✅ Screenshot saved: ${OUT}"
fi

ls -lh "${OUT}"
