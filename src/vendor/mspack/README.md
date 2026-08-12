# vendored: libmspack (KWAJ/SZDD-family decompression)

Minimal subset of [libmspack](https://www.cabextract.org.uk/libmspack/) by
Stuart Caie, vendored 2026-08-12 to expand `SIMTOWER.EX_` — the MS-Compress
KWAJ (method 3, LZH) form that SimTower's CD and floppy media ship instead
of a plain EXE. Modern `expand.exe` and 7-Zip cannot decompress this
variant; libmspack can.

Files are unmodified. License: LGPL 2.1 (see COPYING.LIB) — compatible
with this project's GPL. Entry point used: `mspack_create_kwaj_decompressor`
via `src/kwaj_expand.c`.
