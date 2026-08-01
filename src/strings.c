/* strings.c — see strings.h. Formats decoded 2026-07-30 (hex-verified):
 *
 * 0x7f06 string table: byte 0 = 0, byte 1 = count, then `count` Pascal
 * strings back-to-back (length byte + chars, no terminator).
 *
 * Dialog (RT_DIALOG, Win16 DLGTEMPLATE): u32 style, u8 item count,
 * x/y/cx/cy u16s, menu (0xFF+ord or ASCIIZ), class ASCIIZ, caption
 * ASCIIZ, [DS_SETFONT: u16 size + face ASCIIZ]; each item: x/y/cx/cy/id
 * u16s, u32 style, class (byte >= 0x80 = ordinal, else ASCIIZ), text
 * (0xFF+ord or ASCIIZ), u8 extra-len + extra. */
#include "strings.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_TABLES 24
#define MAX_TABLE_STRINGS 64
#define MAX_DIALOGS 48
#define MAX_DIALOG_TEXTS 8

typedef struct {
    uint16_t rid;
    int      count;
    char    *strs[MAX_TABLE_STRINGS];
} StrTable;

typedef struct {
    uint16_t id;
    int      count;
    char    *texts[MAX_DIALOG_TEXTS];
} DlgTexts;

static StrTable g_tables[MAX_TABLES];
static int      g_ntables = 0;
static DlgTexts g_dialogs[MAX_DIALOGS];
static int      g_ndialogs = 0;

static char *dup_range(const uint8_t *p, int len)
{
    char *s = malloc((size_t)len + 1);
    if (!s) return NULL;
    memcpy(s, p, (size_t)len);
    s[len] = '\0';
    return s;
}

static void load_table(const NEResource *r)
{
    if (g_ntables >= MAX_TABLES || r->length < 2) return;
    StrTable *t = &g_tables[g_ntables];
    t->rid = r->id & 0x7fff;
    t->count = 0;
    int n = r->data[1];
    int p = 2;
    for (int i = 0; i < n && i < MAX_TABLE_STRINGS; i++) {
        if (p >= r->length) break;
        int len = r->data[p++];
        if (p + len > r->length) break;
        t->strs[t->count++] = dup_range(r->data + p, len);
        p += len;
    }
    g_ntables++;
}

/* Advance past an ASCIIZ string; returns -1 on overrun. */
static int skip_sz(const uint8_t *b, int len, int p)
{
    while (p < len && b[p]) p++;
    return p < len ? p + 1 : -1;
}

static void load_dialog(const NEResource *r)
{
    if (g_ndialogs >= MAX_DIALOGS || r->length < 13) return;
    const uint8_t *b = r->data;
    int len = r->length;
    uint32_t style = (uint32_t)b[0] | (uint32_t)b[1] << 8 |
                     (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    int items = b[4];
    int p = 5 + 8;                                   /* x,y,cx,cy */
    if (p >= len) return;
    if (b[p] == 0xFF) p += 3; else p = skip_sz(b, len, p);   /* menu */
    if (p < 0 || p >= len) return;
    p = skip_sz(b, len, p);                          /* class */
    if (p < 0 || p >= len) return;
    p = skip_sz(b, len, p);                          /* caption */
    if (p < 0) return;
    if (style & 0x40) {                              /* DS_SETFONT */
        p += 2;
        p = skip_sz(b, len, p);
        if (p < 0) return;
    }

    DlgTexts *d = &g_dialogs[g_ndialogs];
    d->id = r->id & 0x7fff;
    d->count = 0;
    for (int i = 0; i < items && p >= 0 && p < len; i++) {
        p += 14;                                     /* x,y,cx,cy,id + style */
        if (p >= len) break;
        if (b[p] & 0x80) p++;                        /* ordinal class */
        else p = skip_sz(b, len, p);
        if (p < 0 || p >= len) break;
        if (b[p] == 0xFF) {
            p += 3;                                  /* ordinal text */
        } else {
            int s = p;
            p = skip_sz(b, len, p);
            if (p < 0) break;
            int tl = p - 1 - s;
            if (tl > 0 && d->count < MAX_DIALOG_TEXTS)
                d->texts[d->count++] = dup_range(b + s, tl);
        }
        if (p >= len) break;
        p += 1 + b[p];                               /* extra data */
    }
    if (d->count > 0) g_ndialogs++;
}

void exe_strings_init(NEResourceTable *exe)
{
    /* ne_load keeps NE type ids raw — int types carry the 0x8000 bit. */
    NEResourceList *l = ne_find_type(exe, 0xFF06);
    if (l)
        for (int i = 0; i < l->count; i++) load_table(&l->items[i]);
    l = ne_find_type(exe, 0x8005);
    if (l)
        for (int i = 0; i < l->count; i++) load_dialog(&l->items[i]);
    printf("Strings: %d tables + %d dialog texts loaded from the EXE\n",
           g_ntables, g_ndialogs);
}

const char *exe_str(uint16_t rid, int idx, const char *fallback)
{
    for (int i = 0; i < g_ntables; i++)
        if (g_tables[i].rid == rid)
            return (idx >= 0 && idx < g_tables[i].count &&
                    g_tables[i].strs[idx])
                       ? g_tables[i].strs[idx] : fallback;
    return fallback;
}

const char *exe_dlg_text(uint16_t dlg, int idx, const char *fallback)
{
    for (int i = 0; i < g_ndialogs; i++)
        if (g_dialogs[i].id == dlg)
            return (idx >= 0 && idx < g_dialogs[i].count)
                       ? g_dialogs[i].texts[idx] : fallback;
    return fallback;
}

void str_subst(char *dst, size_t n, const char *src,
               const char *tag, const char *val)
{
    const char *hit = strstr(src, tag);
    if (!hit || !n) {
        snprintf(dst, n, "%s", src);
        return;
    }
    size_t pre = (size_t)(hit - src);
    if (pre >= n) pre = n - 1;
    memcpy(dst, src, pre);
    dst[pre] = '\0';
    size_t used = strlen(dst);
    snprintf(dst + used, n - used, "%s%s", val, hit + strlen(tag));
}
