/* ne_resource.h - Windows NE executable resource extractor
 * 
 * Reads resource table from 16-bit New Executable (NE) format files.
 * Used to extract bitmaps, sounds, and palettes from SIMTOWER.EXE.
 *
 * Based on OpenSkyscraper's WindowsNEExecutable by Fabian Schuiki,
 * rewritten in C for the SimTower Linux port.
 */
#ifndef NE_RESOURCE_H
#define NE_RESOURCE_H

#include <stdint.h>
#include <stddef.h>

/* NE resource types (from Windows SDK) */
#define NE_RT_BITMAP    0x8002  /* Standard bitmaps (DIB) */
#define NE_RT_SOUND     0xFF0A  /* WAV sounds (custom) */
#define NE_RT_PALETTE   0xFF03  /* Custom palette resources */
#define NE_RT_RAWBITMAP 0xFF02  /* Raw 8-bit pixel data (custom) */

typedef struct {
    uint16_t type;
    uint16_t id;
    int      offset;
    int      length;
    uint8_t *data;       /* Owned - caller must free */
} NEResource;

typedef struct {
    NEResource *items;
    int         count;
    int         capacity;
} NEResourceList;

typedef struct {
    NEResourceList *types;    /* Array indexed by type */
    int             type_count;
    int             type_capacity;
    uint16_t       *type_ids; /* Parallel array of type IDs */
} NEResourceTable;

/* Load all resources from an NE executable.
 * Returns 0 on success, -1 on error. */
int ne_load(NEResourceTable *table, const char *path);

/* Find all resources of a given type.
 * Returns NULL if type not found. */
NEResourceList *ne_find_type(NEResourceTable *table, uint16_t type);

/* Find a specific resource by type and ID.
 * Returns NULL if not found. */
NEResource *ne_find(NEResourceTable *table, uint16_t type, uint16_t id);

/* Free all resources in a table. */
void ne_free(NEResourceTable *table);

#endif /* NE_RESOURCE_H */
