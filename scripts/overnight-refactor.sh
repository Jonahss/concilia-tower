#!/bin/bash
# overnight-refactor.sh — one-shot scheduled runtime-model refactor pass.
# Scheduled by Claw for ~11pm PT 2026-06-16 (Jonah authorized). Self-removes its
# own cron line so it runs ONCE. Runs a headless Claude session that executes
# docs/REFACTOR-PLAN-runtime-model.md under its guardrails.

export HOME=/home/concilliator-claw
export PATH="$HOME/.local/bin:$HOME/.bun/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
# Auth: the service authenticates via CLAUDE_CODE_OAUTH_TOKEN from this env file.
set -a; . "$HOME/.claude/oauth-token.env" 2>/dev/null; set +a
PROJ="$HOME/projects/simtower-linux"
LOG="/tmp/overnight-refactor-$(date +%Y%m%d).log"

# Self-remove: drop any crontab line referencing this script (one-time job).
( crontab -l 2>/dev/null | grep -v 'overnight-refactor.sh' ) | crontab - 2>/dev/null || true

cd "$PROJ" || exit 1

# Make sure git can commit non-interactively.
git config user.name  >/dev/null 2>&1 || git config user.name  "Claw"
git config user.email >/dev/null 2>&1 || git config user.email "claw@conciliator.local"

# Work on a review branch off the current state.
BR="refactor/runtime-model-$(date +%Y%m%d)"
git checkout -b "$BR" 2>>"$LOG" || git checkout "$BR" 2>>"$LOG"

PROMPT='You are Claw, working autonomously overnight while Jonah sleeps. Read docs/REFACTOR-PLAN-runtime-model.md in this repo and execute that runtime-model refactor pass EXACTLY per its guardrails: pure refactor with ZERO behavior change; run the sim tests + build + .TDT round-trip after every step and keep them green; commit per small step (commits only, never push); if a step goes red and one focused fix does not recover it, reset that step and move on; stop by ~6am PT; and append an honest morning summary to ~/.claude-agent/notes/memory/2026-06-17.md. Stay on the current git branch. Do NOT relaunch the concilliatower-vnc service. Mechanics must stay faithful to the original SimTower (verify against ~/.claude-agent/archive/openclaw/workspace/projects/simtower-decomp/annotated/ if unsure); this pass only modernizes code structure.'

echo "=== overnight refactor start $(date) on branch $BR ===" >>"$LOG"
timeout 6h claude -p "$PROMPT" --dangerously-skip-permissions >>"$LOG" 2>&1
echo "=== overnight refactor end $(date) (rc=$?) ===" >>"$LOG"
