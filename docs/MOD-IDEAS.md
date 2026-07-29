# MOD IDEAS — deliberate departures from the original

Things we might ADD beyond faithful mechanics. Not bugs, not gaps:
the faithful port stays the default; mods are opt-in flavor on top.
(Keep the list; date the entries; note who wanted it.)

- **Excavation premium** (Jonah, 2026-07-29) — the original charges the
  same $500/cell for floor deck above and below ground (TerrainCost
  1178:0583, byte-verified; no basement branch exists). Feels wrong to
  modern sensibilities: digging should cost more than framing. Mod:
  scale the per-cell deck price below ground (e.g. 2-4x, maybe deeper =
  pricier). Implementation point: `tower_deck_price()` in src/tower.c —
  one function, already floor-aware.

- **Zoom in/out** (Jonah, 2026-07-29) — the original renders at exactly
  one scale (8px cells, 36px floors). Mod: camera zoom levels (integer
  scaling first — 2x out for overview, the minimap already proves the
  tower reads fine small). Touches grid_to_screen/screen_to_grid and
  every hit-test that assumes CELL_W/CELL_H; consider a global
  view-scale factor those helpers own.

- **Multi-speed simulation** (existing, flagged 2026-07-29) — the
  original's only speed control is the Options → Fast Mode toggle
  (removes the 6ms tick throttle; menu id 40007 → [0xDE34] → TimeT
  1200:01a5). Our 1x/2x/3x Speed menu is already a mod by that
  standard; keeping it.

- **Deliberate multi-tower gaps** (older note in tower.c) — the
  original forbids horizontal gaps in a floor (deck is one contiguous
  extent per floor). Mod: allow separate towers on one lot.
