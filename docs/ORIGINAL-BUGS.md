# Original SimTower Bugs & Quirks (faithfully reproduced)

Behaviors that look like bugs but are how the *real* SIMTOWER.EXE (v1.1
Windows) actually behaves, verified against the simtower-decomp project. We
reproduce them on purpose — "faithful mechanics" means porting the quirk, not
the instinct to fix it. Anything we'd rather *fix* lives in the port as an
opt-in **mod** (see UI_TODO.md → Backlog / MODS), never as a silent change to
the faithful base.

Sibling doc: `OPENSKYSCRAPER-ERRATA.md` (where OpenSkyscraper's reconstruction
diverges from the EXE). This file is the opposite lens: where the *original
itself* does something surprising.

Started 2026-07-18.

---

## 1. Tenancy "Length" resets to zero on load

**What you see:** the tenant-info dialog's **Length** field ("N Year M Q",
capped "Over 30 years") counts how long a tenant has occupied a unit. Save the
tower, reload it, and every tenant's Length is back to zero — even though the
tenant, its name, and its rent class all survive the round-trip.

**Why it's faithful, not our bug:** the .TDT tenant record is only **18 bytes**
and has no field for tenancy length. In the EXE it lives in an in-memory-only
counter (tenant +0x17, quarters) that the FileT serializer never writes. So the
original resets Length on load too. Persisting it in the port would be
*inventing* state SimTower doesn't keep.

- Port field: `Tenant.let_quarters` (`tower.h`), incremented in `game.c`
  (~L1336) while occupied; displayed by `inspect_length_str` (`main.c`).
- Not present in the `twr.c` serializer — confirmed by grep, matching the EXE.

**Mod that changes it:** "persist tenancy Length across save/load" — logged in
UI_TODO.md as an opt-in port nicety. The faithful base keeps resetting.
