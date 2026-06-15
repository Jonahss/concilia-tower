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
- [ ] Unlock schedule: decomp only confirms cinema=3★ (gating data lives in EXE resources, not C). → research online guides for the canonical table.

## Batch 3 — menu bar
- [ ] Enable the (coded but MENU_BAR_H=0) top menu bar. Shift top windows down to avoid overlap. Home for: Mode (Campaign/Sandbox), tuning params, analytics, speed, view.

## Visual / sprites
- [ ] Lobby tileset mismatch: wrong endcap on the middle lobby segment (sprite set mismatch). Verify lobby endcap vs middle sprite mapping.

## Backlog / research
- [ ] Easter eggs (research original user guide — NOT in decomp): buried treasure, secret money-click spot, hidden grand lobby.
