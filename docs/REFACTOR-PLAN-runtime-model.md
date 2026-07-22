# Overnight refactor pass — runtime model modernization (worst offenders)

> **Completed.** The branch (`refactor/runtime-model-20260616`, 4 commits)
> ran under these guardrails, sat reviewed-clean, and was fast-forward merged
> to master on 2026-06-29 with the test suite green. The capacity byte became
> an explicit occupancy model, and the same "be explicit, no reason to save
> bytes" principle carried into later work (e.g. the hotel `RoomCondition`
> enum, July 2026). Kept as the brief + guardrail template for future
> unsupervised passes.

**Authorized by Jonah 2026-06-16 to run ~11pm PT, unsupervised.**
Scope chosen: "worst offenders." Goal: clearer, modern runtime code — NOT a
byte-for-byte transcription of the 1994 original. See memory
`project_simtower_faithful_mechanics_modern_code`.

## THE ONE RULE
**Pure refactor. ZERO behavior change.** Every game mechanic must stay
byte-for-byte faithful. This pass restructures *code*, never *behavior*. If you
cannot modernize something without changing behavior, LEAVE IT and note it.

## Guardrails (hard stops)
1. **Tests are the gate.** After EVERY step:
   - `gcc -I src -o /tmp/test_sim tests/test_sim.c src/tower.c src/game.c src/people.c src/twr.c -lm && /tmp/test_sim` → must be ALL PASS.
   - `make -j4` → must build clean (no new warnings).
   - `.TDT` round-trip: the SCHMITT/BARKLE/THEECSTA fixtures must still
     import→export byte-identically (the existing round-trip checks). Do not
     change the on-disk `.TDT` byte layout.
   If anything goes red and you can't fix it in one focused attempt, **`git
   revert`/reset that step, leave it un-done, and move on** — never push through
   a red state.
2. **Commit per step**, small and described. Commits ONLY — never push/publish.
   End each commit message with the Co-Authored-By line.
3. **Save-layout stays quarantined.** The `.TDT` serializer (`twr.c`) must keep
   the exact original byte layout — that's an external constraint (real 1995
   saves must round-trip). The point of the refactor is to give the RUNTIME a
   clean model and translate to/from the byte layout in the serializer only.
4. Stop by ~6am PT regardless. Leave a summary.

## Scope (in order; stop when a step is risky)
1. **`capacity` byte → explicit occupancy model.** Today it packs a persistent
   tier + a daily oscillation in one byte (CAP_MIN/STEP/MAX, cap_peak,
   cap_daily_floor, cap_tier_top). Replace with clear named fields/enum on the
   tenant (e.g. `occupancy_tier` + `daily_occupancy`), with helpers that read
   like prose. Preserve EXACT numeric behavior (the same fill/empty curves,
   the same frame-selection results in main.c). The `.TDT`/save path translates
   to/from the old byte.
2. **Cell `flags` bitfield → named booleans/struct** (`has_transport_overlay`,
   etc.). Keep grid memory reasonable but readable.
3. **`Tenant` struct cleanup** — name fields clearly, group related state,
   add explanatory comments. Don't change semantics.

Each is independent; do as many as land cleanly. Better to ship ONE system
fully-clean + green than three half-done.

## Deliverable
Write a morning summary to `~/.claude-agent/notes/memory/2026-06-17.md` (append):
what changed (per commit), what was left un-done and why, test/build/round-trip
status at stop time, and any behavior-risk you want Jonah's eyes on. Be honest
about anything uncertain. Do NOT relaunch the live game service (Jonah may be
testing); just leave the commits on the branch for review.
