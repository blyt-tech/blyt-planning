/* doom_tick_c.c — native C twin of benchmarks/doom_tick.lua.
 *
 * Same algorithm: per-mob state machine, sqrt distance check, projectile
 * spawn/expire.  Compiled directly to RV32IMFC and run under rv32emu —
 * no Lua VM in the loop.  This is the comparison point for "what does
 * the same game logic cost when written in the language the platform
 * was actually targeted at?"
 *
 * The native version uses a fixed-size projectile pool with swap-with-
 * last removal, which is the C-game-code idiom (Doom's mobj list works
 * almost identically).  The Lua version's table.insert/table.remove
 * pays for table-grow allocator churn that idiomatic C avoids.
 */

#include <stdint.h>
#include <stddef.h>

/* From lua_runtime.c — also used by the Lua carts. */
uint64_t now_ns(void);
int      printf(const char *, ...);
float    sqrtf(float);

#ifndef N_MOBS
#define N_MOBS 64
#endif
#define NFRAMES         100
#define MAX_PROJECTILES 256

#define STATE_IDLE   1
#define STATE_CHASE  2
#define STATE_ATTACK 3
#define STATE_DEAD   4

#define PLAYER_X       64.0f
#define PLAYER_Y       64.0f
#define SIGHT_RANGE    40.0f
#define ATTACK_RANGE   12.0f
#define ATTACK_PERIOD  8
#define CHASE_SPEED    1.5f
#define PROJ_SPEED     4.0f
#define PROJ_TTL       30

typedef struct {
    float x, y, vx, vy;
    int   hp;
    int   state;
    int   tics;
    int   alive;
} mob_t;

typedef struct {
    float x, y, vx, vy;
    int   ttl;
} proj_t;

static mob_t  mobs[N_MOBS];
static proj_t projs[MAX_PROJECTILES];
static int    n_projs = 0;

static uint32_t seed = 12345;
static float lrand(void)
{
    seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
    return (float)seed / 2147483647.0f;
}

static void spawn_projectile(const mob_t *from)
{
    if (n_projs >= MAX_PROJECTILES) return;
    float dx = PLAYER_X - from->x;
    float dy = PLAYER_Y - from->y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) d = 1.0f;
    proj_t *p = &projs[n_projs++];
    p->x   = from->x;
    p->y   = from->y;
    p->vx  = dx / d * PROJ_SPEED;
    p->vy  = dy / d * PROJ_SPEED;
    p->ttl = PROJ_TTL;
}

static void tick_mob(mob_t *m)
{
    if (!m->alive) return;
    m->tics++;
    float dx   = PLAYER_X - m->x;
    float dy   = PLAYER_Y - m->y;
    float dist = sqrtf(dx * dx + dy * dy);

    switch (m->state) {
    case STATE_IDLE:
        if (dist < SIGHT_RANGE) { m->state = STATE_CHASE; m->tics = 0; }
        break;
    case STATE_CHASE:
        if (dist < ATTACK_RANGE) { m->state = STATE_ATTACK; m->tics = 0; }
        else {
            if (dist > 0.001f) {
                m->vx = dx / dist * CHASE_SPEED;
                m->vy = dy / dist * CHASE_SPEED;
            }
            m->x += m->vx;
            m->y += m->vy;
        }
        break;
    case STATE_ATTACK:
        if (dist > ATTACK_RANGE) m->state = STATE_CHASE;
        else if ((m->tics % ATTACK_PERIOD) == 0) spawn_projectile(m);
        break;
    case STATE_DEAD:
        m->alive = 0;
        break;
    }
}

static void tick_projectiles(void)
{
    /* swap-with-last removal: O(1) per remove, O(n) per pass. */
    int i = 0;
    while (i < n_projs) {
        proj_t *p = &projs[i];
        p->x  += p->vx;
        p->y  += p->vy;
        p->ttl--;
        if (p->ttl <= 0 || p->x < 0 || p->x > 128 || p->y < 0 || p->y > 128) {
            projs[i] = projs[--n_projs];
        } else {
            i++;
        }
    }
}

static void maybe_kill_one(void)
{
    if (lrand() < 0.02f) {
        int idx = (int)(lrand() * (float)N_MOBS);
        if (idx < 0) idx = 0;
        if (idx >= N_MOBS) idx = N_MOBS - 1;
        if (mobs[idx].alive) mobs[idx].state = STATE_DEAD;
    }
}

int main(void)
{
    for (int i = 0; i < N_MOBS; i++) {
        mobs[i].x     = lrand() * 128.0f;
        mobs[i].y     = lrand() * 128.0f;
        mobs[i].vx    = 0.0f;
        mobs[i].vy    = 0.0f;
        mobs[i].hp    = 10;
        mobs[i].state = STATE_IDLE;
        mobs[i].tics  = 0;
        mobs[i].alive = 1;
    }

    const int FRAMES = 30;
    uint64_t min_us = (uint64_t)-1, max_us = 0, sum_us = 0;

    for (int f = 0; f < FRAMES; f++) {
        uint64_t t0 = now_ns();
        for (int t = 0; t < NFRAMES; t++) {
            for (int i = 0; i < N_MOBS; i++) tick_mob(&mobs[i]);
            tick_projectiles();
            maybe_kill_one();
        }
        uint64_t t1 = now_ns();
        uint64_t us = (t1 - t0) / 1000ULL;
        printf("FRAME doom_tick_c %d %lu\n", f, (unsigned long)us);
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        sum_us += us;
    }
    printf("SUMMARY doom_tick_c frames=%d min=%lu max=%lu mean=%lu\n",
           FRAMES, (unsigned long)min_us, (unsigned long)max_us,
           (unsigned long)(sum_us / FRAMES));
    return 0;
}
