/* pixelduel -- central tunables.
 *
 * Everything that shapes "game feel" lives here so we can tune the fight
 * without touching logic. Numbers are in arena pixels (320x180 canvas) and
 * seconds.
 */
#ifndef PD_CONFIG_H
#define PD_CONFIG_H

/* --- canvas --- */
#define PD_VIEW_W 320
#define PD_VIEW_H 180
#define PD_WALL 8 /* wall thickness around the arena */

/* --- timing --- */
#define PD_TICK_HZ 60
#define PD_TICK_DT (1.0f / (float)PD_TICK_HZ)

/* --- player movement --- */
#define PD_PLAYER_SIZE 14.0f
#define PD_PLAYER_SPEED 95.0f

/* --- dash / dodge roll ---
 * Proto Guild-Wars-2 dodge roll: grants brief i-frames while rolling.
 * TODO(later): energy cost instead of a raw cooldown, GW2-style.
 */
#define PD_DASH_SPEED 340.0f
#define PD_DASH_TIME 0.12f    /* seconds spent locked in the roll */
#define PD_DASH_COOLDOWN 0.9f /* stands in for the future energy cost */
#define PD_DASH_IFRAMES 0.14f /* i-frames granted by the roll */

/* --- sword stab --- */
#define PD_ATTACK_REACH 12.0f   /* how far the blade extends past the body */
#define PD_ATTACK_ACTIVE 0.10f  /* seconds the blade can actually hit */
#define PD_ATTACK_ANIM 0.22f    /* total animation length */
#define PD_ATTACK_COOLDOWN 0.42f
#define PD_ATTACK_DAMAGE 1

/* --- damage & feedback --- */
#define PD_PLAYER_MAX_HP 5
#define PD_KNOCKBACK_SPEED 230.0f  /* impulse applied to the victim */
#define PD_KNOCKBACK_FRICTION 9.0f /* how fast knockback bleeds off */
#define PD_IFRAME_TIME 0.6f        /* invulnerability after being hit */
#define PD_HITSTOP_TIME 0.06f      /* global freeze frames on a hit */
#define PD_SHAKE_TIME 0.15f
#define PD_SHAKE_MAG 3.0f          /* shake amplitude in arena pixels */

#endif /* PD_CONFIG_H */
