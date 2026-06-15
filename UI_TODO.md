# UI Polish Pass — 2026-06-15 (live VNC review with Jonah)

## Done (committed)
- [x] Star rating: show all 5 slots (filled+empty) — faithful to DrawStarIndicators. `84ddbaa`
- [x] Build ghost + placement centered LEFT-RIGHT on cursor. `84ddbaa`
- [x] Group-button pulldown marker = solid ◢ bottom-right corner. `84ddbaa`

## Batch 1 — quick correctness fixes (in progress)
- [x] Camera: clamp so you can't scroll past the bottom of the map (added clamp_camera, vertical+horizontal). NEEDS build+verify.
- [ ] Cursor: also center the tile VERTICALLY on the pointer (only LR done).
- [ ] Cursor shape: should be an arrow normally, not an 'X' — only bulldozer/crosshair when demolishing. (The 'X' is the bare Xvfb root cursor; need to set SDL cursors per mode. Check what the binary uses.)
- [ ] FLR floor tool: places a 63-cell-wide slab; should be the minimum ATOMIC tile, drag to extend. (ITEM_WIDTH[ITEM_FLOOR] = 63 → fix to atomic; confirm true atomic width.)
- [ ] Overhangs shouldn't be possible — placement needs support below (no floating tenants/floors). Tighten tower_can_place.
- [ ] Lobby-on-lobby overlap: don't charge money for re-placing lobby over existing lobby (it's a no-op / only the new cells should cost, ideally nothing for pure overlap).

## Batch 2 — gating + modes
- [ ] Item gating: HIDE locked items (not grey); toolbox physically shrinks and grows with stars (decomp seg_1050: sub-items scale with star).
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

## Visual / sprites
- [ ] Lobby tileset mismatch: wrong endcap on the middle lobby segment (sprite set mismatch). Verify lobby endcap vs middle sprite mapping.

## Backlog / research
- [ ] Easter eggs (research original user guide — NOT in decomp): buried treasure, secret money-click spot, hidden grand lobby.
- [ ] **Mod menu / feature-flags system** — a menu to toggle intentional "Jonah-additions" (mods) separate from faithful behavior. Store mods as flags so the faithful base stays clean.
- [ ] **MOD: multi-tower gaps + bridges** — allow deliberate gaps in floors (separate towers). Tenants can't walk across gaps. Gaps improve views/light → adjacent units more desirable (also applies to units on normal edges). No accidental holes: a unit bridging a gap fills floor underneath (partial overhangs still disallowed). Later: bridges between towers, upgradeable to moving walkways favorable in routing.
- [ ] **MOD: above-ground parking** — allow parking above ground (if not already), cheaper than underground.

## NOW
- "Go with filling floor for now": floors auto-fill so there are no gaps between tenants on a row (no swiss-cheese); multi-tower gaps are the backlogged mod.
