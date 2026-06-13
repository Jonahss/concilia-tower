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

/* Write the tower + sim as a v0x2400 .TDT. Towers that came from
 * twr_import keep the raw header/retail blocks they arrived with;
 * fresh towers get defaults observed in real saves. May assign retail
 * table slots to port-built retail tenants (hence non-const tower).
 * People are not exported; the game repopulates through the daily
 * cycle. Returns 0 on success, else -1 with a message in err. */
int twr_export(const char *path, Tower *tower, const GameSim *sim,
               char *err, int errlen);

/* Storefront variants (the retail table's +0x0B byte): how many the EXE
 * art has for a retail class, and which one a tenant shows. */
int twr_variant_count(ItemType it);
int twr_tenant_variant(const Tower *tower, const Tenant *t);

#endif
