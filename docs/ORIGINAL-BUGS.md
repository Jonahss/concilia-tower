# Original SimTower — genuine bugs, and surprising mechanics

Two different things live here, and they were getting conflated:

- **Part 1 — Genuine bugs.** Programming *defects* in the shipped SIMTOWER.EXE
  (v1.1 Windows): copy-paste typos, off-by-ones, missing checks, dead code,
  uninitialized reads. Things the original authors would call bugs.
- **Part 2 — Surprising mechanics (NOT bugs).** Intended design that *looks*
  like a bug or like something we invented, so we keep re-discovering it and
  asking "wait, is that broken?" Recorded so we stop re-litigating them.

Everything is verified against the simtower-decomp project. "Faithful mechanics"
means porting the behavior, not the instinct to fix it; where we'd rather fix
something it's an opt-in **mod** (UI_TODO.md), never a silent change to the base.

Sibling doc: `OPENSKYSCRAPER-ERRATA.md` (where OpenSkyscraper diverges from the
EXE). This file is where the *original itself* is surprising.

**Legend:** ✅ port reproduces it · ⚠️ real bug the port **deliberately does
not** reproduce · confidence **HIGH** (byte-verified) / **MED** (reasoned).

Started 2026-07-18.

---

# Part 1 — Genuine bugs (defects in the shipped binary)

### B1. `SetupGateCount` copy-paste: one quota slot set twice, another never ✅ HIGH
In `SetupGateCount` (00d9) the parking-quota table has `quota[4]` **assigned
twice** and `quota[9]` (condo) **never assigned** — a classic copy-paste where
the duplicated line's index was never bumped from 4 to 9. Harmless in the
shipped game because only office(0) and suite(3) people actually drive, so the
condo slot is never read — but a genuine source typo. seg ParkingT 00d9,
byte-decoded 2026-07-04.

### B2. `InParkingCar` never checks whether a space is already taken ✅ HIGH
`InParkingCar` never tests `space.person == 0`, so two cars can be assigned to
the **same physical space**. It's masked only because the per-category quota
(2× spaces) is the real limiter, so the double-assignment rarely surfaces
visibly. seg ParkingT, 2026-07-04.

### B3. Shop population counter leaks upward forever ⚠️ HIGH
`ShopVacate` adds +10 population on each re-let but **never subtracts on
vacate** (no `CountT` call in seg_11da) — the tower population counter inflates
by ~10 per re-let cycle. A missing-decrement bug. seg_1178 MoneyT, byte-verified
2026-07-11.
- The port **diverges**: recomputes population leak-free (game.c ~L1988).

### B4. Medical center registers into one floor-band but is looked up in another ⚠️ HIGH
A sick worker searches for a center in their 15-floor band, but the EXE computes
the **registration** band and the **lookup** band with *different offsets* — a
center can register into one band yet be searched for in another (off-by-a-few).
MedicalT seg_1170, byte-verified 2026-07-10.
- The port **diverges**: uses one band function (game.c ~L2680).

### B5. Noisy-neighbor adjacency scan overshoots by one record ✅ HIGH
The left/right scans range-check the *current* record, then test the **next**
one — a one-record overshoot past the strict x-cutoff. (Relatedly,
`NoiseScanRange` returns garbage for types 6/8 — unreachable only because callers
gate first.) NoiseT seg_1138, 100% byte-decoded 2026-07-03.

### B6. `PickRestaurant` validates the venue *after* choosing it ✅ HIGH
The picker selects a candidate and only then checks whether it's open; if the
random pick is closed the **whole selection fails** instead of retrying — one
closed venue can deny a customer even when open ones exist. seg54,
double-verified 2026-07-04.

### B7. The firefighting chopper misses fires left of the origin extent ✅ HIGH
The $500k chopper sweeps left from the origin floor's right edge dousing fronts,
but stops at the origin floor's **left extent** — a front that slipped left of
that point on a wider floor survives the sweep and keeps burning. An
incomplete-bound loop. FireT 10e8 0450/0856 (game.c ~L2987).

### B8. Person-info window reads an uninitialized stack buffer for unhandled types ✅ HIGH
Type 0xE (security guard) draws no text at all; a set of unhandled person types
(8, 0xB, 0xD, 0x10–0x11, 0x13–0x1C, 0x1E–0x20, 0x22, 0x23) would `TextOut` an
**uninitialized stack buffer** plus a garbage floor number. Latent — unreachable
in practice because those types aren't clickable. 2026-07-04.

## Cut features & dead code

### B9. "Fire Department" is a cut feature — and it's why the bomb never names a floor ✅ HIGH
Global 0xB3EA (fire_dept_index) has **no gameplay writer in all 80 segments**;
it's set only to −1 at new-game and save/load. Every branch keyed on it — the
spread-delay timer, and the bomb-threat dialog variant that would reveal the
target floor — is unreachable dead code. So the bomb dialog *never* tells you
which floor. corpus scan + EventT seg_10c8.

### B10. The "evening clean" housekeeping arm can never run ✅ HIGH
The hotel-room 0x20 "evening clean" branch is dead code — its caller gate forces
period ≤ 3, so the arm is never reached. seg, 2026-07-04.

### B11. There is no medical-emergency event at all ✅ HIGH
`CheckMedicalEmergency` genuinely does not exist — the segment people assumed
held it holds only `MakeLobby` and the subway routine. The only medical mechanic
is a sick office worker (10% daily roll at 3★+) who can't reach a live center.
2026-07-13.

### B12. The 15th movie, "Under the Apple Tree", can never play ✅ HIGH
String table 0x1A4 carries 15 film titles, but the movie-id space is 0..13:
a new cinema rolls `rand() % 14` (1180:014a) and the change-movie rotation
maps through `(id+1) % 7` within each half. Index 14 is unreachable — the
title shipped in every copy of the game and no player ever saw it.
referee_cinema_soundtrack_2026-07-30. (The port mirrors the 0..13 space;
the title is read from the EXE but likewise never drawn.)

### B13. Two cinema themes are malformed WAVs — two films play a silent movie ✅ HIGH (this EXE)
Each film has its own theme, sound resource 9001+movie_id (11c8:0895
`add ax,0x2329`), gated on show state. But in our SIMTOWER.EXE, resources
9004 and 9007 — the themes for film 3 "Big Wave" and film 6 "Western
Sheriff" — are not valid RIFF data (every other theme is). The original's
sndPlaySound on that data most plausibly fails silently, so those two
films screen without music. Unknown whether this is universal or a bad
pressing of this particular 1.1 EXE. The port reproduces the silence.
referee_cinema_soundtrack_2026-07-30 + RIFF sweep 2026-08-01.

---

# Part 2 — Surprising mechanics (intended design, NOT bugs)

These *look* like bugs or like port inventions but are how the game really
works. Kept so we don't keep asking.

## Tenant demand & economy

- **Stress-vacated units deadlock condemned forever.** ✅ A unit emptied by
  stress is re-judged daily against its pool's *frozen* banked stress, so it
  scores demand-0 forever until the player acts. Cutting rent to the bottom
  class is the *intended* rescue lever (also: remove a noisy neighbor, earn a
  star, or a thriving same-floor twin vouches at move-out pairing). raw 1220:0000.
- **A departing tenant makes YOU buy the unit back, and drags a neighbor out.** ✅
  The function annotated "TenantUpgrade" is actually `StressedTenantMoveOut`;
  office gentrification never existed. 2026-07-11.
- **Restaurants & fast food can never move out — and the bottom tier is a net
  loss** (−$6k/night sit-down, −$3k FF). They bleed you until demolished. 2026-07-11.
- **Shop eviction is a self-healing "blink."** ✅ `ShopVacate` resets tenure to
  0 (a fresh grace quarter) and scores persist; the vacant storefront keeps
  opening, so next dawn's judge reads the untouched quota as demand and re-lets
  it. The every-24th-day rainy/settlement collision evicts weak shops en masse
  (the designed squeeze) but healthy ones recover in ~1.5 days.
- **Rent settles once a quarter as a lump; move-outs process first so leavers
  pay nothing.** ✅ Hotels are separate — per-guest at checkout, no schedule.
- **Cutting rent revives a condemned unit the instant you click** — the price
  control calls `JudgeTenant` directly. It's also the game's only sim-time rent
  writer.
- **Late shoppers still grade your elevators.** ✅ Arrivals after closing still
  contribute service grades; each customer grades elevators (+2/+1/+0) → that's
  tomorrow's walk-in quota. Drop the late grades and basement shops death-spiral.
- **Offices never abandon from stress** — they only leave via the daily
  category-0 move-out judge. MED (MainteT `OfficeStressCheck`).

## Disasters & events (all scheduled — no RNG)

- **Fires every 84th day @10AM, bombs every 60th, Santa every 12th.** ✅ TimeT
  seg_1200. (OS models these as random — that's OS's error.)
- **Bomb hunt is pure geometry** — 6 guards/office sweep floor-by-floor, sim
  freezes, outcome by office count/proximity; detonation at exactly 1:00 PM.
- **Build the cathedral and fires stop forever** (`0xB3EC ≥ 0` gate). ✅
- **5-star towers are immune to bombs** — the ransom switch has no 5★ case. ✅
- **Venues earn $0 on bomb days and fire days.** ✅

## Elevators & transport

- **There is no call button — the queue IS the button.** ✅ First person to join
  an empty floor-queue triggers `CallElevator`.
- **"No route at all" = instant maximum stress (300),** not gradual. ✅
- **Service elevators never accrue wait stress** — staff can't get angry about
  waiting. ✅ TripT seg_1210.
- **New express shafts must anchor on a sky-lobby floor** (`%15==0`); *extending*
  one is unrestricted. ✅
- **Non-express shafts cap at a 29-floor span; express is uncapped.** ✅
- **Shafts may reach exactly one floor below the metro, no deeper.** ✅
- **Removing a car refunds nothing.** ✅ (Add-a-car price is per-type: std $80k /
  express $150k / service $50k.)
- **Hard caps: 24 shafts, 8 cars each,** with a status-bar nag on refusal. ✅
- **The metro moves no commuters** (visitors only) and **parking earns $0/car**
  (ramps cost $10k/quarter). ✅
- **A suite guest who can't park cancels the stay entirely** — and the VIP gating
  the 3→4 promotion also arrives by car, so occupancy *and* promotion are
  parking-bound. ✅

## Display, sound & time

- **The "stuck crane."** ✅ The crane's x is captured only when the top-floor
  *number* changes, so widening the top floor strands it; it's hidden entirely
  above displayed floor 100 (and under the cathedral). OverlayT seg_11c0.
- **Financial report: hotels show income at ZERO population** (guests checked out
  overnight). ✅ (No bar graph exists; number columns were sized for the EXE's
  ÷100 internal-value display.)
- **The ambient soundtrack samples a random on-screen cell each tick** and plays
  that tenant type's ambient — no background track. Elevator dings are the
  lowest priority class in a channel budget, so only a couple ring at once. ✅
- **The night elevator schedule period is hidden** — the EXE clamps it, so only
  6 of 7 periods are editable and the dialog art has 6 cells. ✅
- **Person type 0x21 reports its floor as "home floor + 2"** during info status
  0x62 — a display quirk; the class identity is unresolved. MED.
- **Hotel guests in even-numbered rooms go out for dinner** — a room-number
  parity coin-flip baked into the bytes. ✅
- **Tenancy "Length" resets to zero on load** — the 18-byte .TDT record has no
  field for it; the EXE keeps it in memory only. A faithful *omission*, not a
  defect. (Mod to persist it logged in UI_TODO.md.) ✅

---

## Resolved (were open questions — both turned out to be normal mechanics, now settled)

- **0x87EC red shaft floor-digit variant** — RESOLVED (referee 2026-07-18, HIGH).
  Not a bug and not dead: it's the "car-is-here" highlight. The floor-number
  plate on a shaft lights **red** (0x87ec, the +0x58 highlight bank) when a car
  of that group is currently on that floor — gate `IsCarOnFloor`
  (seg_10a8:367). The EXE renders it; OpenSkyscraper drops it (hardcodes atlas
  row 0). **Now implemented in the port** (main.c `draw_shaft_digits` red twin).
- **Retail closed-frame vs capacity-frame** — RESOLVED (referee 2026-07-18, HIGH).
  Not capacity-driven: the displayed frame is a discrete **state byte** at the
  venue record +0x02 (`0` empty / `1` 1-9 inside / `2` 10+ packed / `3` closed /
  `0xFF` un-let). "Closed" (state 3) is a **clock event** — shops/FF close 9PM,
  restaurants 11PM (`CloseOneVenue` 11a8:06ca), open 10AM. Shops draw a shared
  closed frame 0x22 (and for-rent 0x21); restaurants/FF draw `variant*4+3`.
  The port already does this for restaurants/FF (`retail_open`); **shops still
  need the shared closed/for-rent frames** — logged as a fix in UI_TODO.md.

---

## Part 3 — Buried things of interest (2026-07-29 pass-3 finds)

Not bugs, not mechanics — things the EXE ships but hides.

- **The hidden debug menu.** The menu resource (0x8001) ends with three
  items past Help: **9000 "Terrorist"** (force the bomb event),
  **9001 "Treasure"** (the star/level apply path), **9002 "Fire"**
  (start a fire). WM_CREATE (1158:0062) deletes all three from the menu
  at startup via USER.#413 — but the WM_COMMAND handlers stay live in
  the shipped binary. Anything that can post those command ids to the
  main window gets Maxis's own debug triggers. A fourth survivor, 9003,
  is a debug full-redraw and isn't even deleted. Confidence HIGH
  (menu resource dumped + dispatcher table traced).

- **Fast Mode is a tick-throttle bypass.** Options → Fast Mode (menu id
  40007) toggles word [0xDE34], whose only reader is the TimeT tick
  dispatcher (1200:01a5): with Fast Mode OFF the sim advances at most
  one tick per 6ms of real time; ON removes the throttle entirely — the
  sim runs as fast as the machine can draw. Init defaults it ON, so on
  1994 hardware "normal" speed was just "your computer's best effort."
  An earlier annotation called the throttle dead code because of the
  default; the menu handler makes it live. Confidence HIGH.

- **Menu trivia.** The Animation submenu (People / Effects) gates the
  crowd and effect animation passes ([0xDE30]/[0xDE32], read by
  AnimPeple/AnimeT every frame) — the 1994 "performance settings."
  "Call Fire Rescue" (40008) is greyed into the Options menu and only
  enabled during a fire.

---

## Part 4 — Manual-documented features (implementation decode notes)

Not buried, not secrets: both of these are described in the README guide
that shipped with the game and are well known to players. Kept here only
because the byte-level decode is useful to the port; the in-game
mechanism has no UI, which is why they were initially mistaken for
hidden easter eggs (corrected 2026-07-30, Jonah).

- **The corner-click money doubler.** Documented in the shipped guide.
  Mechanism (build dispatcher prologue, 11f8:0955-098c): one-shot — on a
  *virgin* tower — no tenants, lobby height still unchosen, balance
  exactly the starting $2,000,000 — a build-tool click on the lot's
  bottom-left cell (B10, cell 0) calls AwardMoney(current balance),
  doubling your money to $4M. Any spend, any placement, or the height
  lock disarms it forever (the balance check is exact). No sound, no
  dialog — the balance just changes. Ported as-is. Confidence HIGH
  (hand-disassembled).

- **The grand lobby height choice (Ctrl-click).** Documented in the
  shipped guide. Mechanism: [0xB3E6] (ground-lobby height 1-3) is
  written exactly once per tower, by the FIRST build-tool world click
  (11f8:098f-09bb): plain click = 1 story, **Ctrl-click = 2 stories,
  Ctrl+Shift-click = 3** — and the write happens before the build
  dispatch, so a click that fails to place anything still locks the
  choice. Miss the modifier on click one and your tower can never have
  a grand lobby. Ground-floor lobby drags then auto-build the upper
  rows (drag-builder 11f8:26dd), priced through TerrainCost's lobby
  band row ($5,000 x height per cell). Ported faithfully, quirks
  included. Confidence HIGH.

---

# Part 3 — Cutting-room floor (content in the EXE that nothing displays)

Found by the 2026-08-07 sprite audit (tools/ne_bmp_dims.py + contact.py;
full report: simtower-decomp output/sprite_audit_2026-08-07.md). These
resources exist in SIMTOWER.EXE but a byte-level scan finds **no
instruction that loads them** (no `push <id>` anywhere) — the shipped
game cannot show them. Likely first public documentation (TCRF has no
SimTower page as of 2026-08).

- **0xF530 / 0xF531 — city composite sheets, day & night** (269x218
  each): dense micro-facade tiles plus landmark pieces — a cathedral
  dome in 3/4 perspective (night version wreathed in smoke columns), a
  pillar bearing a woman's portrait, a narrow TOWER-sign building.
  Nothing in the manual matches (no zoom-out or city view exists).
  Best guesses: marketing/box-art compositing material, or art carried
  over from a cut feature (or from Yoot Saito's earlier *The Tower*).
  0xF53C (96x9) rides along in the same orphaned 30000-decimal id
  family.
- Other unreferenced-by-the-port art we DID identify homes for (not
  cut content, just unwired in the port so far): 0xA711 fire-chopper
  dialog picture, 0x82BD red stressed-figure row, 0x8191 weekend
  elevator-schedule art, 0x8195/96 Local/Express button strips,
  0x8080/0x8100/0x8101 splash + title screens (port will make its own
  intro instead — mods list), 0x8F6C 4-phase flame strip.
