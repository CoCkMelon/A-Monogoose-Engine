/* test_biscuit_sim — Biscuit Fuel's sim on trunk, headless and GL-free.
 *
 * The "playable on trunk" bar as a ctest: load the level (markers via OBJ
 * object names -> prefab), drive the car, burn fuel, reach the goal. No
 * renderer involved; the render module is proven by the headless
 * screenshot smoke in CI.
 */
#include "config.h"
#include "gameplay.h"

#include <stdio.h>

#include "utest.h"

static void tick_n(int n, float *t)
{
    for (int i = 0; i < n; i++) {
        *t += 1.0f / 60.0f;
        bf_tick(1.0f / 60.0f, *t);
    }
}

int main(void)
{
    printf("=== test_biscuit_sim (Biscuit Fuel on trunk) ===\n");
    float t = 0;

    UT_CASE("level loads: markers spawned from OBJ object names");
    {
        bf_reset(1);
        bf_skip_dialogue();
        BfSnap s;
        bf_snapshot(&s);
        UT_ASSERTF(s.n_fuel > 0, "no cookies spawned (n_fuel=%d)", s.n_fuel);
        UT_ASSERTF(s.n_mine > 0, "no mines spawned");
        UT_ASSERTF(s.n_saw > 0, "no saws spawned");
        UT_ASSERTF(s.n_spawn > 0, "no checkpoints spawned");
        UT_ASSERTF(s.n_plat > 0, "no platform colliders");
        UT_ASSERT(s.goal_w > 0.1f && s.max_fuel > 0.0f);
        printf("    markers: fuel=%d mine=%d saw=%d spawn=%d plat=%d\n",
               s.n_fuel, s.n_mine, s.n_saw, s.n_spawn, s.n_plat);
        UT_OK();
    }

    UT_CASE("car rests on its wheels, then drives and burns fuel");
    {
        tick_n(90, &t);
        BfSnap s;
        bf_snapshot(&s);
        UT_ASSERTF(s.wheel_r > 0.1f, "wheels not round");
        UT_ASSERTF(s.wheel_ground[0] && s.wheel_ground[1],
                   "wheels not grounded (y=%f %f car=%f)", s.wheel_y[0],
                   s.wheel_y[1], s.car_y);
        UT_ASSERTF(s.car_y > s.wheel_y[0], "chassis not on suspension");
        float x0 = s.car_x, fuel0 = s.fuel;
        bf_hold_accel(1);
        tick_n(120, &t);
        bf_snapshot(&s);
        UT_ASSERTF(s.car_x > x0 + 0.4f, "car did not drive (x=%f from %f)",
                   s.car_x, x0);
        UT_ASSERTF(s.fuel < fuel0, "fuel did not burn (%f >= %f)", s.fuel,
                   fuel0);
        bf_hold_accel(0);
        printf("    drove to x=%.2f, fuel %.1f -> %.1f\n", s.car_x, fuel0,
               s.fuel);
        UT_OK();
    }

    UT_CASE("goal loop: reaching the gate wins");
    {
        BfSnap s;
        bf_snapshot(&s);
        bf_teleport(s.goal_x, s.goal_y);
        tick_n(30, &t);
        bf_snapshot(&s);
        UT_ASSERTF(s.won == 1, "goal overlap did not set won");
        UT_OK();
    }

    UT_CASE("restart resets the run");
    {
        bf_request_restart();
        tick_n(2, &t);
        BfSnap s;
        bf_snapshot(&s);
        UT_ASSERT(s.won == 0);
        UT_ASSERT(s.fuel > 0.0f);
        UT_OK();
    }

    return ut_done("test_biscuit_sim");
}
