# OpenSkyscraper Errata

Discrepancies between OpenSkyscraper's reconstruction of SimTower and the
actual SIMTOWER.EXE (v1.1 Windows), as verified against the machine code in
the simtower-decomp project (annotations + `tools/dis16.py` hand-disassembly).
Every entry cites the EXE evidence (segment:offset addresses are NE
segments; `dis16` = hand-disassembly, used where Ghidra was blind).

OpenSkyscraper (OS) deserves real credit — its loader documentation of the
resource formats is what bootstrapped our sprite work, and its behavioral
reconstructions were good enough to fool everyone for a decade. That's
exactly why the corrections are worth publishing.

Maintained as we go. Started 2026-06-10.

---

## 0. Erratum to the errata: our retracted "swap" claim

For about three hours on 2026-06-10 this file's first entry claimed OS had
standard and express elevators swapped (wide 42-person car = standard). That
was **our** error, not theirs: we had correctly proven that *type 0* is the
wide, 42-person, fast one — and then attached the wrong name to type 0.
Five "independent confirmations" all inherited that one labeling assumption.

What settled it (2026-06-11, all dis16-verified):

- **The sky-lobby anchor.** MakeElevator (tenant.c 11f8:0fea, at 0ff9):
  a type-0 shaft placed above ground must sit on a floor where
  seg21:0x12e0 returns true — and that function is literally
  `floor > ground && (displayed_floor % 15) == 0`. Only the express
  elevator has the every-15th-floor rule. Basements are exempt (express
  serves B1–B10), also matching.
- **Build prices.** The per-item cost resource (type 0x7f0b id 0x3e8,
  BE u32, internal money ×$100): item 0x01 → type 1 costs **$200,000**,
  item 0x2a → type 0 costs **$400,000**, item 0x2b → type 2 costs
  **$100,000**. Those are the game's known standard/express/service
  prices, in that order.

So: **type 0 = express** (6 cells wide, 42 passengers, the 48px art, the
gear-3 turbo, queue-based route scoring), type 1 = standard, type 2 =
service. OS's wide-express labeling was right all along. The corrected
facts below are still corrections — several of OS's *numbers* and
*mechanisms* remain invented — but the labels now match the EXE.

Lesson recorded for both projects: a label is a hypothesis too. Every
"confirmation" was a fact about *type 0*; none of them tested the name.

## 1. Car capacity: the express car holds 42, and capacities differ by type

**OS** (`Elevator.cpp::init`): `maxCarCapacity = 21` for every type —
no subtype overrides it.

**EXE**: group+0x02 = capacity, **42 for express**, 21 for
standard/service. Confirmed end to end: the build tool's creation calls
push literal immediates — `push 0x2a; push 0` (express, 42), `push 0x15;
push 1` (standard, 21), `push 0x15; push 2` (service, 21) — into
MakeElevator (tenant.c seg_11f8:0x0fea, which stores them at group+2),
and boarding computes `free = group[+2] − passenger_count` (TripT
1210:03a4). The 42-slot per-car passenger arrays (`int32[42]`,
`byte[42]`) corroborate. Exactly double — 21×2 is a design choice, in
the machine code. (And it's the giant double-width car that holds 42,
which is why nobody's eyebrows were raised in 1994.)

## 2. Elevator speed: a 4-gear distance table, not maxSpeed/acceleration

**OS** (`Express.h::init`): express gets `maxCarSpeed = 30` vs standard's
10, and triple the acceleration. Direction right — the express *is* the
fast one — but the numbers and the model are invented.

**EXE** (`CalcMoveSpeed` 1090:209f, dis16, exact thresholds): speed gear
by distance-to-target and distance-from-run-start —
- express (type 0): ≤1 floor either → gear 0 (crawl); ≤4 either →
  gear 2 (cruise); else **gear 3** (3 floors/tick).
- standard/service: ≤1 → 0; ≤3 → **gear 1** (a middle gear express never
  uses); else 2. Standard **never reaches gear 3**.

So the express wins twice: top gear on long hauls, *and* it only stops at
lobbies/sky-lobbies. There is no per-type maxSpeed or acceleration
constant anywhere — the whole curve is that one distance table.

## 3. Build prices

**OS**: standard $100k / service $80k / express $1M per shaft.

**EXE** (cost resource type 0x7f0b id 0x3e8, BE u32 ×$100, indexed by
item type): standard **$200k** (item 0x01 = 2000), express **$400k**
(item 0x2a = 4000), service **$100k** (item 0x2b = 1000). New shafts
check and charge through MoneyT's build pair (1178:009e CanAffordBuild /
1178:01db ChargeBuild = item cost + terrain cost; both halves of MoneyT's
000c–07e8 range were a Ghidra blind spot, dis16-read 2026-06-11).

## 3b. Add-a-car price: per type, and from the tuning resource

**OS**: a flat **$80k** for any extra car, any type
(`Game.cpp: transferFunds(-80000)`).

**EXE**: clicking the build tool on an existing shaft *is* the add-car
path — it lives inside PlaceElevator (tenant.c 11f8: CanAddCar max-8
check via 1148:02c8, afford gate 10e3–113f "error 7", inline charge
11b0–11ed) — and the price is **per type: standard $80k / express
$150k / service $50k**. The values (globals 0xde0a/0c/0e) come not
from the cost table but from the master **tuning resource** (0x7F05
offsets +0x90/92/94), so car prices are moddable balance data. OS's
$80k was right for exactly one of the three types. Removing a car
refunds nothing (ElevatorUI 10a0:036e — no money touch).

## 3c. Running costs: the upkeep sweep OS doesn't model

The third cost resource (**0x3ea**, unread by OS) is the per-item
maintenance table, consumed by MoneyT's sweep (1178:0b44, run once per
3-day quarter at day-tick 0x9e5). Elevators charge **per car**:
standard $10k/car, express $20k/car, service $10k/car; escalators $5k
each; stairs free. Lobby upkeep (1178:0a6a) is per **cell**, star-gated
by tuning values 0xde16/18/1a = 0/30/100: **free below 3 stars**,
$300/cell at 3 stars, $1000/cell at 4+. And promotions to star 2/3/4
pay a **bonus** of $200k/$300k/$500k (tuning +0xa8, LevelUp 1148:020f →
MoneyT 1178:076f, cash capped at $99,999,999). Resource 0x3e9 rounds
out the set: per-item income/expense rates in 4 rate classes
(`[item*0x10 + class*4]`, e.g. office = 150/100/50/20 ×$100).

## 4. The walking/transfer budgets are folklore

**OS** (pathfinder): per-trip budgets of "3 stairs, or 6 escalators, or
1 stair + 2 escalators / 2 stairs + 1 escalator; 2nd elevator only at a
sky lobby and only if no stairs were used; service and public never mix."

**EXE** (TransferT 11b0 + TripT 1210, dis16 — these functions were in
Ghidra's blind spots, which is why nobody had read them):
- A walking leg is capped at **6 floors if all-escalator, 3 floors if any
  stair is involved** (11b0:0dc0/0e80). No per-device counting, no mixing
  arithmetic. Staff walk stairs only, max 3.
- A route is transport → lobby → transport, **max ONE transfer of any
  kind** — not a counter, the routing record physically has one slot
  field. OS's "no 2nd elevator after stairs" rule is a behavioral shadow
  of this (stairs+elevator already spends the transfer).
- Transfers happen only at the 16 routing slots (0xdb9c): lobby tenants
  whose x-span geometrically overlaps both shafts (6 cells for an express
  shaft, 4 for standard/service).

## 5. The route cost table is different

**OS**: escalator = 10, stairs = 30, elevator ≈ 170 per route-scoring
unit, with an "80-cell soft cap" inhibitory penalty on walking stretches.

**EXE** (TransferT score table, lower wins): walking inside a lobby
zone/chain = 0; escalator = 8×distance; stairs = 8×distance **+640**;
**express** elevator = queue length + 640 (+1000 if its 40-person queue
is full); standard/service elevator = 8×distance + 640; any transfer
route = base **3000** (6000 if full). The real "80" is an early-accept:
an escalator within 80 cells (score < 0x280) wins before elevators are
even scored. Separately, walking ≥80/≥125 cells to a transport adds
+30/+60 **stress** (not route cost).

## 6. Stress constants (OS can't have known — they're in a hidden resource)

All of SimTower's balance constants live in resource **0x7F05 id 1000**,
big-endian (Mac heritage), loaded to DS:0xDD7A.. — the game is secretly
data-driven. Real values: wait-frustration cap 300; full-queue rejection
+5; **no route at all = instant 300** (cap-out); walking stress per span:
escalator 16, stairs 35; star population thresholds 300/1k/5k/10k.
Anything OS hardcoded for these is at best a guess; the resource is the
canon (and is live-editable in ConcilliaTower's F4 panel).

## 7. Smaller things the EXE does that OS doesn't model

- **The call button doesn't exist**: a floor stop is two 40-person ring
  buffers; the *first person joining an empty queue* triggers
  CallElevator (TripT 1210:11c2). The queue is the button.
- **Door choreography** (door_timer 5→0): tick 5 = ding + everyone for
  this floor exits; each odd tick boards exactly one person; tick 1 bulk
  boards the rest; a full car departs instantly.
- **Service elevators never bank wait stress** — BoardOnePerson skips the
  WaitT charge for type 2. Staff cannot get angry about waiting.
- **Grand lobby patience buff**: a 2-story lobby forgives 25 ticks of
  elevator wait, 3-story forgives 50, before frustration accrues (WaitT,
  global 0xB3E6).
- **Per-floor serviced flags** are per *group* (+0x42+floor), edited by
  the ElvDlogT dialog grid — not just a top/bottom range.
- **Per-period car schedules**: each elevator group carries three
  [weekday/weekend][7-period] tables (threshold +0x12: how much closer a
  busy car must be to beat sending an idle one, default 5; mode +0x20:
  normal vs shuttle, plus both-direction pickup when nonzero; patience
  +0x2e: door dwell ×30 ticks, 0–3). The clock: 0xB3A1 = period =
  frame/400 (7 per day), 0xB3A0 = weekend flag from the 3-day quarter
  (WD1/WD2/WE). The dialog edits all three (the mode write hides behind
  a +9/+23 offset split at 1098:25d8). OS models none of this.
- **New express shafts must anchor at a lobby**: MakeElevator refuses a
  type-0 shaft above ground unless its base floor is a sky-lobby floor
  (every 15th); extending an existing shaft is unrestricted. OS lets you
  start an express shaft anywhere.
- **Engine/security/cinema animation is palette cycling**: color-table
  entries 197↔198, 199↔200, 201→202→203 rotate in place; one bitmap = 3
  frames. (OS knows this one — their loadAnimatedBitmap does the same
  swap — listed here because it's nowhere documented.)

## Disasters are SCHEDULED, and the fire model is fronts + a $500k chopper

Decoded 2026-07-09/10 (TimeT seg_1200 dispatcher map, FireT seg_10e8,
EventT seg_10c8 — byte-verified referees):

- **Fires are on a calendar**: TimeT's 10:00 AM block starts one every
  84th day (`day % 0x54 == 0x53`, "a fire every 7 game-years"), gated on
  star > 2, a security office, **and no cathedral** (0xB3EC < 0) — build
  the cathedral and fires stop forever. Nothing is random per-tick.
- **Bomb threats every 60th day** (`day % 0x3C == 0x3B`), only at stars
  2/3/4 — the ransom switch (0xDE1C/1E/20 = $200k/$300k/$1M) has no other
  case, so 5-star towers never get one. Refusing arms the bomb; it
  detonates at exactly 1:00 PM (frame 0x4B0) unless a guard reaches it.
  The dialog never reveals the floor (the floor-naming variant is gated
  on the cut fire-department global 0xB3EA — dead code).
- **Fire = per-floor left/right fronts**, not a radius: each front
  destroys the cell it stands on and advances 1 cell every 7 frames
  (tuning 0xDDD0); each floor ABOVE the origin ignites at the origin cell
  80 frames later (0xDDD2) — fire never spreads downward. A front dies at
  the floor extent's edge; unfought fires hard-stop at 9:00 PM (frame
  2000).
- **The firefighting helicopter is real and purchasable**: right after
  ignition, dialog 0xBC4 offers choppers for $500,000 (tuning 0xDE14).
  Pay and 0xB418 (mislabeled "fire_spread_count" — it's the chopper x)
  starts at the origin floor's right edge and sweeps left 1 cell/frame,
  dousing every front to its right (FireT 0450/0856).
- **"Fire department" is a cut feature**: global 0xB3EA has no gameplay
  writer in all 80 segments — every branch keyed on it (spread-delay
  timer, floor-revealing bomb dialog) is dead code.

OS models disasters as random events and has no chopper/front mechanics.

## Open questions being dug (will confirm or add entries)

- **0x87EC** — RESOLVED (referee 2026-07-18): the red shaft floor-digit
  variant is the "car-is-here" highlight — the plate lights red when a car
  of that group is on that floor (`IsCarOnFloor`, seg_10a8:367). The EXE
  renders it; OS loads it but hardcodes atlas row 0 and never shows it.
  Port implements it (main.c draw_shaft_digits). Full entry in
  ORIGINAL-BUGS.md "Resolved" section.
- Tuning words 0xde10/12 (change-movie costs, found) — 0xde14/1c/1e/20
  settled above (chopper + bomb ransoms).

## The construction crane: left-parked, 7-cell minimum, hidden at the top

OpenSkyscraper (`Decorations.cpp updateCrane`) centers the crane over
the top floor, shows it whenever that floor is >= 4 cells wide, and
never hides it. The EXE (OverlayT seg_11c0, `024a` update + `0000`
draw gate) disagrees on every count:

- **Position**: the crane parks at the top floor's **left edge**
  (`crane_x = floor_hdr.left`), not the center — and the x is captured
  only when the top floor *number* changes, so widening the top floor
  leaves the crane where it was (the original's famous stuck crane).
- **Width gate**: extent must be >= **7** cells, not 4.
- **Visibility**: drawn only while `crane_floor < 0x6E` (file floor
  110 = displayed floor 100). A tower built to the ceiling — or
  crowned by the cathedral, whose records occupy file floors 109-113 —
  shows no crane at all.

Evidence: seg_11c0 raw (`FUN_11c0_024a` scan/assign, `FUN_11c0_0428`
36x36 rect one row above at `crane_x*8 - scroll`, gate in
`FUN_11c0_0000`).

## Parking-space sheets 0x86A8/0x86A9: not "departing/arriving"

OpenSkyscraper's `SimTowerLoader.cpp` labels the two parking bitmaps
`car/departing` (0x86A8) and `car/arriving` (0x86A9) and merges them
as one strip. Dumping the DIBs (2026-07-12) shows they're the parking
SPACE's frame set, not car-motion frames:

- **0x86A8** is a single 32x24 frame: the **empty bay**.
- **0x86A9** is 14 frames: the **red X** (space unusable — off the
  B1-anchored ramp chain, ParkingT CheckAllParking) followed by
  **13 parked-car variants**.

So the merged 15-frame strip is: empty bay, X, then 13 cars. Nothing
in either sheet animates arrival or departure; the EXE picks a car
frame per occupied space (random 1-of-13 at park time, 1198:031a
path) and the X/empty frames by usability/occupancy.
