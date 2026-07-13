/* audio_events.h - event -> WAV resource ids
 *
 * Every id here is byte-verified in the decomp referee
 *   simtower-decomp/output/referee_sound_events_2026-07-13.md
 * The NE WAV resource id is the SoundT logical id with the 0x8000
 * integer-resource flag set: resource = sound_id | 0x8000. The values below
 * are the resource ids (what audio_play() / ne_find() want). The "#NNNNN" is
 * the EXE's decimal WAVE number; the referee row is cited in the comment.
 *
 * Only HIGH-confidence rows with a locatable port sim point are wired.
 * The ambient background pool (referee event #24) is MED confidence — its
 * category selection is UNPROVEN — so it is deliberately left unwired rather
 * than guessed.
 */
#ifndef AUDIO_EVENTS_H
#define AUDIO_EVENTS_H

#define SND_CASH        0xA71D  /* #10013 cash "ka-ching"        referee row 1  */
#define SND_EXPLOSION   0xA714  /* #10004 bomb/terror explosion  row 2          */
#define SND_EVENT_OK    0xA71F  /* #10015 event accept/reward    row 3          */
#define SND_FIRE_LOOP   0xA719  /* #10009 fire crackle (loop)    row 5          */
#define SND_WEDDING     0xA718  /* #10008 wedding / 5-star TOWER  row 6          */
#define SND_GARBAGE     0x88E8  /* #2280  garbage truck          row 7          */
#define SND_GUARD_STEP  0xA71E  /* #10014 guard search footsteps row 8          */
#define SND_ELEV_DING   0x9771  /* #6001  elevator arrival ding  row 9          */
#define SND_ELEV_DEPART 0x9772  /* #6002  elevator departure     row 10         */
#define SND_METRO       0xA71A  /* #10010 metro/subway train     row 11         */
#define SND_CHIME_9AM   0x938C  /* #5004  9:00 AM chime          row 12         */
#define SND_FANFARE_8AM 0x938D  /* #5005  special-day 8:00 AM    row 13         */
#define SND_FANFARE_830 0x938B  /* #5003  special-day 8:30 AM    row 14         */
#define SND_NEWDAY      0x9388  /* #5000  new-day 7:00 AM        row 15         */
#define SND_NEWDAY_SPEC 0x9389  /* #5001  new-day, special day   row 15         */
#define SND_EVENING     0x938A  /* #5002  evening chime          row 16         */
#define SND_BUILD_PLACE 0x9B58  /* #7000  place/stamp confirm    row 17         */
#define SND_BUILD_DRAG  0x9B59  /* #7001  build drag (loop)      row 18         */
#define SND_BUILD_TOOL  0x9B5A  /* #7002  tool action / toolbar  rows 19,20     */
#define SND_BUILD_DONE0 0xA714  /* #10004 build complete (rand)  row 21         */
#define SND_BUILD_DONE1 0xA715  /* #10005 build complete (rand)  row 21         */
#define SND_DELETE      0x9B5B  /* #7003  demolish / delete      row 22         */
#define SND_STARTUP     0xCE20  /* #20000 startup / intro jingle row 23         */

#endif /* AUDIO_EVENTS_H */
