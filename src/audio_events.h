/* audio_events.h - event -> WAV resource ids
 *
 * Every id here is byte-verified in the decomp referee
 *   simtower-decomp/output/referee_sound_events_2026-07-13.md
 * The NE WAV resource id is the SoundT logical id with the 0x8000
 * integer-resource flag set: resource = sound_id | 0x8000. The values below
 * are the resource ids (what audio_play() / ne_find() want). The "#NNNNN" is
 * the EXE's decimal WAVE number; the referee row is cited in the comment.
 *
 * Only HIGH-confidence rows with a locatable port sim point are wired,
 * plus the ambient background pool (referee event #24, MED confidence:
 * its category selection is UNPROVEN — see ambient_tick in main.c).
 */
#ifndef AUDIO_EVENTS_H
#define AUDIO_EVENTS_H

#define SND_CASH        0xA71D  /* #10013 cash "ka-ching"        referee row 1  */
#define SND_EXPLOSION   0xA714  /* #10004 bomb EXPLOSION (once at detonation)   */
#define SND_EVENT_OK    0xA71F  /* #10015 event accept/reward (bomb ransom paid)*/
#define SND_FIRE_LOOP   0xA719  /* #10009 fire crackle (loop while burning)     */
#define SND_BOMB_THREAT 0xA713  /* #10003 bomb-threat popup (once, at the offer) */
#define SND_BOMB_ARM    0xA710  /* #10000 bomb armed (refuse/deploy dialog)      */
#define SND_FIRE_START  0xA716  /* #10006 fire OUTBREAK alert (once, at ignition)*/
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
/* Row 21 CORRECTED 2026-08-10: FUN_11f8_35ac is DeleteTenant, not build
 * complete — (rand%2)+0x2714 booms on PROGRAMMATIC deletion (disasters).
 * Construction completion is silent; #7000 carries its own final thud. */
#define SND_DESTROY0    0xA714  /* #10004 tenant destroyed (rand; = explosion) */
#define SND_DESTROY1    0xA715  /* #10005 tenant destroyed (rand)              */
#define SND_DELETE      0x9B5B  /* #7003  demolish / delete      row 22         */
#define SND_STARTUP     0xCE20  /* #20000 startup / intro jingle row 23         */

/* Ambient background pool (referee event #24 + referee_ambient_timing).
 * Selected by the tenant type at a random on-screen cell; resource = id|0x8000.
 * The cinema 9xxx soundtrack pool (0xA329-0xA337) is keyed to movie_id inline
 * in ambient_tick(); the exact EXE sub-index stays undecoded. */
#define AMB_RESTAURANT_A 0x8568  /* #1384 restaurant murmur   */
#define AMB_RESTAURANT_B 0x8569  /* #1385 (also shop/fastfood) */
#define AMB_OFFICE       0x85A8  /* #1448 office ambience      */
#define AMB_HOTEL        0x8629  /* #1577 hotel (also condo)   */
#define AMB_CONDO_RARE   0x8628  /* #1576 condo, 1-in-10       */
#define AMB_SHOP_FF_B    0x8668  /* #1640 shop / fast food alt */
#define AMB_PARKING_A    0x86A8  /* #1704 parking              */
#define AMB_PARKING_B    0x86A9  /* #1705 parking alt          */
#define AMB_PARTY        0x8B28  /* #2856 party hall           */
/* Seasonal accents (referee_ambient_timing): the EXE's empty-probe branch.
 * Ids + season/time conditions HIGH-confidence; the [0xDD6C] fallback (#10002)
 * is undecoded and left out. */
#define AMB_SEASON_DAY   0xA71C  /* #10012 year-Q3, daytime    */
#define AMB_SEASON_EVE   0xA71B  /* #10011 year-Q4, evening    */
#define AMB_SEASON_SANTA 0xA712  /* #10002 while Santa flies (EXE [0xDD6C]) */

#endif /* AUDIO_EVENTS_H */
