# Supported SIMTOWER.EXE builds

ConcilliaTower reads the game's art, sound, and text out of your own copy of
`SIMTOWER.EXE` at runtime — nothing is bundled. Different builds of SimTower
exist (releases, pressings, localizations, patch levels), and this file is
the registry of the ones we know about.

**How acceptance actually works:** the web page (and the native binary) does
a *structural* check — the file must be a Win16 NE executable containing the
six resource types the engine reads (`0x8002` bitmaps, `0xFF0A` WAVE sounds,
`0xFF03` CLUT palette, `0xFF02` CGPK sprite sheets, `0xFF06` STRL string
tables, `0x8005` dialog text). Any build that passes plays; the hash table
below only affects the label you see ("verified" / "recognized"), never
acceptance. Resources are located by ID through the NE resource table, so
builds whose data sits at different file offsets work without per-build code.
Pressings that identify the custom types by *name* (`ALRT`…`YEN`) instead of
ordinal IDs are resolved back to the canonical ordinals automatically.

**Compressed media copies work too.** The original CD and floppies never
ship a plain EXE — they carry `SIMTOWER.EX_` in MS-Compress KWAJ form
(method 3, LZH), which neither 7-Zip nor modern Windows `expand.exe` can
decompress. The web page expands it in-browser (bundled libmspack). The
floppy set splits it across disks 2–3 as `SIMTOWER.E1_` + `SIMTOWER.E2_`
(two independent KWAJ streams); drop both together and the page expands
each and joins them. Natively: `./simtower --expand SIMTOWER.EX_ SIMTOWER.EXE`.

## Registry

| SHA-256 | Size | Label | Status |
|---|---|---|---|
| `801f2ba4c1b4d1ae2c1c9a032c5d37cff861f04deaa9542c2521c0a6a4ccd618` | 6,220,288 | SimTower 1.1b (US, official TOWERW11 patch, 1995) | **verified** — every mechanic byte-checked against this build |
| `2825a3c53f77945c63b6d72e26faa7dde5ddd56c31ca668e67a12576d7feca96` | 6,566,400 | SimTower 1.0 (US, Apr-1995 CD/floppy pressing; tower-together's reference) | **working** — boot-tested, art/sound load and render correctly |
| `ecb91df191336e94efb95f4828970b6bc3cfaad802ee671902ab695f38490a42` | 6,511,104 | SimTower 1.0 (US, original Nov-1994 CD pressing) | **working** — boot-tested, art/sound load and render correctly |
| `368cb4b9278bdf687c25c0324f180acaf3010551c33057d82c75066916985142` | 6,783,488 | The Tower for Windows 1.2J (Japanese original, `TOWER.EXE`) | **rejected** — 13 custom resource types (+2 ordinal shift), Shift-JIS text; detected and declined with an explanation |

Compressed distribution forms these expand from (for identification only —
the page accepts them directly):

| File | Where | Size | SHA-256 | Expands to |
|---|---|---|---|---|
| `SIMTOWER.EX_` | Apr-1995 CD / setup dir | 2,521,765 | `b3efe22b9efec71141aad0129c90c1dcc886a44d08342f8f94fc73f24fd0e736` | 1.0 Apr-95 |
| `SIMTOWER.EX_` | Nov-1994 CD | 2,447,363 | `dfd29ad134b83407bdb53080505b16063c4440cd00378879e9570dd8c5e30562` | 1.0 Nov-94 |
| `SIMTOWER.E1_` + `SIMTOWER.E2_` | floppy disks 2+3 | 1,457,664 + 1,043,793 | `c1dc4de9…` + `98863179…` | 1.0 Apr-95 |

Statuses:
- **verified** — the reference build; the port's mechanics were
  reverse-engineered from and byte-verified against this binary.
- **working** — structure-checked, boot-tested, played; art/sound load
  correctly. Mechanics differences from the reference build (if its code
  differs) have not been audited.
- **rejected** — known not to work, with the reason.

Known to exist, no dump examined yet: a Spanish localization (reported in
OpenSkyscraper issue #5), a presumed French localization, and at least one
pressing whose custom resource types are name-encoded (handled in code, but
untested against a real dump — if you have one, please report!). The German
retail release shipped the English EXE.

## Contributing a build

Have a copy that isn't listed? The page will have shown you a 12-character
build id (the SHA-256 prefix). To add it:

1. Get the full hash: `sha256sum SIMTOWER.EXE` (or
   `certutil -hashfile SIMTOWER.EXE SHA256` on Windows).
2. Note where it came from (which release/CD/patch, language) and its exact
   size in bytes.
3. Confirm it plays in the browser: art looks right, sounds fire, a tower
   saves and reloads.
4. Open a PR adding a row here and an entry to `KNOWN_BUILDS` in
   `web/shell.html` — or just open an issue with the hash, size, and
   provenance, and we'll take it from there.

Never attach or link the EXE itself in issues or PRs — hashes and
provenance only.

## Known non-starters

- **Macintosh SimTower** — no NE executable at all; code and art live in a
  classic Mac resource fork (the Windows port's custom NE types ALRT/CLUT/
  WAVE/STRL literally mirror the Mac Resource Manager types). Appears as
  HFS/hybrid CD images, StuffIt archives, or MacBinary; detected and
  explained.
- **Yoot Tower / The Tower II** (1998–99) — the sequel; 32-bit PE with a
  plugin-based data architecture. Different game.
- **The Tower for Windows 1.2J** — see registry; support would need the +2
  resource-ordinal shift plus Shift-JIS text handling. Feasible later if
  wanted.
- **SETUP.EXE / installers / launchers** — Win16 programs without the
  game's resources (or 32-bit PE launchers like the 2001 reissue's
  `STOWER.EXE`); detected and rejected with a pointer to the right file.
