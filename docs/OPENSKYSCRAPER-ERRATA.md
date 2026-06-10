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

## 1. Standard and express elevators are SWAPPED

**OS** (`Item/Elevator/Standard.h`, `Express.h`): standard = 4 cells wide,
narrow 32px shaft/car; express = 6 cells wide, the 48px "wide" art.

**EXE**: it's the other way around.
- `BuildRoutingSlots` (11b0:0538, dis16): shaft overlap width =
  `type==0 ? 6 : 4` — **standard is 6 cells**, express *and* service are 4.
- `MarkDirtyZone` (ElevatorsT 1090): standard marks ±3 cells (6 wide),
  express/service ±1.
- `CalcCarRect` (1090:216e): standard car sprite = 48px wide,
  express/service = 32px.
- The 48px sheet (0x842B) holds a dense-crowd car (~15 silhouettes when
  full) with a **double-drum** engine — that's the 42-person standard car.
  The 32px sheets are express (0x8428/0x8429) and service (0x842A).

OS's own loader comment was the missed clue: *"The standard elevator seems
to stem from an earlier phase of SimTower development, since the empty car
is in a separate bitmap."* The asymmetry exists because those bitmaps are
the **express** set.

## 2. Car capacity: standard holds 42, not 21

**OS** (`Elevator.cpp::init`): `maxCarCapacity = 21` for every type —
no subtype overrides it.

**EXE**: group+0x02 = capacity, **42 for standard**, 21 for
express/service. Confirmed end to end: the build tool's creation calls
push literal immediates — `push 0x2a; push 0` (standard), `push 0x15;
push 1` (express), `push 0x15; push 2` (service) — into MakeElevator
(tenant.c seg_11f8:0x0fea, which stores them at group+2), and boarding
computes `free = group[+2] − passenger_count` (TripT 1210:03a4). The
42-slot per-car passenger arrays (`int32[42]`, `byte[42]`) corroborate.
Yes, exactly double — 21×2 is a design choice, in the machine code.

## 3. Express speed: it's structure, not horsepower

**OS** (`Express.h::init`): express gets `maxCarSpeed = 30` vs standard's
10, and triple the acceleration. Invented.

**EXE** (`CalcMoveSpeed` 1090:209f, dis16, exact thresholds): speed gear
by distance-to-target and distance-from-run-start —
- standard (type 0): ≤1 floor either → gear 0 (crawl); ≤4 either →
  gear 2 (cruise); else **gear 3** (3 floors/tick).
- express/service: ≤1 → 0; ≤3 → **gear 1** (a middle gear standard never
  uses); else 2. Express **never reaches gear 3**.

The standard car is the fastest car in the game. The express elevator's
advantage is that it only *stops* at lobbies/sky-lobbies — fewer stops,
not more velocity.

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
  whose x-span geometrically overlaps both shafts (using the widths from
  item 1).

## 5. The route cost table is different

**OS**: escalator = 10, stairs = 30, elevator ≈ 170 per route-scoring
unit, with an "80-cell soft cap" inhibitory penalty on walking stretches.

**EXE** (TransferT score table, lower wins): walking inside a lobby
zone/chain = 0; escalator = 8×distance; stairs = 8×distance **+640**;
standard elevator = queue length + 640 (+1000 if its 40-person queue is
full); any transfer route = base **3000** (6000 if full). The real "80"
is an early-accept: an escalator within 80 cells (score < 0x280) wins
before elevators are even scored. Separately, walking ≥80/≥125 cells to
a transport adds +30/+60 **stress** (not route cost).

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
- **Engine/security/cinema animation is palette cycling**: color-table
  entries 197↔198, 199↔200, 201→202→203 rotate in place; one bitmap = 3
  frames. (OS knows this one — their loadAnimatedBitmap does the same
  swap — listed here because it's nowhere documented.)

## Open questions being dug (will confirm or add entries)

- **Prices**: OS charges standard $100k / service $80k / express $1M per
  shaft. The EXE's cost table is resource 0x3EA (type 0x7f0b looks like
  per-item-type build costs, values ×$1000: 100/200/500/1000 present).
  Add-a-car price: unverified everywhere (ConcilliaTower temporarily uses
  $80k, flagged); the answer is in ELVPOPUP (1098) → MoneyT (1178).
- **0x87EC**: the red variant of the shaft floor digits — purpose unknown
  (OS loads it but never renders row 1).
