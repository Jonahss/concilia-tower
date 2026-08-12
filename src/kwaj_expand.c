/* kwaj_expand.c — expand MS-Compress KWAJ files (SIMTOWER.EX_ → SIMTOWER.EXE)
 *
 * SimTower's CD and floppy media never ship a plain SIMTOWER.EXE: they carry
 * SIMTOWER.EX_ in MS-Compress KWAJ form (method 3, LZH), which neither 7-Zip
 * nor modern Windows expand.exe can decompress. libmspack can — so the web
 * shell hands an uploaded .EX_ through this wrapper (file-to-file on MEMFS)
 * and boots the result. Also reachable natively via `simtower --expand`.
 */
#include <stdio.h>
#include "vendor/mspack/mspack.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define CT_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define CT_EXPORT
#endif

/* Returns 0 on success, an MSPACK_ERR_* code otherwise. */
CT_EXPORT int ct_kwaj_expand(const char *in_path, const char *out_path)
{
    struct mskwaj_decompressor *d = mspack_create_kwaj_decompressor(NULL);
    if (!d) return MSPACK_ERR_NOMEMORY;
    int err = d->decompress(d, in_path, out_path);
    if (err != MSPACK_ERR_OK)
        fprintf(stderr, "kwaj_expand: %s -> %s failed (mspack error %d)\n",
                in_path, out_path, err);
    mspack_destroy_kwaj_decompressor(d);
    return err;
}
