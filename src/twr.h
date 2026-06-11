/* twr.h - Import original SimTower saved games (.TDT / .TWR)
 *
 * Format decoded from the EXE's own serializer, FileT FUN_10d0_0b3a
 * (simtower-decomp seg_10d0; field order extracted instruction-by-
 * instruction from the io_readwrite call sequence, 2026-06-11).
 * Supports version 0x24xx — the format SimTower 1.1 for Windows writes.
 */
#ifndef TWR_H
#define TWR_H

#include "game.h"

/* Load a .TDT/.TWR save into a fresh tower + sim. Returns 0 on success;
 * on failure returns -1 and puts a message in err (if non-NULL). */
int twr_import(const char *path, Tower *tower, GameSim *sim,
               char *err, int errlen);

#endif
