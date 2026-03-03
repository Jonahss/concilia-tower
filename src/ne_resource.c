/* ne_resource.c - Windows NE executable resource extractor */
#include "ne_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void list_init(NEResourceList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static NEResource *list_add(NEResourceList *list)
{
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = realloc(list->items, sizeof(NEResource) * list->capacity);
    }
    NEResource *r = &list->items[list->count++];
    memset(r, 0, sizeof(*r));
    return r;
}

static NEResourceList *table_get_type(NEResourceTable *table, uint16_t type)
{
    /* Find existing */
    for (int i = 0; i < table->type_count; i++) {
        if (table->type_ids[i] == type)
            return &table->types[i];
    }
    /* Add new */
    if (table->type_count >= table->type_capacity) {
        table->type_capacity = table->type_capacity ? table->type_capacity * 2 : 16;
        table->types = realloc(table->types, sizeof(NEResourceList) * table->type_capacity);
        table->type_ids = realloc(table->type_ids, sizeof(uint16_t) * table->type_capacity);
    }
    int idx = table->type_count++;
    table->type_ids[idx] = type;
    list_init(&table->types[idx]);
    return &table->types[idx];
}

int ne_load(NEResourceTable *table, const char *path)
{
    memset(table, 0, sizeof(*table));
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ne_load: cannot open %s\n", path);
        return -1;
    }
    
    /* Read MZ header → offset to NE header */
    uint32_t off_ne;
    fseek(f, 0x3C, SEEK_SET);
    if (fread(&off_ne, 4, 1, f) != 1) goto fail;
    
    /* Read resource table offset and logical sector alignment shift */
    uint16_t off_rt, lsa;
    fseek(f, off_ne + 0x24, SEEK_SET);
    if (fread(&off_rt, 2, 1, f) != 1) goto fail;
    fseek(f, off_ne + 0x32, SEEK_SET);
    if (fread(&lsa, 2, 1, f) != 1) goto fail;
    
    /* Parse resource table */
    fseek(f, off_ne + off_rt + 2, SEEK_SET);
    
    for (;;) {
        uint16_t type, count;
        if (fread(&type, 2, 1, f) != 1) goto fail;
        if (fread(&count, 2, 1, f) != 1) goto fail;
        
        if (type == 0) break; /* End of resource table */
        
        NEResourceList *list = table_get_type(table, type);
        
        /* Skip 4 reserved bytes */
        fseek(f, 4, SEEK_CUR);
        
        for (int i = 0; i < count; i++) {
            uint16_t offset_aligned, length_aligned;
            if (fread(&offset_aligned, 2, 1, f) != 1) goto fail;
            if (fread(&length_aligned, 2, 1, f) != 1) goto fail;
            
            int offset = (int)offset_aligned << lsa;
            int length = (int)length_aligned << lsa;
            
            /* Skip 2 bytes (flags) */
            fseek(f, 2, SEEK_CUR);
            
            uint16_t id;
            if (fread(&id, 2, 1, f) != 1) goto fail;
            
            /* Skip 4 reserved bytes */
            fseek(f, 4, SEEK_CUR);
            
            NEResource *r = list_add(list);
            r->type = type;
            r->id = id;
            r->offset = offset;
            r->length = length;
            r->data = malloc(length);
            
            /* Read resource data */
            long backup = ftell(f);
            fseek(f, offset, SEEK_SET);
            if ((int)fread(r->data, 1, length, f) != length) {
                fprintf(stderr, "ne_load: short read for resource type=0x%x id=0x%x\n", type, id);
            }
            fseek(f, backup, SEEK_SET);
        }
    }
    
    fclose(f);
    return 0;

fail:
    fprintf(stderr, "ne_load: unexpected EOF in %s\n", path);
    fclose(f);
    ne_free(table);
    return -1;
}

NEResourceList *ne_find_type(NEResourceTable *table, uint16_t type)
{
    for (int i = 0; i < table->type_count; i++) {
        if (table->type_ids[i] == type)
            return &table->types[i];
    }
    return NULL;
}

NEResource *ne_find(NEResourceTable *table, uint16_t type, uint16_t id)
{
    NEResourceList *list = ne_find_type(table, type);
    if (!list) return NULL;
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id == id)
            return &list->items[i];
    }
    return NULL;
}

void ne_free(NEResourceTable *table)
{
    for (int i = 0; i < table->type_count; i++) {
        NEResourceList *list = &table->types[i];
        for (int j = 0; j < list->count; j++) {
            free(list->items[j].data);
        }
        free(list->items);
    }
    free(table->types);
    free(table->type_ids);
    memset(table, 0, sizeof(*table));
}
