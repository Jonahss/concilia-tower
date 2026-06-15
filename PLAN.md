# SimTower for Linux — Build Plan

## Architecture

**Goal:** Native Linux port of SimTower using original assets from SIMTOWER.EXE.
Game mechanics extracted from Ghidra decompilation + YootTower code map.
Platform code written fresh in C + SDL2.

```
SIMTOWER.EXE (assets) ──→ ne_resource.c (parser) ──→ Game
                                                       │
Decompiled segments ──→ Reference for game logic       │
YootTower code map  ──→ Function names/structure       │
                                                       ▼
                                              SDL2 (render/sound/input)
```

### What we reuse from the original:
- All bitmap/sprite assets (loaded from EXE at runtime)
- All sound effects (WAV resources from EXE)
- Game mechanics logic (population, tenants, elevators, star ratings)
- Magic numbers, thresholds, timing constants

### What we write fresh:
- Rendering (SDL2 instead of WinG)
- Sound playback (SDL2_mixer instead of WaveMix)
- Save/load (our own format — JSON or binary, no .TDT compatibility needed)
- Window management, input (SDL2)
- Asset loading pipeline (our NE parser + SDL surface conversion)

## Build Phases

### Phase 1: Static Tower ✅ → 🔨 IN PROGRESS
- [x] NE resource parser (ne_resource.c)
- [x] Palette loading (256-color from resource 0x83e8)
- [x] DIB bitmap → SDL surface conversion
- [x] Raw bitmap → SDL surface conversion (8px-wide cell tiles)
- [x] SDL2 window with basic rendering
- [x] Bitmap dump mode for verification
- [ ] **Sprite atlas system** — load and catalog all game sprites by ID
- [ ] **Tower grid** — 2D array: 63 cells wide × ~130 floors (-9 to +100+)
- [ ] **Floor rendering** — tile lobby/office/condo sprites across occupied cells
- [ ] **Sky + underground backgrounds** — use original sky bitmaps
- [ ] **Camera/scroll** — pan around the tower with mouse/keyboard
- [ ] **Basic UI** — toolbar at top, info bar at bottom

### Phase 2: Placement & Money
- [ ] Click-to-build: select item type, click to place on grid
- [ ] Building costs (from decompiled ParamT / code map)
- [ ] Money display, running balance
- [ ] Delete/demolish items
- [ ] Floor number display on left edge

### Phase 3: Time & People
- [ ] Day/night cycle (CLUT animation from AnimeT.c / seg 1020)
- [ ] Time progression: weekday cycle (Qtr1→Qtr2→Qtr3→Weekend)
- [ ] Population counter (CountT.c / seg 1060)
- [ ] People spawning and basic movement
- [ ] Tenant satisfaction (from JudgeT.c)

### Phase 4: Elevators (the meat!)
- [ ] Elevator shaft placement (vertical span selection)
- [ ] Car movement physics (from ElevatorsT.c / seg 1090)
- [ ] People queueing and riding (ElvPeple.c / seg 10a8)
- [ ] Elevator editor dialog (ElvDlogT.c / seg 1098)
- [ ] Express/service/standard types

### Phase 5: Star Rating
- [ ] Star progression system (LevelT.c / seg 1128)
- [ ] Level-up conditions (LevelUp.c — VIPs, facilities, population thresholds)
- [ ] Unlockable building types per star level
- [ ] Tower rating UI display

### Phase 6: Full Simulation
- [ ] All tenant types (office, condo, hotel, restaurant, shop, cinema, etc.)
- [ ] Hotel housekeeping (MainteT.c / seg 1130)
- [~] Fire/terrorist events (FireT.c, EventT.c) — visual presentation DONE 2026-06-13 (real flame anim + alert icons + banner + feed); interactive decision modal DONE 2026-06-15 (bomb: deploy security vs. pay star-scaled ransom; fire: info-only acknowledge — faithful to EventT/FireT decomp; pauses sim, full modal input capture). Aftermath DONE 2026-06-15 (firefighting chopper flies in + drops water on high-rise fires >=F8; fire AND bomb blasts leave rubble until rebuilt). Disaster arc complete bar sound.
- [ ] Sound effects (SoundT.c / seg 11c8)
- [ ] VIP visits
- [ ] Parking, metro, cathedral, medical
- [x] Flavor polish DONE 2026-06-15: medical emergencies (CheckMedicalEmergency — only with a medical center, no penalty, red-cross marker + feed); Santa once-a-year holiday flyby (no calendar months in the EXE, so year-end); fire-glow palette flash (warm screen tint scaled to blaze size); star-up certificate (gold card built from the real star sprites; no cert bitmap in original); multi-story grand-lobby wait-forgiveness bonus (WaitT: 2/3-story lobby forgives 25/50 ticks of wait stress)

## Key Files

| File | Purpose |
|------|---------|
| `src/ne_resource.{h,c}` | NE executable resource parser |
| `src/sprites.{h,c}` | Sprite atlas: load, catalog, render game sprites |
| `src/tower.{h,c}` | Tower grid data structure + cell management |
| `src/render.{h,c}` | SDL2 rendering: sky, tower, UI |
| `src/game.{h,c}` | Game state, time, money, simulation loop |
| `src/main.c` | Entry point, SDL init, event loop |

## Sprite ID Reference (from OpenSkyscraper analysis)

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
- `0x8568-0x8571` — Restaurant sprites (Jonah confirmed — NOT lobby!)
- `0x85a8-0x85ab` — Fast food sprites
- `0x85e8-0x85ee` — Cinema/party hall sprites
- `0x8628-0x8636` — Hotel room sprites
- `0x8668-0x8674` — Hotel suite sprites(?)
- `0x86a8-0x86a9` — Security sprites
- `0x86e8-0x86f1` — Parking sprites
- `0x87e8-0x87ed` — Metro station sprites
- `0x8828` — Cathedral
- `0x8868` — Medical center
- `0x88a8` — Recycling center (Jonah confirmed)
- `0x88e8-0x88ed` — Stairway sprites
- `0x8928-0x892e` — Recycling center sprites (Jonah corrected — NOT escalator)
- `0x8968-0x8969` — Housekeeping/maintenance
- `0x89a8-0x89a9` — Additional escalator
- `0x8f28` — Underground dirt tile
- `0x8f68-0x8f6d` — Foundation/structure sprites

### Raw bitmap resource IDs (type 0xFF02)
- `0x89e8-0x89ea` — People walking sprites (124 cells each!)
- `0x8F68-0x8F6B` — **Fire/large** flame animation (4 frames, 96x36 = 12-cell front; white-keyed)
- `0x8F6C` fire/small · `0x8F6D` fire/chopper · `0x8FA8` fire/destroyed (burnt cell)
- `0xA710` alert/terrorist (76x67) · `0xA714` alert/fire (76x60) — white-keyed alert icons
- Fire is an 8-frame cycle in the EXE (frame=b3de%4, +4 when extinguishing), 12 cells wide per front, drawn from the master tile-sheet; the port tiles the 4 standalone DIB frames across [fire_left,fire_right]
- Event dialogs (string res): fire start 0xBC2/0xBC3, caught 0xBCF, exploded 0xBD0; sounds 0x2714 bomb / 0x2716 fire alarm (deferred)
- `0x8a28-0x8a2a` — More people sprites
- `0x8a68-0x8a6a` — Even more people sprites
- `0x8fe9` — Large sprite sheet (552 cells)
- `0x8fea` — Largest sprite sheet (736 cells)

### Sound resource IDs (type 0xFF0A)
- 58 sounds total — elevators, doors, ambient, events, etc.

## Decompilation Reference

The decompiled code lives in `../simtower-decomp/`:
- `segments/seg_XXXX.c` — per-segment decompiled C
- `segment-mapping.md` — which segment = which module
- `analysis.json` — machine-readable mapping

Key segments for each phase:
- Phase 1: seg_1208 (DrawT), seg_1048 (draw utilities)
- Phase 2: seg_1188 (ParamT — costs/parameters), seg_1118 (tenant)
- Phase 3: seg_1020 (AnimeT — CLUT), seg_1060 (CountT), seg_11d8 (TimeT)
- Phase 4: seg_1090 (ElevatorsT), seg_10a8 (ElvPeple), seg_1098 (ElvDlogT)
- Phase 5: seg_1128 (LevelT/LevelUp)
