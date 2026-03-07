#!/bin/bash
# stop.sh — Kill ConcilliaTower (keeps VNC infrastructure alive)
pkill -f "simtower.*SIMTOWER" 2>/dev/null && echo "✅ ConcilliaTower stopped" || echo "Not running"
