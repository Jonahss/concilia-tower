# Live worklist — remaining loose ends + mod backlog

> Started 2026-06-15 as the live VNC review pass with Jonah; grew into the
> project's running worklist. Everything checked `[x]` shipped with the commit
> noted. The open items live in "Faithful loose ends" and "Backlog / MODS".

## Done (committed)
- [x] Star rating: show all 5 slots (filled+empty) — faithful to DrawStarIndicators. `84ddbaa`
- [x] Build ghost + placement centered LEFT-RIGHT on cursor. `84ddbaa`
- [x] Group-button pulldown marker = solid ◢ bottom-right corner. `84ddbaa`
- [x] Camera: clamp so you can't scroll past the bottom/edges of the map. `3d16d5d`
- [x] Cursor: center tile VERTICALLY on the pointer (truncating-division bug). `3d16d5d`
- [x] Cursor shape: real arrow normally, crosshair for bulldozer (was bare Xvfb 'X'). `3d16d5d`
- [x] FLR floor tool: atomic tile, drag to extend (was 63-wide slab). `b54bc27`
- [x] Auto-fill empty floor between tenants on a row (no swiss-cheese). `b54bc27`
- [x] No overhangs — placement requires support below. `b54bc27`
- [x] Lobby-on-lobby overlap: free (no charge for pure re-place). `b54bc27`
- [x] Elevator placement: placeable on ANY built floor inside the tower (was over-restricted). `74b4099`
- [x] Black-screen incident: my fault (killed live game for a screenshot, relaunch died in sandbox). Resolved — new rule: render check screenshots OFF-SCREEN, never touch the live session.

## Batch 1c — live VNC bug wave (19:11–19:39)

### Placement / build validation (cluster — tower.c)
- [x] **Bulldozer deletes the build floor itself** → now keeps the floor. `3f7c631`
- [x] **Bulldozer killed the lobby** → lobby is permanent now. `3f7c631`
- [x] **Escalator placeable straight into dirt** → removed support fallback. `331d65e`
- [x] **Underground elevator** — confirmed: shafts are intentionally self-supporting (Jonah OK'd leaving them free); buildable within the tower (`74b4099`).
- [x] **Stairs & escalators shouldn't overlap** → full-overlap rejected, chains still ok. `331d65e`

### Sim correctness / reachability (cluster — people.c / tower.c)
- [x] ~~Stairs don't let people reach units~~ — Jonah retracted (misread; they do).
- [x] **Top restaurant full despite being unreachable** → capacity byte drains when unreachable. `4b9f662`
- [x] **Adding elevator cars doesn't work** → finger tool + double-click dialog (`c4f4fdc`) AND faithful elevator-tool-on-shaft car add (`19b04c2`).
- [x] **Condos showing occupancy?** — resolved by the condo sprite-state fix (`9506891`): renders occupied day/eve/night vs For-Sale; population counted. Jonah verifying on VNC.

### Tools
- [x] **Pointer/finger tool** wired as interact (opens elevator dialog). `c4f4fdc`
- [x] **Inspector tool** — click a unit → info popup (floor, status, occupancy, income, satisfaction, tier). `1d4eccc`
- [x] **Drag-to-extend elevator shaft** — works: elevator tool drag extends both directions (drag_place_units). Jonah verifying feel on VNC.

### Animation
- [x] **Stairs/escalators animate** when carrying people (7/14-frame stairs, 8-frame escalator). `0d9f07c` `14f0f39`
- [x] **Recycling trash cycle + collection truck** animate. `6f386e2` `14f0f39`
- [x] **Construction-worker build animation** — workers animate on units under construction (0x85EA, was loaded-but-unused). `1a80ed9`

### Menu / toolbar / HUD
- [x] **Sub-menu selection updates the selected-tile sprite** in the group button. `b17fd4a`
- [x] **Single wide play/pause toggle** (was two buttons) — matches Jonah's reference shot. `bd79ebb`
- [x] **Top menu bar** — DONE in Batch 3 (A) `8816b9c` (MENU_BAR_H=18 + the missing render calls). Visible and functional.

### Minimap
- [x] **Minimap legend obscures bottom floors** → moved to top (sky). `b17fd4a`
- [x] **Minimap hotel dirty rooms but clean main sprite** → hotel rooms now render by state (dirty frame shows). `1e268da`

### Visual / sprites
- [x] **Lobby endcap** → now tiles cap+body segments like the original (OS loadLobbies). `5ab8401`
- [x] **Office frames** → time-of-day + windowed variant (was capacity ramp). `14fdb7c`. OPEN: window-vs-no-window variant rule (a/b/c asked Jonah).

## Batch 2 — gating + modes — DONE
- [x] Item gating: HIDE locked items + toolbox compacts/grows with stars. `ab300d8`
- [x] Campaign mode (default): star-gated, starts on empty lot (no lobby). `c5bbbe1`
- [x] Sandbox mode: everything unlocked. F8 toggles in game. `5aa155a` `c5bbbe1`
- ITEM_STAR_REQ[] table in tower.h; item_unlocked(); MODE_CAMPAIGN/SANDBOX in GameSim.mode.

### Unlock schedule — CONFIRMED (kiwizoid GameFAQs + simtower.fandom, two primary sources agree)
- 1★: Lobby, Floor, Stairs, Standard Elevator, Office, Condo, Fast Food
- 2★: Service Elevator, Hotel Single, Security, Housekeeping
- 3★: Express Elevator, Escalator, Hotel Twin, Hotel Suite, Restaurant, Retail Shop, Cinema, Party Hall, Parking (space + ramp), Recycling, Medical
- 4★: Metro / Subway Station
- 5★: Cathedral
(PC v1.0 data. Promotion gates: 2→3 needs a Security office; 3→4 needs ≥2 suites+VIP+recycling+medical demand; 4→5 needs a Metro; 5→TOWER needs Cathedral+wedding.)

## Batch 3 (A) — menu bar — DONE
- [x] Enabled the top menu bar (MENU_BAR_H=18 + render calls were missing too). Windows shifted below. Game/Build×4/Speed/View menus. Mode toggle -> Game menu (radio). `8816b9c`
- [x] Grey out locked Build-menu items in Campaign — DONE (render_dropdown draws locked items in WIN31_DISABLED grey; execute_menu_item rejects their clicks with "Locked — raise your star rating first"). Confirmed 2026-07-18.

## Faithful loose ends — 2026-07-18 pass
- [x] **>15-floor elevator edit-grid scrollbar** — wheel scroll + click-to-position + a thumb (main.c); verified at the real surface on BARKLE's ~50-floor express shaft.
- [x] **Post-catch clock jump to 4PM** — was only wired for a *caught* bomb; a *detonated* bomb now also jumps to 4PM (unconditional), and fire jumps *only if before 4PM* (the EXE's conditional guard). Verified with a sound/clock harness.
- [x] **Event sounds** — wired the real triggers (referee-corrected labels): bomb-threat `0x2713` at the offer, arm `0x2710` on deploy, **explosion `0x2714` once at detonation**, ransom-paid `0x271F`, **fire-outbreak `0x2716` once at ignition**, fire-crackle `0x2719` looping while burning. Verified.
- [x] **`0x87EC` red floor-plate** — the shaft floor number lights red when a car is on that floor (IsCarOnFloor). Verified at the real surface (forced via `ELV_REDTEST` for capture).
- [ ] **24px elevator mode-icon art** — still vector glyphs (scan/shuttle/hold). The real icons are a boot-loaded GDI bitmap, not a standard NE resource, so extraction is unresolved. Low priority; needs a referee pass to locate the GDI blob.
- [ ] **Shop closed / for-rent sprite frames** (referee 2026-07-18) — restaurants/FF already draw the state-byte closed frame (`variant*4+3` via `retail_open`); SHOPS still don't. Faithful fix: load the shop sheet's shared **closed frame 0x22** and **for-rent 0x21**, and pick shop frame by state (`0xFF→0x21`, closed→`0x22`, else `variant*3+state`) instead of the capacity bucket. Needs the two shared frames loaded (the port's per-variant 3-frame sheets omit them). See docs/ORIGINAL-BUGS.md "Resolved".
- [ ] **Two defined-but-unwired sounds** (audit 2026-07-21): `SND_GARBAGE`
  (#2280, garbage-truck collection) and `SND_GUARD_STEP` (#10014, bomb-guard
  footsteps) exist in `audio_events.h` but nothing triggers them yet.
- [ ] **Cinema soundtrack sub-index mapping** — the 9xxx movie themes play,
  but which theme belongs to which film is a guess (`movie_id % 13`); the
  real EXE mapping is undecoded. Needs a referee pass.
- [ ] **NE string-table reader** — event dialog/feed text is hardcoded to
  match the original wording; the real strings (res `0xBC2`–`0xBD0` etc.)
  sit unread in the EXE. A small string-table loader would make the text
  byte-faithful and localizable for free.

## Backlog / research / MODS
- [ ] **MOD: merge F3 graphs into F7 financial report** — the over-time trend graphs (F3 Analytics: Population/Commuters, Income/Expenses, Value built/lost — a port invention) could be surfaced inside the faithful 1994 financial-report dialog (F7, art 0x81f4). Keep them optional/mod-gated since the original report has no graph. Jonah asked to keep both surfaces for now; merge later. (2026-07-16)
- [x] Easter eggs (research original user guide — NOT in decomp): the money-click spot and grand lobby shipped 2026-07-29 (893f9b2) — and per Jonah 2026-07-30 they're documented in the shipped README guide, not secrets (see docs/ORIGINAL-BUGS.md Part 4). "Buried treasure" = the hidden debug menu's 9001 Treasure item (Part 3).
- [ ] **Mod menu / feature-flags system** — toggle intentional "Jonah-additions" (mods) separate from faithful behavior. First mod candidate: the floor auto-fill behavior (store as an intentional Jonah-addition flag). Faithful base stays clean.
- [ ] **MOD: persist tenancy Length across save/load** — the tenant-info "Length" field resets to zero on load, because the original .TDT record (18 bytes) has no field for it and the EXE keeps it in-memory only. That's faithful (see docs/ORIGINAL-BUGS.md #1) — so persisting `let_quarters` is a port nicety, mod-gated. Needs a side-channel since .TDT can't hold it (port-native save extension or sidecar). (2026-07-18)
- [ ] **MOD: multi-tower gaps + bridges** — deliberate gaps in floors (separate towers). Tenants can't cross gaps. Gaps improve views/light → adjacent + edge units more desirable. No accidental holes (a bridging unit fills floor underneath; partial overhangs still disallowed). Later: bridges → upgradeable moving walkways favorable in routing.
- [ ] **MOD: above-ground parking** — allow parking above ground (if not already), cheaper than underground.
- [ ] **MOD: window washing** — install window-washing cranes on the roof; pay washers a quarterly fee; cranes animate up/down washing the tower's faces. Pairs with the views/windows/light-wells/bridges mod family (dirty windows → lower desirability?).

## Sprite-state decode — COMPLETE (Jonah decoded sheets, I wired by state)
- [x] HOTEL: door + clean/occupied/dirty/roaches × day-night. `1e268da`
- [x] OFFICE: day/night × window-variant-by-tier (cap_peak). `14fdb7c` `03633b4`
- [x] CONDO: occupied day/eve/night + For-Sale day/night (5 frames). `9506891`
- [x] MEDICAL / CINEMA / PARTY HALL / PARKING by state. `3273601`
- [x] RECYCLING 5-frame trash cycle + truck; METRO night frame. `6f386e2` `14f0f39`
- [x] STAIRS (both variants) + ESCALATOR animate. `0d9f07c` `14f0f39`
- [x] RESTAURANT/FAST FOOD (empty/busy/packed/closed) + SHOP (3 fill frames): confirmed already-correct occupancy ramps.
- Audit DONE. Only open sprite item: optional no-window office frames usage (tier mapping shipped; revisit if Jonah wants perimeter-based).

## NOW (2026-07-21)
- All batches through the 2026-07-18 faithful-loose-ends pass are shipped.
- Open: the loose ends above (shop closed frames first — smallest, fully
  referee'd), then the mods backlog as Jonah picks. Full-project status
  lives in the [README](README.md#status).
