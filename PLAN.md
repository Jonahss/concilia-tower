# ConcilliaTower — Build Plan

> **Status: all six phases are complete** (March–July 2026). This document is
> kept as the project's build map and phase history; for what the port does
> today, see the [README](README.md#status). The live worklist of remaining
> polish and mod ideas is [`UI_TODO.md`](UI_TODO.md).

## Architecture

**Goal:** Native Linux port of SimTower using original assets from SIMTOWER.EXE.
Game mechanics reimplemented from scratch, with their behaviour and constants
reverse-engineered from a Ghidra decompilation + the YootTower code map.
Platform code written fresh in C + SDL2.

```
SIMTOWER.EXE (assets) ──→ ne_resource.c (parser) ──→ Game
                                                       │
Decompiled segments ──→ Reference for game logic       │
YootTower code map  ──→ Function names/structure       │
                                                       ▼
                                              SDL2 (render/sound/input)
```

### Read from the original EXE at runtime (and only this):
- All bitmap/sprite assets (NE resource table)
- All sound effects (WAV resources)

### Reimplemented from scratch, matching the original's behaviour:
- Game mechanics (population, tenants, elevators, star ratings, disasters)
- Magic numbers, thresholds, timing constants — re-derived by *studying* the
  decompilation, then verified against it; no original code is extracted or
  executed (see the README's "reuses vs. rewrites" section for the full story)

### Written fresh:
- Rendering (SDL2 instead of WinG)
- Sound playback (SDL2 audio instead of WaveMix)
- Save/load — a native quick-save **plus** full import/export of the original
  SimTower 1.1 `.TWR`/`.TDT` format (real 1995/96 towers round-trip
  byte-identically; this superseded the early "own format only" plan)
- Window management, input (SDL2)
- Asset loading pipeline (NE parser + SDL surface conversion)

## Build Phases

### Phase 1: Static Tower ✅ (Mar 2026)
- [x] NE resource parser (ne_resource.c)
- [x] Palette loading (256-color from resource 0x83e8)
- [x] DIB bitmap → SDL surface conversion
- [x] Raw bitmap → SDL surface conversion (8px-wide cell tiles)
- [x] SDL2 window with basic rendering
- [x] Bitmap dump mode for verification
- [x] Sprite atlas system — load and catalog all game sprites by ID
- [x] Tower grid — 2D array: 63 cells wide × 115 floors (B10 to +105)
- [x] Floor rendering — tile lobby/office/condo sprites across occupied cells
- [x] Sky + underground backgrounds — original sky bitmaps
- [x] Camera/scroll — pan around the tower with mouse/keyboard
- [x] Basic UI — toolbar, info bar (faithful pass Jun 2026: bg art 0x8140
      measurements, 128px/4-col toolbox with pull-downs, Win3.1 menu bar)

### Phase 2: Placement & Money ✅ (Jun 2026)
- [x] Click-to-build: select item type, click to place on grid
- [x] Building costs (from decompiled ParamT / tuning resources)
- [x] Money display, running balance (income/expense settlement on the EXE's
      real quarterly cadence; rent table res 0x3E9)
- [x] Delete/demolish items
- [x] Floor number display

### Phase 3: Time & People ✅ (Jun 2026)
- [x] Day/night cycle
- [x] Time progression: weekday/weekend cycle
- [x] Population counter (standing population, time-of-day independent)
- [x] People spawning and movement (leg-by-leg trip planning, real walk
      budgets + route cost table from the decomp)
- [x] Tenant satisfaction (JudgeT zones; EXE's banked felt-wait stress model,
      daily category pass, demand re-arm — the Jul 2026 occupancy-lifecycle
      keystone)

### Phase 4: Elevators ✅ (Jun–Jul 2026)
- [x] Elevator shaft placement (drag-to-extend, faithful placement rules)
- [x] Car movement + door cycle (state machine from the decomp; queues,
      3-strike wait stress with the real tuning constants, res 0x7F05)
- [x] People queueing and riding (ElvPeple)
- [x] Elevator editor dialog — rebuilt on the original 0x8190 artwork, plus
      the full-screen "Simulate" shaft-edit mode (ElvEditT, seg_10f0)
- [x] Express/service/standard types (real stop rules; express sky-lobby
      anchoring; red car-is-here floor plates, 0x87EC)

### Phase 5: Star Rating ✅ (Jun–Jul 2026)
- [x] Star progression system (population thresholds on standing population)
- [x] Level-up conditions (VIP gate for 3→4 and 4→5, security, suites,
      recycling, medical adequacy, metro; weekday-evening promotion window)
- [x] Unlockable building types per star level (campaign mode; sandbox = all)
- [x] Tower rating UI display (earned golds + grey slots, 21px cells)
- [x] The 5⭐→TOWER win: cathedral + VIP + population → wedding ceremony day
      (procession, 6-strip ceremony art) → TOWER rank

### Phase 6: Full Simulation ✅ (Jun–Jul 2026)
- [x] All tenant types (office, condo, hotel ×3, restaurant, fast food, shop,
      cinema, party hall, parking, metro, recycling, security, housekeeping,
      medical, cathedral) with state-driven sprites and per-type economies
- [x] Hotel housekeeping + the full room-condition model (clean/dirty/
      infested, booking demand, neglect fuse — MainteT)
- [x] Fire/terrorist events: scheduled disasters, per-floor fire fronts,
      $500k helicopter, bomb ransom/hunt, decision modals, rubble aftermath,
      alert banners, real flame art
- [x] Sound effects — SDL2 audio backend, event + ambient sounds wired from
      referee'd EXE WAV ids (a few gaps remain — see UI_TODO.md)
- [x] VIP visits (day cadence, hotel-quality check, promo flag)
- [x] Parking (ramp chains, 2N quotas), metro (visitors), cathedral,
      medical center
- [x] Flavor: medical emergencies, Santa flyby, fire-glow flash, star-up
      certificate, multi-story-lobby wait forgiveness

## Key Files

| File | Purpose |
|------|---------|
| `src/ne_resource.{h,c}` | NE executable resource parser |
| `src/sprites.{h,c}` | Sprite atlas: load, catalog, render game sprites |
| `src/tower.{h,c}` | Tower grid data structure + cell management |
| `src/game.{h,c}` | Game state, time, money, simulation |
| `src/people.{h,c}` | People, trips, elevator queues and cars |
| `src/twr.{h,c}` | Original `.TWR`/`.TDT` import/export |
| `src/audio.{h,c}` | SDL2 audio backend (EXE WAV playback) |
| `src/main.c` | Entry point, rendering, UI, dialogs, event loop |

## Sprite ID Reference

> **Superseded — kept for history.** This table was the early exploration
> pass, and several rows were later proven wrong by the sprite-state decode
> work (e.g. `0x85a8` is the *office* interior family, `0x8668` the shop
> variant sheets, `0x87ec` the red shaft floor-digit variant). The
> authoritative mapping is the `SPR_*` defines and `sprites_init` loading
> table in `src/main.c`, and the per-sheet notes in
> [`docs/ORIGINAL-BUGS.md`](docs/ORIGINAL-BUGS.md) /
> [`docs/OPENSKYSCRAPER-ERRATA.md`](docs/OPENSKYSCRAPER-ERRATA.md).

<details>
<summary>Original exploration table (historical, partly wrong)</summary>

### Bitmap resource IDs (type 0x8002)
- `0x8080` — Maxis logo
- `0x8100` — Title screen (SimTower entrance)
- `0x81fe-0x8200` — Sky backgrounds (day/night/sunset)
- `0x8258-0x825e` — Cloud sprites
- `0x8351-0x835b` — Elevator shaft sprites
- `0x83e8-0x83eb` — Palette display bitmaps
- `0x8428-0x842d` — Floor/ceiling sprites
- `0x8468-0x8469` — Office sprites
- `0x84a8-0x84ab` — Condo sprites
- `0x84e8-0x84ef` — Restaurant sprites
- `0x8528-0x852b` — Shop sprites
- `0x8568-0x8571` — Restaurant sprites
- `0x85a8-0x85ab` — Fast food sprites *(actually office interiors)*
- `0x85e8-0x85ee` — Cinema/party hall sprites *(0x85e9 = interior people)*
- `0x8628-0x8636` — Hotel room sprites
- `0x8668-0x8674` — Hotel suite sprites(?) *(actually shop variant sheets)*
- `0x86a8-0x86a9` — Security sprites
- `0x86e8-0x86f1` — Parking sprites *(0x86e8+ = fast-food variant sheets)*
- `0x87e8-0x87ed` — Metro station sprites *(0x87e9/0x87ec = shaft floor digits)*
- `0x8828` — Cathedral *(actually the wedding procession animation)*
- `0x8868` — Medical center
- `0x88a8` — Recycling center
- `0x88e8-0x88ed` — Stairway sprites
- `0x8928-0x892e` — Recycling center sprites
- `0x8968-0x8969` — Housekeeping/maintenance
- `0x89a8-0x89a9` — Additional escalator
- `0x8f28` — Underground dirt tile
- `0x8f68-0x8f6d` — Foundation/structure sprites *(actually the fire family)*

### Raw bitmap resource IDs (type 0xFF02)
- `0x89e8-0x89ea` — People walking sprites (124 cells each!)
- `0x8F68-0x8F6B` — **Fire/large** flame animation (4 frames, 96x36 = 12-cell front; white-keyed)
- `0x8F6C` fire/small · `0x8F6D` fire/chopper · `0x8FA8` fire/destroyed (burnt cell)
- `0xA710` alert/terrorist (76x67) · `0xA714` alert/fire (76x60) — white-keyed alert icons
- Fire is an 8-frame cycle in the EXE (frame=b3de%4, +4 when extinguishing), 12 cells wide per front, drawn from the master tile-sheet; the port tiles the 4 standalone DIB frames across [fire_left,fire_right]
- Event dialogs (string res): fire start 0xBC2/0xBC3, caught 0xBCF, exploded 0xBD0; sounds 0x2714 bomb / 0x2716 fire alarm (both wired)
- `0x8a28-0x8a2a` — More people sprites
- `0x8a68-0x8a6a` — Even more people sprites
- `0x8fe9` — Large sprite sheet (552 cells)
- `0x8fea` — Largest sprite sheet (736 cells)

### Sound resource IDs (type 0xFF0A)
- 58 sounds total — elevators, doors, ambient, events, etc.

</details>

## Decompilation Reference

The decompiled code lives in `../simtower-decomp/` (not part of this repo):
- `segments/seg_XXXX.c` — per-segment decompiled C
- `segment-mapping.md` — which segment = which module
- `analysis.json` — machine-readable mapping

Key segments per phase:
- Phase 1: seg_1208 (DrawT), seg_1048 (draw utilities)
- Phase 2: seg_1188 (ParamT — costs/parameters), seg_1118 (tenant)
- Phase 3: seg_1020 (AnimeT — CLUT), seg_1060 (CountT), seg_11d8 (TimeT)
- Phase 4: seg_1090 (ElevatorsT), seg_10a8 (ElvPeple), seg_1098 (ElvDlogT),
  seg_10f0 (ElvEditT)
- Phase 5: seg_1128 (LevelT/LevelUp)
- Phase 6: seg_1130 (MainteT), seg_10e8 (FireT), seg_10c8 (EventT),
  seg_11c8 (SoundT), seg_11a8 (JudgeT)
