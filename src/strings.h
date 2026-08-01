/* strings.h — read the EXE's own text so user-visible wording is
 * byte-faithful (and localizable for free).
 *
 * Two sources in SIMTOWER.EXE:
 *  - custom resource type 0x7f06: 21 string tables (u8 pad, u8 count,
 *    then count Pascal strings). Movie titles, item names, person kinds,
 *    tenant comments, etc.
 *  - RT_DIALOG (type 5) templates: the event/VIP/star dialogs carry
 *    their text as control captions ("Blackmail from Terrorists!...").
 *
 * Every getter takes a fallback and returns it when the EXE string is
 * missing (or exe_strings_init was never called), so callers stay safe
 * in tests and on odd EXE versions. Returned pointers are owned by this
 * module and live until program exit. */
#ifndef STRINGS_H
#define STRINGS_H

#include <stddef.h>
#include <stdint.h>
#include "ne_resource.h"

void exe_strings_init(NEResourceTable *exe);

/* String #idx (0-based) of 0x7f06 table `rid`, e.g. exe_str(0x01a4, 3, "Big
 * Wave") = the 4th movie title. */
const char *exe_str(uint16_t rid, int idx, const char *fallback);

/* The idx-th NON-EMPTY control text of dialog `dlg` (buttons + statics,
 * template order). May contain '\n' and the EXE's substitution tags
 * ("^0" = argument, "$#000" = money). */
const char *exe_dlg_text(uint16_t dlg, int idx, const char *fallback);

/* Copy src into dst replacing the first occurrence of tag with val. */
void str_subst(char *dst, size_t n, const char *src,
               const char *tag, const char *val);

#endif /* STRINGS_H */
