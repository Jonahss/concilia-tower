# Original SimTower Bugs & Quirks (faithfully reproduced)

Behaviors that look like bugs — or like things we invented — but are how the
*real* SIMTOWER.EXE (v1.1 Windows) actually behaves, verified against the
simtower-decomp project. "Faithful mechanics" means porting the quirk, not the
instinct to fix it. Where we'd rather *fix* something it lives in the port as an
opt-in **mod** (UI_TODO.md → Backlog / MODS), never as a silent change to the
faithful base.

Sibling doc: `OPENSKYSCRAPER-ERRATA.md` (where OpenSkyscraper's reconstruction
diverges from the EXE). This file is the opposite lens: where the *original
itself* does something surprising.

**Legend**
- ✅ reproduced faithfully in the port
- ⚠️ genuine original bug the port **deliberately does not** reproduce
- confidence: **HIGH** = byte-verified against the binary/decomp · **MED** =
  reasoned from decomp · **LOW** = open question / unverified

Started 2026-07-18.

---

## Save / load & counters

### 1. Tenancy "Length" resets to zero on load ✅ HIGH
The tenant-info **Length** field ("N Year M Q", capped "Over 30 years") counts
tenancy duration. Save and reload and every tenant's Length is back to zero —
even though the tenant, its name, and its rent class survive. The .TDT tenant
record is only **18 bytes** with no field for it; the EXE keeps it in an
in-memory counter (tenant +0x17, quarters) the FileT serializer never writes.
Persisting it would *invent* state SimTower doesn't keep.
- `Tenant.let_quarters` (tower.h), inc'd in game.c while occupied, shown by
  `inspect_length_str` (main.c). Absent from twr.c — matches the EXE.
- **Mod that changes it:** "persist tenancy Length across save/load" (UI_TODO.md).

### 2. Shop population counter leaks upward forever ⚠️ HIGH
`ShopVacate` adds +10 population on each re-let but never subtracts on vacate
(no `CountT` call in seg_11da), so the tower population counter inflates by ~10
per re-let cycle. seg_1178 MoneyT ShopVacate, byte-verified 2026-07-11.
- The port **diverges**: it recomputes population leak-free (game.c ~L1988).

### 3. Move-in "double-dip" in the arrival quarter ✅ HIGH
A tenant that moves in during a settlement quarter banks its full rent lump
immediately at arrival **and** still receives that quarter's settlement lump —
paid twice for one quarter. Reproduced faithfully (game.c ~L2634); byte-verified
2026-07-11.

---

## Tenant demand & economy

### 4. Stress-vacated units deadlock — condemned forever ✅ HIGH
A unit that empties from stress is re-judged daily against its pool's **frozen**
banked elevator stress (nothing resets +0x0E/+0x09 while vacant), so it scores
demand-category 0 forever and stays dead until the player intervenes. The
designed rescue levers: **cut the rent** to the bottom class (forces a pass),
remove a noisy neighbor, earn a star, or let a thriving same-floor twin vouch at
move-out pairing. "Lower the rent to re-fill" is the intended mechanic, not a
workaround. seg 03f4 / raw 1220:0000; byte-verified 2026-07-11 (game.c ~L1571).

### 5. A departing tenant makes YOU buy the unit back — and decline is contagious ✅ HIGH
When a stressed condo/office/shop moves out, a departing **condo owner forces
the player to repurchase** the unit at its rate-class price, and **one content
neighbor gets dragged toward the exit** with them. The decomp function annotated
"TenantUpgrade" is actually **StressedTenantMoveOut** — office *gentrification
never existed at all*; that was our fiction. Verified 2026-07-11.

### 6. Restaurants & fast food can NEVER move out — and the bottom tier is a net loss ✅ HIGH
Sit-down/FF venues have **no move-out path anywhere in the binary**. Under 25
customers a restaurant loses **−$6,000/night**, fast food **−$3,000/night** —
they bleed you until you demolish them. Referee-proven 2026-07-11.

### 7. Shop eviction is a "blink," not a death ✅ HIGH
`ShopVacate` resets tenure (+0x17) to 0 (seg48 @123b) — the first-settlement
immunity flag — so a re-let shop gets a brand-new grace quarter. Scores persist
through vacancy (only rec+2 = 0xFF is written) and the vacant storefront **keeps
opening every morning**; nobody consumes its walk-in quota, so the next dawn's
judge reads that untouched quota as healthy demand and re-lets it automatically.
So the every-24th-day rainy/settlement collision (day ≡ 20 mod 24) evicts weak
shops en masse — the designed rainy-day squeeze — but healthy ones self-heal
within ~1.5 days, no player action needed. Reproduced (game.c ~L1999).

### 8. Rent settles once a quarter as a lump — and leavers pay nothing ✅ HIGH
Rent is never trickled: one settlement tick every 3rd day, in strict order —
**move-outs process first** (so departing tenants pay no rent that quarter),
then every office and shop banks its full rent as a lump, then the maintenance
sweep runs on the same tick. Hotels are different again: they bank per guest at
checkout, on no schedule at all. Byte-verified 2026-07-11.

### 9. Cutting rent revives a condemned unit the instant you click ✅ HIGH
The 4-tier price control calls `JudgeTenant` directly, so changing rent triggers
an **immediate** re-judge — a condemned unit comes back the moment you drop its
price, not at the next 5AM. It's also the game's *only* sim-time rent writer.
Verified 2026-07-12/13.

### 10. Evening shoppers who arrive AFTER closing still grade your elevators ✅ HIGH
Shoppers arriving after a shop closes still contribute elevator service grades.
Each arriving customer grades the elevators (+2 fast / +1 slow / +0 awful) and
that score becomes *tomorrow's* walk-in quota — the whole retail economy is an
elevator report card. Drop those late grades and basement shops death-spiral
(worse score → smaller quota → fewer graders → evicted). Verified 2026-07-11/12.

### 11. Hotel guests in EVEN-numbered rooms go out for dinner ✅ HIGH
Whether a hotel guest dines out is decided by **room-number parity** — a 1994
coin-flip frozen in the bytes. Verified 2026-07-11.

### 12. PickRestaurant validates the venue AFTER choosing it ✅ HIGH
The venue picker chooses a candidate and only *then* checks if it's open; if the
randomly chosen venue is closed the **entire selection fails** rather than
retrying — one closed venue can deny a customer even when open ones exist. seg54,
double-verified 2026-07-04.

### 13. Offices never abandon from stress — they ride it out ✅ MED
Per MainteT `OfficeStressCheck`, an unhappy office has no stress-abandon path; it
only ever leaves via the daily category-0 move-out judge. (The "stress resets the
growth timer" idea in an old comment is unverified — office *growth* turned out
to be a fabrication we deleted.) game.c ~L625.

---

## Elevators & transport

### 14. There is no call button — the queue IS the button ✅ HIGH
A floor stop is two 40-person ring buffers; the *first* person joining an empty
queue triggers `CallElevator`. No separate call-button entity or state exists.
TripT 1210:11c2.

### 15. "No route at all" = instant maximum stress ✅ HIGH
A person who can find no route to their destination is assigned the full **300**
stress cap immediately, not by gradual accrual. Tuning resource 0x7F05 id 1000.

### 16. Service elevators never accrue wait stress ✅ HIGH
`BoardOnePerson` skips the wait charge entirely for type-2 (service) cars, so
staff riding service elevators **never get angry** about the wait, no matter how
long. TripT seg_1210, verified 2026-06-10.

### 17. New express shafts must anchor at a sky-lobby floor; extending is unrestricted ✅ HIGH
`MakeElevator` refuses a type-0 (express) shaft placed above ground unless its
base floor is a sky-lobby floor (`(displayed_floor % 15) == 0`). But *extending*
an already-placed express shaft has no such check. tenant.c 11f8:0fea.

### 18. Non-express shafts are hard-capped at a 29-floor span; express is uncapped ✅ HIGH
Standard/service shafts refuse to extend past a 29-floor span (error 0x23,
clamped) in both extend-up (0819) and extend-down (0b87). Express has no cap.
Verified 2026-07-06.

### 19. Elevators may extend to exactly one floor below the metro — no deeper ✅ HIGH
Extend-down gates on `new_bottom >= metro_floor − 1` (error 0xE otherwise): a
shaft can reach one floor below the platform but no further. With no metro (−1)
the gate never fires. 10a0:0bad.

### 20. Removing an elevator car refunds nothing ✅ HIGH
Deleting a car from a shaft touches no money — the add-car charge is never
reversed. (Add-a-car price also differs by type: standard $80k / express $150k /
service $50k, from tuning 0x7F05 — not OS's flat $80k.) ElevatorUI 10a0:036e.

### 21. Hard caps: 24 shafts, 8 cars each ✅ HIGH
Two functions annotated as "promotion" logic are actually elevator guards
enforcing max 24 shafts and 8 cars per shaft, with a status-bar nag on refusal.
Verified 2026-07-14.

### 22. The elevator dialog's night time-period is genuinely hidden ✅ HIGH
Of the 7 daily schedule periods, only **6 are editable** — the EXE clamps the
night one, and the dialog art (bitmap 0x8190) only has 6 cells for it.

### 23. The metro moves no commuters; parking earns $0 per car ✅ HIGH
The metro carries only the **visitor** crowd — commuters never arrive by train;
that mechanic simply isn't in the binary. It's a $1M visitor pump costing
$100k/quarter. Parking generates **$0 per car**; the only parking cost is
$10k/quarter per ramp. Verified 2026-07-11.

### 24. A suite guest who can't find parking cancels the stay entirely ✅ HIGH
At 3★+, suite guests arrive by car; no parking → they **cancel**, not walk in.
The VIP who gates the 3→4 star promotion also arrives by car, so suite occupancy
*and* the promotion path are literally parking-bound. Verified 2026-07-11.

---

## Disasters & events (scheduled, not random)

### 25. Disasters run on a fixed calendar — zero randomness ✅ HIGH
Fires start every **84th day** (`day % 0x54 == 0x53`) at 10:00 AM, bomb threats
every **60th day** (`day % 0x3C == 0x3B`), Santa every 12th day. Nothing is
per-tick random. TimeT seg_1200, verified 2026-07-09/10. (OS models these as
random — that's OS's error; the schedule is the surprising real fact.)

### 26. The bomb hunt has zero randomness — and never names the floor ✅ HIGH
Catch-or-detonate is pure geometry: 6 guards per security office sweep
floor-by-floor right-to-left; the sim freezes during the search; the outcome is
decided by office count and proximity, no RNG. Refuse the ransom and the bomb
detonates at frame 0x4B0 — **exactly 1:00 PM**. The dialog variant that would
name the target floor is gated on the cut fire-department global (see #30), so
the player is **never told which floor**. EventT seg_10c8.

### 27. Building the cathedral stops all fires forever ✅ HIGH
The 84th-day fire is gated on `0xB3EC < 0` (no cathedral). Once the cathedral
exists that global is ≥ 0 and fires **never trigger again**. FireT seg_10e8.

### 28. 5-star towers never get bomb threats ✅ HIGH
The bomb-ransom switch ($200k/$300k/$1M at 0xDE1C/1E/20) only has cases for stars
2/3/4 — no 5-star case — so a maxed tower is **immune to bombs**. EventT seg_10c8.

### 29. The firefighting chopper leaves fires left of the origin still burning ✅ HIGH
The $500k chopper sweeps left from the origin floor's right edge dousing fronts,
but stops at the origin floor's **left extent** — a front that slipped left of
that point on a wider floor survives the sweep and keeps burning. FireT 10e8
0450/0856 (game.c ~L2987).

---

## Cut features & dead code

### 30. "Fire Department" is a cut feature — pure dead code ✅ HIGH
Global 0xB3EA (fire_dept_index) has **no gameplay writer in all 80 segments**;
it's set only to −1 at new-game and save/load. Every branch keyed on it — the
spread-delay timer, the floor-revealing bomb dialog — is unreachable. This is
*why* the bomb threat never names a floor (#26).

### 31. There is no medical-emergency event ✅ HIGH
`CheckMedicalEmergency` genuinely does not exist — that segment holds only
`MakeLobby` and the subway routine. The only medical mechanic is a sick office
worker (10% daily roll at 3★+) who can't reach a live medical center.
Verified 2026-07-13.

### 32. The "evening clean" housekeeping arm is dead code ✅ HIGH
The hotel-room 0x20 "evening clean" arm can never run — its caller gate forces
period ≤ 3. Verified 2026-07-04.

---

## Medical, housekeeping & noise

### 33. Medical center: registration and lookup use different band offsets ⚠️ HIGH
A sick worker searches for a center in their 15-floor band, but the EXE computes
the **registration** band and the **lookup** band with different offsets — a
center can register into one band yet be searched for in another (off-by-a-few).
MedicalT seg_1170, verified 2026-07-10.
- The port **diverges**: it uses one band function (game.c ~L2680).

### 34. A full medical center turns patients away silently ✅ MED
At its 40-patients/day cap a center refuses the patient with no message, and
overflow never dings the medical-adequacy flag — only finding *no* center in-band
does. MedicalT seg_1170.

### 35. Housekeeping picks rooms up-first, not nearest-first ✅ HIGH
`ChoiceOneHRoom` selects the next dirty room by scanning **upward**, not by
proximity to the maid. Verified 2026-07-04.

### 36. The noisy-neighbor scan overshoots by one record ✅ HIGH
The left/right adjacency scans range-check the current record, then test the
**next** one — a one-record overshoot past the strict x-cutoff. (Separately,
`NoiseScanRange` returns garbage for types 6/8, unreachable because callers gate
first.) NoiseT seg_1138, 100% byte-decoded 2026-07-03.

---

## Display & UI quirks

### 37. The "stuck crane" — frozen until the top-floor NUMBER changes; hidden above floor 100 ✅ HIGH
The construction crane parks at the top floor's left edge, and its x is captured
only when the top-floor *number* changes — so **widening** the top floor leaves
the crane stranded. It's drawn only while `crane_floor < 0x6E` (displayed floor
100), so a ceiling-height tower — or one crowned by the cathedral (file floors
109–113) — shows **no crane at all**. OverlayT seg_11c0.

### 38. The financial report shows hotels with income but ZERO population ✅ HIGH
On a weekday-morning report the hotels list real income against zero population —
their guests paid at checkout overnight and already left. Looks like a bug; is
correct. (The report also has **no bar graph** — an old note guessed one; the
paint code has none — and its number columns were sized for the EXE's old
÷100 internal-value display quirk.) Report art 0x81f4.

### 39. Person-info window: guards draw nothing; unhandled types would print stack garbage ✅ HIGH
Type 0xE (security guard) draws no text at all. A set of unhandled person types
(8, 0xB, 0xD, 0x10–0x11, 0x13–0x1C, 0x1E–0x20, 0x22, 0x23) would `TextOut` an
**uninitialized stack buffer** plus a garbage floor number — a latent bug,
unreachable in practice because those types aren't clickable. Verified 2026-07-04.

### 40. The ambient soundtrack is on-screen-dependent ✅ HIGH
There's no background music track — each tick the game samples a random **visible
on-screen cell** and plays the ambient tied to that tenant type. Elevator dings
are the lowest priority class in a channel budget, so only a couple ring at once
(why a mega-tower doesn't drown in dings). Verified 2026-07-13.

### 41. Person type 0x21 reports its floor as "home floor + 2" ✅ MED
An unnamed person class (type 0x21, empty name) reports its whereabouts as home
floor **+2** during status 0x62 in the info window — a display quirk; the class's
identity is still unresolved. Helper 692c; 2026-07-04/06.

---

## Open questions (LOW confidence — recorded, not confirmed)

### 42. Red variant of the shaft floor-digit sprite (0x87EC) — purpose unknown LOW
The EXE loads a red variant of the shaft floor digits, but its render path is
undetermined. OS loads it but never renders row 1. Unresolved.

### 43. Retail closed-frame vs capacity-frame mapping LOW
Capacity maps `cap 0x40 → last frame`, but for retail pairs the last frame is
CLOSED, hinting the original keys the closed sprite off *opening hours* rather
than capacity. Not yet checked against the EXE.
