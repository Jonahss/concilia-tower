#!/bin/bash
# stop.sh — Kill ConciliaTower (keeps VNC infrastructure alive)
pkill -f "simtower.*SIMTOWER" 2>/dev/null && echo "✅ ConciliaTower stopped" || echo "Not running"
