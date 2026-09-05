#include "entities/car.h"
#include "abilities.h"
#include "gameplay.h"
#include "physics.h"
#include "config.h"

#include <string.h>

void car_init(Chassis *c, Wheel *wheels)
{
    memset(c, 0, sizeof(*c));
    memset(wheels, 0, sizeof(Wheel) * N_W);
    c->w = BODY_W;
    c->h = BODY_H;
    c->mass = BODY_MASS;
    c->I = BODY_I;
    c->max_hp = 200.0f;
    c->hp = 200.0f;
    c->max_fuel = 100.0f;
    c->fuel = 100.0f;
    wheels[0].lx = AXLE_B;
    wheels[1].lx = AXLE_F;
    for (int i = 0; i < N_W; i++) {
        wheels[i].r = WHEEL_R;
        wheels[i].mass = WHEEL_MASS;
    }
}

void car_seat_wheels(Chassis *c, Wheel *wheels)
{
    float fx, fy, ux, uy;
    phys_body_axes(c, &fx, &fy, &ux, &uy);
    for (int i = 0; i < N_W; i++) {
        Wheel *w = &wheels[i];
        float ax = c->x + fx * w->lx;
        float ay = c->y + fy * w->lx;
        w->x = ax - ux * REST_LEN;
        w->y = ay - uy * REST_LEN;
        w->vx = c->vx;
        w->vy = c->vy;
        w->grounded = 0;
    }
}

void car_apply_damage(Chassis *c, float dmg)
{
    c->hp -= dmg;
}

void car_refuel(Chassis *c, float amount)
{
    c->fuel += amount;
    if (c->fuel > c->max_fuel) c->fuel = c->max_fuel;
}

static void motor_wheels(Chassis *car, Wheel *wheels, int driving,
                         int accel, int boost, float dt)
{
    float mul = ability_boost_mul(boost, car->fuel);
    if (!driving) accel = 0;
    if (car->fuel <= 0.0f) accel = 0;
    if (accel != 0) {
        float use = ability_fuel_use(boost, dt);
        car->fuel -= use;
        if (car->fuel < 0.0f) car->fuel = 0.0f;
    }
    float fx, fy, ux, uy;
    phys_body_axes(car, &fx, &fy, &ux, &uy);
    for (int i = 0; i < N_W; i++) {
        Wheel *w = &wheels[i];
        if (w->grounded && accel != 0 && car->fuel > 0.0f) {
            float tx = -w->ny, ty = w->nx;
            if (tx * fx + ty * fy < 0.0f) { tx = -tx; ty = -ty; }
            float F = (float)accel * MOTOR_F * mul;
            w->vx += tx * F / w->mass * dt;
            w->vy += ty * F / w->mass * dt;
            w->spin_vel = -(float)accel * MOTOR_W * mul;
        } else if (w->grounded) {
            float tx = -w->ny, ty = w->nx;
            float vt = w->vx * tx + w->vy * ty;
            w->spin_vel = -(w->r > 0.05f ? vt / w->r : 0.0f);
            w->vx -= tx * vt * 1.8f * dt;
            w->vy -= ty * vt * 1.8f * dt;
        } else {
            w->spin_vel *= (1.0f - 1.2f * dt);
        }
        w->spin += w->spin_vel * dt;
    }
}

void car_step(Chassis *car, Wheel *wheels, struct PhysWorld *world,
              int driving, int accel, int yaw, int boost, float dt)
{
    car->vy += GRAV * dt;
    for (int i = 0; i < N_W; i++)
        wheels[i].vy += GRAV * dt;

    if (driving)
        car->omega += -(float)yaw * GYRO * dt;

    phys_strut_forces(car, wheels, N_W, dt);
    motor_wheels(car, wheels, driving, accel, boost, dt);

    car->x += car->vx * dt;
    car->y += car->vy * dt;
    car->a += car->omega * dt;
    for (int i = 0; i < N_W; i++) {
        wheels[i].x += wheels[i].vx * dt;
        wheels[i].y += wheels[i].vy * dt;
    }

    phys_strut_lateral(car, wheels, N_W);
    for (int i = 0; i < N_W; i++) {
        Wheel *w = &wheels[i];
        phys_circle_world(world, &w->x, &w->y, &w->vx, &w->vy, w->r,
                          &w->grounded, &w->nx, &w->ny);
    }
    phys_strut_limits(car, wheels, N_W);
    phys_strut_lateral(car, wheels, N_W);
    int dummy_g = 0, dummy_w = 0;
    phys_aabb_world(world, &car->x, &car->y, &car->vx, &car->vy,
                    car->w * 0.5f, car->h * 0.5f, &dummy_g, &dummy_w, 0);

    if (wheels[0].grounded && wheels[1].grounded) {
        car->omega += -car->a * 22.0f * dt;
        car->omega *= (1.0f - 4.0f * dt);
    } else {
        car->omega *= (1.0f - 0.6f * dt);
    }
    if (car->a > 3.5f) car->a = 3.5f;
    if (car->a < -3.5f) car->a = -3.5f;
}

int car_try_hop(Chassis *c, Wheel *wheels)
{
    if (!wheels[0].grounded && !wheels[1].grounded) return 0;
    c->vy += CAR_HOP_IMPULSE;
    for (int i = 0; i < N_W; i++)
        wheels[i].vy += CAR_HOP_IMPULSE;
    return 1;
}
