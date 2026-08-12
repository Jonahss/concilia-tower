# Supported SIMTOWER.EXE builds

ConcilliaTower reads the game's art, sound, and text out of your own copy of
`SIMTOWER.EXE` at runtime — nothing is bundled. Different builds of SimTower
exist (releases, localizations, patch levels), and this file is the registry
of the ones we know about.

**How acceptance actually works:** the web page (and the native binary) does a
*structural* check — the file must be a Win16 NE executable containing the six
resource types the engine reads (`0x8002` bitmaps, `0xFF0A` sounds, `0xFF03`
palette, `0xFF02` sprite sheets, `0xFF06` string tables, `0x8005` dialog
text). Any build that passes plays; the hash table below only affects the
label you see ("verified" / "recognized"), never acceptance. Resources are
located by ID through the NE resource table, so builds whose data sits at
different file offsets work without any per-build code.

## Registry

| SHA-256 | Size | Label | Status |
|---|---|---|---|
| `801f2ba4c1b4d1ae2c1c9a032c5d37cff861f04deaa9542c2521c0a6a4ccd618` | 6,220,288 | SimTower 1.1 (1994 Maxis release) | **verified** — every mechanic byte-checked against this build's code |

Statuses:
- **verified** — the reference build; the port's mechanics were
  reverse-engineered from and byte-verified against this binary.
- **working** — structure-checked, boot-tested, and played; art/sound load
  correctly. Mechanics differences from the reference build (if its code
  differs) have not been audited.
- **rejected** — known not to work, with the reason.

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

Never attach or link the EXE itself in issues or PRs — hashes and provenance
only.

## Known non-starters

- **Macintosh SimTower** — art lives in a resource fork, not an NE resource
  table. Structurally impossible here; the page detects and explains it.
- **Yoot Tower / The Tower II** (1998+) — a different engine entirely.
- **SETUP.EXE / installers** — Win16 programs without the game's resources;
  detected and rejected with a pointer to the right file.
