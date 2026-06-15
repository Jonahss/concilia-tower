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

## Batch 1c — live VNC bug wave (19:11–19:39) — QUEUED, priority order

### Placement / build validation (cluster — tower.c)
- [ ] **Bulldozer deletes the build floor itself** — should delete the TENANT but leave the build floor. Currently nukes the whole floor.
- [ ] **Bulldozer killed the lobby** too (same root cause — bulldozer too greedy).
- [ ] **Escalator placeable straight into dirt** — needs a built floor under it.
- [ ] **Underground elevator** — likely the real culprit behind the "into dirt" issue; elevators below ground bypass floor-support check. Audit.
- [ ] **Stairs & escalators shouldn't overlap** each other.

### Sim correctness / reachability (cluster — people.c / tower.c)
- [ ] **Stairs don't let people reach units** — reachability network not honoring stairs.
- [ ] **Top restaurant full despite being unreachable** — occupancy granted without a valid transport path. (Same root as stairs reachability? verify.)
- [ ] **Adding elevator cars doesn't work** — car-add action no-ops.
- [ ] **Condos showing occupancy?** — verify condos report occupancy at all.

### Tools not implemented
- [ ] **Inspection tool** — appears unimplemented.
- [ ] **Pointer tool** — unimplemented/inaccessible → can't drag elevator shafts up/down. (Needed to extend shafts.)

### Animation
- [ ] **Stairs need people/movement animations.**
- [ ] **Construction-worker build animation** — does it exist at all? Investigate, then wire or note as absent.

### Menu / toolbar / HUD
- [ ] **Sub-menu selection doesn't update the selected-tile sprite** in the toolbox button.
- [ ] **Menu should match real gameplay screenshot** (Jonah sent one): single play/pause button, correct button sizing.
- [ ] **Top menu bar still not visible** — confirm it's not just a VNC crop; tie into Batch 3 (MENU_BAR_H=0).

### Minimap
- [ ] **Minimap legend obscures bottom floors** — reposition legend.
- [ ] **Minimap hotel shows dirty rooms but the room sprite stays clean** — dirty state mismatch minimap vs main render.

### Visual / sprites
- [ ] Lobby tileset mismatch: wrong endcap on the middle lobby segment. Verify endcap vs middle sprite mapping.

## Batch 2 — gating + modes
- [ ] Item gating: HIDE locked items (not grey); toolbox physically shrinks/grows with stars (decomp seg_1050).
- [ ] Campaign mode (default): star-gated. **Starts with NO lobby** (empty lot — Jonah confirmed).
- [ ] Sandbox/Editor mode: everything unlocked.

### Unlock schedule — CONFIRMED (kiwizoid GameFAQs + simtower.fandom, two primary sources agree)
- 1★: Lobby, Floor, Stairs, Standard Elevator, Office, Condo, Fast Food
- 2★: Service Elevator, Hotel Single, Security, Housekeeping
- 3★: Express Elevator, Escalator, Hotel Twin, Hotel Suite, Restaurant, Retail Shop, Cinema, Party Hall, Parking (space + ramp), Recycling, Medical
- 4★: Metro / Subway Station
- 5★: Cathedral
(PC v1.0 data. Promotion gates: 2→3 needs a Security office; 3→4 needs ≥2 suites+VIP+recycling+medical demand; 4→5 needs a Metro; 5→TOWER needs Cathedral+wedding.)

## Batch 3 — menu bar
- [ ] Enable the (coded but MENU_BAR_H=0) top menu bar. Shift top windows down to avoid overlap. Home for: Mode (Campaign/Sandbox), tuning params, analytics, speed, view.

## Backlog / research / MODS
- [ ] Easter eggs (research original user guide — NOT in decomp): buried treasure, secret money-click spot, hidden grand lobby.
- [ ] **Mod menu / feature-flags system** — toggle intentional "Jonah-additions" (mods) separate from faithful behavior. First mod candidate: the floor auto-fill behavior (store as an intentional Jonah-addition flag). Faithful base stays clean.
- [ ] **MOD: multi-tower gaps + bridges** — deliberate gaps in floors (separate towers). Tenants can't cross gaps. Gaps improve views/light → adjacent + edge units more desirable. No accidental holes (a bridging unit fills floor underneath; partial overhangs still disallowed). Later: bridges → upgradeable moving walkways favorable in routing.
- [ ] **MOD: above-ground parking** — allow parking above ground (if not already), cheaper than underground.

## NOW
- Posted status table to Jonah. Cranking Batch 1c top-down, starting with the placement/build-validation cluster (bulldozer over-greedy is the most destructive bug) + the stairs-reachability cluster (most impactful for gameplay).
