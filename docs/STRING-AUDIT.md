# STRING AUDIT — port coverage vs. SIMTOWER.EXE resource strings

**Date:** 2026-07-28
**Method:** Every user-visible string in `docs/exe-strings-7f06.txt` (resource
type 0x7f06, extracted from SIMTOWER.EXE v1.1) was treated as evidence of a
mechanic and checked against the port sources (`src/tower.c`, `src/game.c`,
`src/people.c`, `src/main.c`, `src/twr.c`, headers). Where a string's meaning
was unclear, the decomp annotations
(`simtower-decomp/annotated/`, read-only) were consulted. Statuses:

- **IMPLEMENTED** — the rule behind the string exists in the port (file:function cited).
- **PARTIAL** — some of the rule exists; the missing part is named.
- **MISSING** — the rule (or the string) has no port behavior behind it.
- **DELIBERATE** — the port knowingly diverges, with a code-comment citation.
- **N/A** — Mac/System-7/file-dialog boilerplate with no game rule behind it.

Throughout, **"string missing" (cosmetic)** is distinguished from **"RULE
missing" (mechanic)**. (The original cross-cutting gap — rejection messages
going only to stdout — was closed 2026-07-29, commit 8edcb78: placement
errors now land in the in-game event feed.)

---

## res 0x0190 — Transport type names

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Express / Standard / Service Elevator | elevator subtypes with distinct width/capacity/price/rules | IMPLEMENTED | `tower.h` ITEM_ELEVATOR_{SHAFT,SERVICE,EXPRESS}; names used in the elevator-dialog title `main.c:2256` and toolbar |
| Escalator, Stairs | the two walk transports | IMPLEMENTED | `tower.h`, overlay placement in `tower.c:tower_place` |

## res 0x01a4 — Movie titles

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| 15 film titles | cinema film identity: ids 0-6 ordinary, 7-13 hits; draw decays with age | IMPLEMENTED (14 titles CORRECT) | `main.c:4717 MOVIE_TITLES[14]` + `game.c:game_change_movie`/`movie_patron_quota`. **The port carries 14 of the 15 titles — "Under the Apple Tree" was dropped** (movie_id range is 0..13, so either the port's id space is one short or the 15th title is genuinely unused in the EXE; worth a decomp check). Cosmetic. |

## res 0x02bc / 0x02bd — Person names & activities (PRIORITY)

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Outside, VIP, Man, Salesman, Woman, Child, Security, Woman with Kid, Homebody, Housekeeper, Janitor | person TYPES — clicking a person shows who they are (InfoPeple seg_1110) | IMPLEMENTED (2026-07-29) | The label is DERIVED, never stored (classifier 1100:3856 on home-tenant type × member index — office member 0/1 = Salesman, condo 1 = Mother with Baby, visitors on n&7) — ported as `person_kind()` in `main.c`, with the two code quirks kept ("Homebody"→"Mother with Baby" always; Housekeeper→"Janitor" evenings). Inspector-tool hit-test on queue figures (exact cell, like 10a8:0aae), popup with type/home/whereabouts/stress lines, 20-slot naming registry (`tower_person_names`) with the EXE's purge schedule. `Person.member` added at spawn. |
| " for sales calls", " to leave", " to eat", " to go home" | person ACTIVITY suffixes in the same popup | IMPLEMENTED (2026-07-29, port-state mapping) | `person_where_line()` maps the port's trip states to the seg_1110 status table ("to go home" with the Lobby/Parking Space split by `parked_cat`, venue name + floor for visitors, "Housekeeping, floor N" for maids on the job). The EXE's status 0x40 office midday errand ("Lobby for sales calls") isn't modeled in the port sim, so that line can't occur yet — flagged in code. |

Note: "VIP" as a mechanic **is** modeled (visit every day%9==3 at 3★+, car-gated,
satisfaction gates 3→4 — `game.c:1395-1436`), but as an abstract event, not a
person entity you can find and click. "Salesman" office visitors are not a
distinct spawn class.

## res 0x02c6 — Item type names

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Floor…Parking Ramp, Burned Area | item vocabulary | IMPLEMENTED | `tower.c:tower_item_name`; Burned Area = `Tenant.burned` rubble rendering (`main.c` render pass) |
| SECOM | a placeable SECOM Center | N/A — CUT CONTENT (verified 2026-07-29) | No `ITEM_SECOM` in the enum; `.TDT` import maps file type 17 to ITEM_NONE (`twr.c:80`). Decomp `globals.md:303` marks TENANT_SECOM "Cut feature?" — so this may be dead in the Windows EXE too, but it has a price ($100,000, res 0x03f1) and a singleton rule (res 0x03eb), so *something* consumes it. Low priority; needs a decomp verdict before implementing. |

## res 0x02c7 — Tenant info-dialog comments (PRIORITY)

Producer: `game.c:game_tenant_comments` (max 3 lines, EXE priority order).

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| No transportation connected | unit has no route to ground | IMPLEMENTED | `game.c:1769` via `people_nearest_transport` |
| Neighbors are too noisy | generic noise complaint | PARTIAL | the *specific* form "\<type\> neighbor is noisy" is implemented (`game.c:1858`, noisy-neighbor matrix `noisy_neighbor_scan` w/ 10/20/30 ranges); the generic string is unused. Cosmetic. |
| Housekeeping needed | dirty room awaiting a maid | PARTIAL | RULE fully implemented (dirty rooms unrentable, maid passes — `game.c:update_housekeeping`; inspector Status shows "Dirty" `main.c:4806`); this comment LINE is not emitted. Cosmetic. |
| It's a Sellout! / Ticket sales are average / Terrible sales! Change the movie! / New movie showing! | cinema draw tiers by film age | IMPLEMENTED | `game.c:1780-1789` |
| Business is very good/good/average / Very few customers | retail customer tiers (50/35/25) | IMPLEMENTED | `game.c:1791-1799` |
| Elevator/Escalator/Stairs (very) far away | horizontal distance ≥80/≥125 cells to nearest transport | IMPLEMENTED | `game.c:1770-1777`, thresholds match TUNING penalty_walk_80/125 |
| Medical Center is too far away | office/condo at 3★+ w/o adequate medical | IMPLEMENTED (flagged approx) | `game.c:1813` — tower-wide `medical_adequate` instead of per-15-floor band; the band logic itself exists in `game_medical_seek`. Documented approximation. |
| Office worker needs parking | office parking shortage comment | N/A — DEAD STRING (no producer in the EXE; verified 2026-07-29) | Parking mechanics exist (per-category car quotas, `people_parking_assign`, VIP gate) but no office ever complains about parking. String + the per-office trigger missing. |
| Not connected to Ramp | dead parking space | IMPLEMENTED | `game.c:1818`, `space_usable` from `game_parking_recompute` |
| **Too far from Lobby or Skylobby** | vertical distance from a lobby floor stressing a unit | **MISSING** | No port comment or mechanic keys on floors-from-nearest-lobby. (Wait-stress goes through the elevator sim instead.) String and its specific trigger both missing. |
| Weekends attract more customers / Rain might cause fewer customers | period commentary | IMPLEMENTED | `game.c:1800-1806`, `game_retail_period` |
| Opens tomorrow | food venue under construction | IMPLEMENTED | `game.c:1842` |
| Transportation access is good | the positive access line | N/A — DEAD STRING (no producer in the EXE; verified 2026-07-29) | trivially derivable from the same scan (dist < 80); never emitted. Cosmetic. |
| Room is too dirty | infested room | IMPLEMENTED | `game.c:1757` |
| Conditions are terrible | office/condo judged stressed | IMPLEMENTED | `game.c:1752` |
| A Party is happening! | party hall mid-event | IMPLEMENTED | `game.c:1846` |
| Full of Garbage | recycling can't keep up | IMPLEMENTED | `game.c:1853`, pop/center ≥ 2500 |
| Last train is gone / First train is not coming / Crowded with passengers | metro-station comments | IMPLEMENTED (2026-07-29) — `game.c:2063-2069`, last train 9PM, crowded = train at platform | The schedule itself exists (`game.c:2597` train toggle 10AM-5PM + SND_METRO; metro visitors 10AM-5PM `people.c:1364`). The comment in `game.c:1856` claims these are "already surfaced via the event feed" — **that claim is false**: no train message exists in `main.c`'s feed. Strings missing; the before-10AM/after-5PM/crowded triggers missing. |
| No renters | vacant shop | IMPLEMENTED | `game.c:1793` |
| Car for … | usable space names its car's owner | IMPLEMENTED | `game.c:1825-1837` (`parking_front_owner`) |
| " neighbor" + " is noisy" | named noisy neighbor | IMPLEMENTED | `game.c:1858-1866` |

Also documented-omitted (not faked): sub-lobby zone comment #21 and the EXE's
parked-car producer variant — see the header comment at `game.c:1698`.

## res 0x02c8 — Status words (PRIORITY)

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| ", Floor " | popup title floor suffix | IMPLEMENTED | `main.c:inspect_title` ("- NF"/"- BN"/"- Lobby") |
| Occupied | occupancy status | IMPLEMENTED | `main.c:4796,4807` |
| For Rent | vacant office/shop status word | IMPLEMENTED (2026-07-29, commit 4d20dd3) | The vacancy state is fully modeled (TENANT_ABANDONED + re-let market, `game_judge_daily`/`game_relet_arrivals`) and vacant art renders, but the inspector never shows a "For Rent" status row for offices/shops. Cosmetic. |
| For Sale | unsold condo | IMPLEMENTED | `main.c:4796`; For Sale spawn art (known-current) |
| Clean / Dirty | hotel room condition | IMPLEMENTED | `main.c:4806`, RoomCondition |

## res 0x02c9 — Duration formatting

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Year / Over 30 years / Over 1 year | tenancy & showing lengths | IMPLEMENTED | `main.c:inspect_length_str` ("N Year M Q", "Over 30 years"), `inspect_showing_str` ("Over 1 year") |
| st/nd/rd/th | ordinal suffixes | PARTIAL | port formats "N Year M Q" instead of "1st/2nd…" — cosmetic formatting divergence |

## res 0x02ca / 0x02cb / 0x02cc — Retail variant names (PRIORITY)

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| English Pub … Steak House (5 restaurants); Japanese Soba … Coffee Shop (5 fast foods); Men's Clothing … Sports Gear (11 shops) | each retail unit is a named VARIANT (art + name) | IMPLEMENTED (2026-07-29) — `main.c:retail_variant_name` feeds the inspector title and person-popup whereabouts | The variant **identity and art** are implemented: `twr.c:twr_variant_count` (5/5/11 — matching the string counts exactly) and `twr_tenant_variant` (import byte or id-stable), used by the renderer. **The names are nowhere in the port** — the inspector title (`main.c:inspect_title`) shows the generic type name ("Restaurant"). String-side gap; the mechanic (stable per-unit variant) exists. |

## res 0x02cd — "People on Floor N need path to Floor M" (PRIORITY)

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| People on Floor … need path to Floor … | a specific floor-pair connectivity warning (fires when a route is severed / a needed link is absent, e.g. housekeeping to a hotel floor) | PARTIAL | The port has the two-network reachability model (`game.c:game_update_reachability`, public + service) and a **generic** feed warning "%d units cannot be reached!" (`main.c:7411`). What's missing is the floor-pair diagnostic — which floors need a path to which — the actionable half of the warning. |

## res 0x03e8 / 0x03ea — System requirements & file errors

All N/A: System-7/memory/256-color requirements and Mac Finder/file-dialog
errors. The port's native save prints "Save FAILED!"/"Load failed" to the feed
(`main.c:5684-5697`), which covers the useful residue.

## res 0x03e9 — closing / quitting / Save the tower as / New Tower

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| closing ×3, quitting, Save the tower as | save-on-close prompts, save-as dialog | N/A (boilerplate) | port quits without prompting; note there is **no save-on-quit guard** — quitting with unsaved progress is silent. Minor UX gap, not a sim rule. |
| New Tower | start-over command | IMPLEMENTED (2026-07-29, commit 4d20dd3) | Game menu (`main.c:GAME_MENU`) has Save/Load/Export but no New Tower; restart requires relaunching. |

## res 0x03eb — Placement errors (PRIORITY, the richest)

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Items cannot be wider than Lobby | floor-1 span bounded by the ground lobby | IMPLEMENTED (equivalent) | subsumed by the full-support-below rule, `tower.c:358-373` |
| Cannot place item there / Cannot place any items there | generic invalid spot | IMPLEMENTED | `tower_can_place` reject paths (console-only messages) |
| Maximum height has been reached | build ceiling F100 / B10 | IMPLEMENTED | `tower.c:82-83`, TOWER_MAX_FLOOR/TOWER_MIN_FLOOR |
| Cannot place items wider than floor below | no overhang | IMPLEMENTED | `tower.c:358-373` (known-current) |
| Not enough money for construction | funds gate on items | IMPLEMENTED | `tower.c:86-90` |
| **Not enough money to build floor** | floor-DECK cost as a distinct charge | IMPLEMENTED (2026-07-29, byte-verified) | TerrainCost 1178:0583 fully decoded: $500/cell for cells outside each floor's contiguous extent, **identical above and below ground** (no excavation premium); ground-lobby band $5,000×height/cell; `ItemCost(0)=0` so the floor tool's whole charge IS the deck. Ported as `tower_deck_extend_cost`/`tower_extend_deck` (`tower.c`): every placement pays item + extent-growth deck (gap-fill and overhang cells included), elevator extension segments pay deck only (pure TerrainCost — and no longer a bogus $200k/floor), two-stage refusal msgs #7 construction / #8 build-floor (CanAffordBuild 1178:009e). Lobby charging is per-cell (ground = terrain-only at $5,000×H; converting existing deck to lobby is free, as in the EXE). |
| Cannot place on top of other items | overlap rejection | IMPLEMENTED | `tower.c:264-292` |
| Item not available underground / Item unavailable above ground | vertical zoning | IMPLEMENTED | `tower.c:225-227`, ITEM_UNDERGROUND_ONLY both directions |
| First floor is only for Lobby | ground floor lobby-only (transports exempt) | IMPLEMENTED | `tower.c:142-145` (known-current) |
| Lobbys are only every 15 floors | sky-lobby cadence | IMPLEMENTED | `tower.c:96-99` (known-current) |
| Cannot place items under Metro | nothing may be built below the metro floor | IMPLEMENTED (2026-07-29, f8219e9) | decomp `seg_11f8_tenant.c:553` (msg 0xe). The port has no metro-floor gate anywhere — shafts and rooms can be built beneath the station. |
| Place Metro station on bottom floor | station bottoms in virgin ground below the dig | IMPLEMENTED (2026-07-29, f8219e9) | port only requires underground + ceiling-above support (`tower.c:374-385`); any basement floor accepts it. |
| Cathedral is available only on 100th floor | cathedral pinned to the build ceiling | IMPLEMENTED (2026-07-29, f8219e9) | port gates it at 5★ (`ITEM_STAR_REQ`) but allows any supported floor — the demo even places one at F19 (`tower.c:834`). Real saves keep it at file floors 109-113 (`tower.h:21-23`), consistent with the 100th-floor rule. |
| Only one Metro Station allowed | metro singleton | IMPLEMENTED (2026-07-29, 8edcb78/f8219e9) | decomp `seg_11f8_tenant.c:576`: SINGLETON on [0xB3E8], msg 0x11. `tower_can_place` has no count check — unlimited metros placeable. |
| Only one SECOM allowed | SECOM singleton | N/A — cut content, no producer (2026-07-29) | SECOM item doesn't exist at all (see 0x02c6). |
| **Only one Cathedral allowed** | cathedral singleton | **MISSING** | decomp `seg_11f8_tenant.c:581`: SINGLETON on [0xB3EC], msg 0x13. No count check in the port. |
| Cannot destroy this item | some items are indestructible | PARTIAL | ground lobby protected (`tower.c:584`); whether the EXE protects more (metro? cathedral under wedding?) unverified. |
| Item requires more space on both sides | 8-cell shaft clearance | IMPLEMENTED | `tower.c:176-204` (known-current; decomp CheckElevatorClearance 10a0:10e8) |
| Cannot place over other transportation items | shaft/stairs mutual exclusion | IMPLEMENTED | `tower.c:197-203, 293-312` (known-current) |
| No more cars in this shaft | 8-car cap per shaft | IMPLEMENTED | `main.c:elev_add_car_at` (5312) + `people.h CARS_PER_SHAFT`; decomp CanAddCar err 0x18 |
| **No more elevator shafts available** | hard cap: 24 elevator groups, rejected at build | **MISSING (and a silent bug)** | decomp `seg_11f8_tenant.c:527-530`: new-shaft slot scan "max 0x18 = 24 groups; none free → reject". The port caps `MAX_SHAFTS 24` only inside the people sim (`people.c:people_rebuild_transport` stops collecting at 24) — a **25th shaft is placeable and takes money but silently never gets cars/queues**. Worse than a missing message. |
| **No more escalators available / No more stairs available** | fixed 64-record tables for stairs and escalators | **MISSING** | decomp `seg_10c0_StairsT.c:8,14`: `stairs_data[64]` (matches FileT's 64 entries); clearance code iterates "the 64 stair/escalator records" (`seg_11f8_tenant.c:538`). Port stores them as ordinary tenants, unbounded. |
| **Escalators available only at commercial spaces** | escalator floors must hold commercial tenants | **MISSING** | port's only gate is "content on both floors" (`tower.c:325-356`) — an escalator between condo floors is accepted. (Decomp gate not yet located — StairsT's placement fn is unannotated — but the string, OpenSkyscraper, and every player guide agree the rule exists.) |
| Cannot place stairs here | generic stair rejection | IMPLEMENTED | same both-floors-built gate |
| Item no longer available | availability gating | IMPLEMENTED (approx) | star-gated unlocks `main.c:item_unlocked` (3711); the "no longer" (one-shot items going away) is the singleton family above |
| Parking Ramps must connect to the 1st floor / …connected vertically / …placed on this level | ramp chain rules; space needs same-floor ramp | IMPLEMENTED | `tower.c:236-262` build gates + `game.c:game_parking_recompute` B1-anchored same-x chain (known-current, byte-verified 2026-07-11) |
| **Cannot destroy items under construction** | bulldozer refuses mid-construction units | **MISSING** | `tower.c:tower_remove` has no construction-state check; demolishing a TENANT_CONSTRUCTION unit is allowed. |
| Elevator shaft can cover only 30 floors | non-express span clamp | IMPLEMENTED | `tower.c:206-221` (known-current; decomp msg 0x23) |

Unlisted decomp-found cap in the same family: **venue records cap at 16**
(cinemas+party halls, `seg_11f8_tenant.c:575`, [0xB400] < 16 else msg 0x1E).
Port is unbounded. MISSING (minor).

## res 0x03ec — Settings modal

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Cannot change settings - Click "Resume" | elevator settings editable only in the paused edit surface | IMPLEMENTED (equivalent) | `main.c:elv_edit_mode` Simulate/Resume flow (2126, 2173-2199) gates grid edits |
| Not enough memory to simulate | — | N/A | |

## res 0x03ed — Stop-toggle / removal confirmations (PRIORITY)

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| **"If you change this setting, you will lose a key route to this floor…" / "If the service elevator doesn't stop on this floor, housekeeping cannot reach it…" / "This floor is part of the route to the Lobby…" / "If you remove this item, you will lose a key route…"** | the game detects that a stop-toggle or a transport removal would sever a floor's route (incl. the housekeeping/service network) and asks for confirmation | **MISSING** | `people.c:people_set_serviced` (537) toggles instantly and flushes queues; `main.c` (2204, 2675) calls it with no dialog; `tower_remove` demolishes transports with no route check. The DETECTION machinery exists (`game_update_reachability` computes both networks, and the feed reports strandings *after the fact*) — what's missing is the **pre-action check + confirm dialog**. This is a real guardrail players relied on. |
| That name is too long. Names can have up to 15 characters. | 15-char rename cap | IMPLEMENTED (structurally) | `main.c` name editor buffer `name_edit_buf[16]` (329), `Tenant.name[16]` "faithful to the rename cap" (`tower.h:373`) — overflow is impossible rather than warned. |
| You may only name 20 people. | 20-person naming cap | IMPLEMENTED (2026-07-29) | 20-slot registry `tower_person_names` keyed on (home tenant, member index) — stable across the daily commuter respawn, unlike the EXE's raw people-array key. 15-char names, exact cap string, purges: hotel-guest names at 4PM, visitor names at the day boundary, dead-tenant names always. `.TDT` person-name import stays parse-past-only: the port respawns people rather than reconstructing the original people array, so the file's people-index keys have nothing to bind to. |
| You may only name 20 tenants. | 20-tenant naming cap | DELIBERATE (divergence) | port stores a name on every tenant (`Tenant.name`), unlimited; the EXE's 20-slot side list survives only as `.TDT` round-trip storage (`tower.h twr_names[20]`). **Caveat:** exporting a port tower with >20 named tenants can't carry them all in the file format — worth an export-time warning. |

## res 0x03ef — Income categories

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Income from Office/Hotel/Condo sale/Restaurant/Fast Food/Retail Shop/Movie Theater/Party Hall | per-category income ledger for the finance report | IMPLEMENTED | `game.h FINCAT_*` (10 rows matching report art 0x81f4 — hotel split into single/twin/suite), banked at every income site via `game.c:fin_bank_income`, rendered by `main.c:render_fin_dialog` (2475) |

## res 0x03f1 — Price list (PRIORITY — full cross-check)

Checked against `tower.h ITEM_COST[]` and `people.c TUNING` (car costs).

| EXE string | port value | status |
|---|---|---|
| Standard Elevator $200000/Shaft $80000/Car | 200000 / car_cost_std 80000 | MATCH |
| Express Elevator $400000/Shaft $150000/Car | 400000 / 150000 | MATCH |
| Service Elevator $100000/Shaft $50000/Car | 100000 / 50000 | MATCH |
| Single Hotel Room $20000 | 20000 | MATCH |
| Twin Hotel Room $50000 | 50000 | MATCH |
| Hotel Suite $100000 | 100000 | MATCH |
| Restaurant $200000 | 200000 | MATCH |
| Office $40000 | 40000 | MATCH |
| Condo $80000 | 80000 | MATCH |
| Retail Shop $100000 | 100000 | MATCH |
| Parking $3000 | 3000 | MATCH |
| Fast Food $100000 | 100000 | MATCH |
| Medical Center $500000 | 500000 | MATCH |
| Security $100000 | 100000 | MATCH |
| **Housekeeping $50000** | **100000** | **MISMATCH** — `tower.h:163` has $100k with no citation; the EXE's own price string says $50k. Verify against cost resource 0x3e8 (the port already dumped it for elevators) and fix. |
| SECOM Center $100000 | — (no item) | MISSING item (see 0x02c6) |
| Movie Theater $500000 | 500000 | MATCH |
| Recycling Center $500000 | 500000 | MATCH |
| Stairs $5000 | 5000 | MATCH |
| Lobby $5000 | 5000 (per 4-cell segment) | MATCH |
| Escalator $20000 | 20000 | MATCH |
| Party Hall $100000 | 100000 | MATCH |
| Metro Station $1000000 | 1000000 | MATCH |
| **Cathedral $3000000** | **0** | **MISMATCH** — `tower.h:152` "Special unlock, free" is uncited folklore; the EXE lists $3,000,000. Verify against cost resource 0x3e8 item 31/0x1F and charge it. |
| Parking Ramp $50000 | 50000 | MATCH |

23 of 25 match; 1 item absent; 2 price mismatches (Housekeeping, Cathedral).

## res 0x03f2 — Star-requirement nags (PRIORITY)

The *gates* these nags explain are implemented (`game.c:game_check_promotion`,
byte-verified); what's mostly missing is **telling the player**.

| string | mechanic | status | evidence / notes |
|---|---|---|---|
| Your tower needs Security | 2→3 security gate + nag | PARTIAL | gate implemented (`game.c:295-297`); no nag anywhere — a stuck player gets no hint. |
| Your tower needs Hotel Suites | 3→4 suite gate + nag | PARTIAL | gate implemented (`game.c:304-308`); no nag. |
| Your tower needs a Recycling Center | recycling gate + nag | PARTIAL | gate implemented (`recycling_adequate`, pop/center < 2500); no nag. |
| Recycling Centers are full! | adequacy flipped off | PARTIAL | per-center comment "Full of Garbage" implemented (`game.c:1853`); no tower-level feed message. |
| Office workers demand Parking | parking demand nag (3★) | MISSING | no nag; office parking demand exists only as the car-quota valve. |
| Medical Center demanded near Lobby | medical adequacy incl. the band-0 ("near Lobby") fallback | IMPLEMENTED | `game.c:game_medical_seek` (own band then band 0) + feed nag `main.c:7393` "A sick worker found no medical center - build more!" |
| Santa Claus is coming to your tower! | Santa flyby announcement | IMPLEMENTED / DELIBERATE | flyby + feed message `main.c:7368`; cadence deliberately changed from every game-year to every ~3rd year — comment at `game.c:1491-1495`. |

---

# PRIORITIZED GAP LIST

Every MISSING/PARTIAL, ranked by gameplay impact. **[RULE]** = mechanic
missing; **[STRING]** = cosmetic/display only. One-line sketch each.

1. **[RULE] 24-shaft cap not enforced at build (silent bug)** — a 25th shaft
   costs money but never runs. *Sketch:* count elevator columns in
   `tower_can_place`; reject ≥24 with a feed message. (`No more elevator
   shafts available`)
2. **[RULE] Route-loss confirmations (0x03ed)** — stop-toggles and transport
   demolition sever floors with no warning. *Sketch:* before
   `people_set_serviced(off)` / `tower_remove(transport)`, run
   `game_update_reachability` on a what-if copy (or recompute after and
   offer undo); if a previously-reachable floor (either network) goes dark,
   open a confirm modal reusing the disaster-modal plumbing.
3. **[RULE] Singletons: Metro, Cathedral (and SECOM)** — unlimited $1M metros
   and free cathedrals break the endgame. *Sketch:* in `tower_can_place`,
   scan tenants for an existing ITEM_METRO / ITEM_CATHEDRAL and reject
   (decomp: [0xB3E8]/[0xB3EC] singletons, msgs 0x11/0x13).
4. **[RULE] Cathedral price + placement** — $0 vs the EXE's $3,000,000, and
   placeable on any floor vs 100th-floor-only. *Sketch:* set
   `ITEM_COST[ITEM_CATHEDRAL]=3000000` (verify res 0x3e8 first) and gate
   `floor == 100` (top-floor anchor) in `tower_can_place`.
5. ~~**[RULE] Person inspection (0x02bc/0x02bd)**~~ **DONE 2026-07-29** —
   traced first (InfoPeple/NameT agents): the label is derived at draw time,
   not stored, so the port adds only `Person.member` and classifies on
   (home type, member). Hit-test, popup, naming registry + purges in.
6. **[RULE] Metro placement family** — build-under-metro allowed, station not
   forced to the bottom floor. *Sketch:* track `metro_floor` on the tower;
   in `tower_can_place` reject any placement below it (msg 0xe semantics)
   and require the station's platform to sit at the current lowest
   excavated level.
7. ~~**[RULE] Floor-deck economics**~~ **DONE 2026-07-29** — TerrainCost
   1178:0583 byte-verified first (no basement premium; lobby band ×H; the
   floor tool IS the terrain charge). Extent-union charging on every
   placement; shaft extension = deck only; msgs #7/#8.
8. **[RULE] Housekeeping price $100k → $50k** — every housekeeping unit
   overcharges 2×. *Sketch:* verify res 0x3e8 item 0x0F, set
   `ITEM_COST[ITEM_HOUSEKEEPING]=50000`.
9. **[RULE] Stairs/escalator 64-record caps + escalator-commercial rule** —
   unbounded stairs trivialize transport planning; escalators between
   residential floors are illegal in the EXE. *Sketch:* count in
   `tower_can_place` (64 each); for escalators require a commercial tenant
   (shop/food/cinema/party-hall/lobby?) on both connected floors — pin the
   exact set from StairsT before coding.
10. **[RULE] Bulldozer guards** — mid-construction demolition allowed
    ("Cannot destroy items under construction"). *Sketch:* reject
    `tower_remove` when `state == TENANT_CONSTRUCTION`.
11. **[PARTIAL RULE] "People on Floor N need path to Floor M"** — generic
    stranding count exists; the floor-pair diagnostic doesn't. *Sketch:* when
    `unreachable_tenants` rises, name the lowest dark floor and its nearest
    reachable floor in the feed message.
12. **[STRING] Star-requirement nags (0x03f2)** — gates exist, guidance
    doesn't; a player stuck at 2★ is never told "Your tower needs Security".
    *Sketch:* on each failed `game_check_promotion` at threshold population,
    emit the matching nag to the feed (throttled to once/day).
13. **[STRING] Metro tenant comments** — "Last train is gone" (after 5PM) /
    "First train is not coming" (before 10AM) / "Crowded with passengers"
    (spawn quota saturated). *Sketch:* three lines in
    `game_tenant_comments` keyed on `sim->hour` and
    `spawned[i] >= METRO_VISITORS_PER_PHASE`; also delete the false
    "surfaced via the event feed" note at `game.c:1856`.
14. **[STRING] Retail variant names (0x02ca-cc)** — art variants exist,
    names don't. *Sketch:* three static name tables indexed by
    `twr_tenant_variant`; use in `inspect_title` and map overlays.
15. **[STRING] Remaining 0x02c7 lines** — "Housekeeping needed", "Office
    worker needs parking", "Too far from Lobby or Skylobby", "Transportation
    access is good", generic "Neighbors are too noisy". *Sketch:* four more
    producers in `game_tenant_comments` (lobby-distance line needs a
    floors-from-lobby scan; parking line a per-office car-quota check).
16. **[STRING] "For Rent" status row** — vacant offices/shops show no status
    word. *Sketch:* add a Status field in `ti_build` for office/shop:
    Occupied / For Rent from `TENANT_ABANDONED`.
17. **[RULE, minor] Venue cap 16** (decomp [0xB400]) — *Sketch:* count
    cinemas+party halls in `tower_can_place`.
18. **[STRING, minor] 15th movie title** ("Under the Apple Tree") — confirm
    whether the EXE uses 14 or 15 films; extend `MOVIE_TITLES`/id range if 15.
19. **[STRING, minor] Placement errors invisible in-game** — all
    `tower_can_place` rejects are stdout-only. *Sketch:* return a reason enum
    and flash the res-0x03eb text near the cursor / in the feed.
20. **[MINOR] .TDT export of >20 tenant names silently truncates** — warn at
    export (`twr_names[20]` limit); "New Tower" menu item absent; no
    save-on-quit prompt.

**Totals (rule-bearing strings/groups classified):** IMPLEMENTED 58 ·
PARTIAL 14 · MISSING 23 · N/A 12 · DELIBERATE 2 (Santa cadence, unlimited
tenant naming).

---

## Pass 2/3 outcomes (2026-07-29, post-gap-list)

Two decomp agents re-verified suspect labels (pass 2) and traced every
previously-dark dispatch handler (pass 3). Full traces:
decomp `output/pass3_coverage_2026-07-29.md` + the pass-2 commits.

**Implemented in the port (398ba81):**
- Construction queue: 10 jobs; 11th placement force-completes the oldest
  instantly (ConstructQ 11f0:004b).
- Caps: restaurants+shops+fast food share one 512-record table ([0xB3F8]);
  medical 10; security 10; recycling verified uncapped.
- Parking ramps: one vertical stack — first ramp B1-only, later ramps same
  column ([0xB3EE]); the EXE's exact error strings.
- Fire/bomb lockout: world clicks dead during an emergency (1058:0000,
  [0xB406]&9); menus/dialogs stay live.

**Verified, deferred (needs art/UI work, not rules):**
- ~~Style-variant round-robin~~ **DONE 2026-07-29** (32bd2ea): per-type
  rotation counters + Tenant.style, 7 new style composites, .TDT word +6
  round-trip; the trace also killed the "medical rotates" claim (its
  counter feeds an initial-status byte the renderer never reads) and
  found two import bugs (hotel condition byte, rate-class byte), fixed.
- ~~Grand-lobby stairs~~ **DONE 2026-07-29** (bc2f1c3): kinds 2-5 with
  the CGPk art sheets (0x8FE9/0x8FEA), promotion/whitelist/overlap/cost
  per the tall-stair trace; and the grand lobby itself is now BUILDABLE
  (893f9b2): first-click Ctrl / Ctrl+Shift height lock, mirrored upper
  rows, empty-lot new game, corner-click money doubler (documented in
  the shipped guide), $2M start.
- Scroll/minimap polish: line scroll is 16px both axes (not a floor), page
  = view−16px; minimap click navigates via animated SmoothScroll; map
  window has a close box; overlay buttons are star-gated (4th at 2★).

**Verdict of note — game speed (corrected same day):** 0x783C is
`current_tool` (0 bulldozer / 1 finger / 2 inspect / ≥3 build item), NOT
game_speed — that part stands. But the original DOES have a speed control,
which the first sweep missed and Jonah remembered: **Options → Fast Mode**
(menu id 40007, menu resource 0x8001) toggles [0xDE34], read by the TimeT
tick dispatcher (1200:01a5) — OFF throttles the sim to one tick per 6ms,
ON runs unthrottled (the default). So the original's speed model is
pause / throttled / unthrottled. The port's 1x/2x/3x Speed menu is the
same idea with fixed tiers — kept, recorded in MOD-IDEAS.md.

**Original's hidden debug menu** (trivia): menu commands 9000/9001/9002 =
start event / apply star / start fire — items deleted at WM_CREATE but the
handlers stay live in the EXE.
