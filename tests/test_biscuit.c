#include "gameplay.h"
#include "ame/events.h"
#include "ame/geo.h"
#include "level_gen.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL biscuit: %s\n", m);
    return 1;
}

static int n_pick, n_mine, n_jump, n_sw, n_win;

static void on_ev(const ame_event *e, void *u)
{
    (void)u;
    if (e->kind == BF_EV_PICKUP) n_pick++;
    if (e->kind == BF_EV_MINE) n_mine++;
    if (e->kind == BF_EV_JUMP) n_jump++;
    if (e->kind == BF_EV_SWITCH) n_sw++;
    if (e->kind == BF_EV_WIN) n_win++;
}

static void ticks(double *t, int n, float dt)
{
    for (int i = 0; i < n; i++) {
        *t += dt;
        bf_tick(dt, *t);
    }
}

int main(void)
{
    ame_events_reset();
    ame_events_subscribe(BF_EV_PICKUP, on_ev, NULL);
    ame_events_subscribe(BF_EV_MINE, on_ev, NULL);
    ame_events_subscribe(BF_EV_JUMP, on_ev, NULL);
    ame_events_subscribe(BF_EV_SWITCH, on_ev, NULL);
    ame_events_subscribe(BF_EV_WIN, on_ev, NULL);

    /* circle vs aabb query used by the wheels */
    ame_aabb ground = ame_aabb_make(0, -0.5f, 0, 4, 0.5f, 1);
    float nx, ny, pen;
    if (!ame_geo_circle_aabb_xy(&ground, 0, 0.2f, 0.32f, &nx, &ny, &pen))
        return fail("circle should hit ground");
    if (ny < 0.5f) return fail("push should be mostly +Y");
    if (ame_geo_circle_aabb_xy(&ground, 0, 3.0f, 0.32f, &nx, &ny, &pen))
        return fail("circle far miss");

    if (level_n_seg < 8) return fail("generated segs");
    if (level_n_marker < 10) return fail("generated markers");
    {
        int cover = 0;
        for (int i = 0; i < level_n_seg; i++) {
            const LevelSeg *sg = &level_segs[i];
            if (sg->x0 <= 0.0f && sg->x1 >= 0.0f && fabsf(sg->y0) < 0.05f
                && fabsf(sg->y1) < 0.05f)
                cover = 1;
        }
        if (!cover) return fail("generated ground at x=0");
    }

    bf_reset(1);
    bf_skip_dialogue();
    double t = 0;
    ticks(&t, 90, 1.0f / 60.0f);
    BfSnap s;
    bf_snapshot(&s);
    if (s.wheel_r < 0.2f) return fail("round wheels");
    if (fabsf(s.wheel_x[0] - s.wheel_x[1]) < 0.4f)
        return fail("two axles");
    if (!s.wheel_ground[0] || !s.wheel_ground[1]) {
        fprintf(stderr, "gnd %d %d wy %.2f %.2f cy %.2f\n",
                s.wheel_ground[0], s.wheel_ground[1],
                s.wheel_y[0], s.wheel_y[1], s.car_y);
        return fail("both wheels grounded after settle");
    }
    /* chassis rides above the wheel hubs */
    float wavg = 0.5f * (s.wheel_y[0] + s.wheel_y[1]);
    if (s.car_y < wavg + 0.15f) return fail("suspension ride height");
    float gap = (s.car_y - s.car_h * 0.5f) - 0.0f;
    if (gap < 0.05f) return fail("chassis should not sit on the dirt (wheels do)");

    float x0 = s.car_x, fuel0 = s.fuel;
    bf_hold_accel(1);
    ticks(&t, 90, 1.0f / 60.0f);
    bf_snapshot(&s);
    if (s.car_x < x0 + 0.8f) {
        fprintf(stderr, "drove to %.2f from %.2f\n", s.car_x, x0);
        return fail("motor through wheels");
    }
    if (s.fuel >= fuel0 - 0.5f) return fail("fuel burn");
    if (fabsf(s.wheel_spin[0]) < 0.2f && fabsf(s.wheel_spin[1]) < 0.2f)
        return fail("wheels should spin");

    /* keep driving: pickup around x=8, mine around x=18 */
    ticks(&t, 240, 1.0f / 60.0f);
    ame_events_drain();
    bf_snapshot(&s);
    if (n_pick < 1) {
        fprintf(stderr, "x=%.2f fuel=%.1f pick=%d\n", s.car_x, s.fuel, n_pick);
        return fail("biscuit pickup");
    }
    if (s.n_spawn < 2) return fail("spawn pads");

    bf_teleport(12.0f, 1.15f);
    ticks(&t, 20, 1.0f / 60.0f);
    ame_events_drain();
    bf_snapshot(&s);
    if (s.spawn_i < 1) {
        fprintf(stderr, "spawn_i=%d x=%.2f\n", s.spawn_i, s.car_x);
        return fail("checkpoint");
    }

    /* shelf jump-cookie (jam enableJump) */
    bf_teleport(12.0f, 3.35f);
    ticks(&t, 50, 1.0f / 60.0f);
    ame_events_drain();
    bf_skip_dialogue();
    bf_snapshot(&s);
    if (!s.car_jump) return fail("jump cookie unlocks car hop");
    if (s.wheel_ground[0] || s.wheel_ground[1]) {
        float y0 = s.car_y;
        bf_request_jump();
        ticks(&t, 8, 1.0f / 60.0f);
        ame_events_drain();
        bf_snapshot(&s);
        if (s.car_y <= y0) return fail("car hop");
    }

    /* mine: warp onto it if we skipped the gap */
    float hp0 = s.hp;
    bf_teleport(18.5f, 0.90f);
    ticks(&t, 50, 1.0f / 60.0f);
    ame_events_drain();
    bf_snapshot(&s);
    if (n_mine < 1 && s.hp >= hp0) {
        fprintf(stderr, "hp %.1f mine events %d\n", s.hp, n_mine);
        return fail("cookie mine");
    }

    bf_hold_accel(0);
    bf_request_switch();
    ame_events_drain();
    bf_snapshot(&s);
    if (s.mode != BF_MODE_HUMAN) return fail("switch to human");
    if (s.human_hidden) return fail("human visible");
    if (n_sw < 1) return fail("switch event");

    ticks(&t, 40, 1.0f / 60.0f); /* land */
    bf_hold_move(1);
    bf_request_jump();
    ticks(&t, 8, 1.0f / 60.0f);
    ame_events_drain();
    bf_snapshot(&s);
    if (n_jump < 1) return fail("jump event");
    if (s.human_y < 0.4f) return fail("human left the ground");

    bf_hold_move(0);
    bf_teleport(44.0f, 1.3f);
    ticks(&t, 30, 1.0f / 60.0f);
    ame_events_drain();
    bf_snapshot(&s);
    if (!s.won && n_win < 1) {
        fprintf(stderr, "won=%d n_win=%d x=%.2f\n", s.won, n_win, s.car_x);
        return fail("goal");
    }

    printf("test_biscuit ok\n");
    return 0;
}
