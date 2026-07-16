# UI Polish Pass — 2026-06-15 (live VNC review with Jonah)

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
- [ ] **Top menu bar still not visible** — confirm not just a VNC crop; tie into Batch 3 (MENU_BAR_H=0).

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
- [ ] Minor: grey out locked Build-menu items in Campaign (placement already blocks them).

## Backlog / research / MODS
- [ ] **MOD: merge F3 graphs into F7 financial report** — the over-time trend graphs (F3 Analytics: Population/Commuters, Income/Expenses, Value built/lost — a port invention) could be surfaced inside the faithful 1994 financial-report dialog (F7, art 0x81f4). Keep them optional/mod-gated since the original report has no graph. Jonah asked to keep both surfaces for now; merge later. (2026-07-16)
- [ ] Easter eggs (research original user guide — NOT in decomp): buried treasure, secret money-click spot, hidden grand lobby.
- [ ] **Mod menu / feature-flags system** — toggle intentional "Jonah-additions" (mods) separate from faithful behavior. First mod candidate: the floor auto-fill behavior (store as an intentional Jonah-addition flag). Faithful base stays clean.
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

## NOW
- Posted status table to Jonah. Cranking Batch 1c top-down, starting with the placement/build-validation cluster (bulldozer over-greedy is the most destructive bug) + the stairs-reachability cluster (most impactful for gameplay).
