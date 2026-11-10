#include "entities/human.h"
#include "abilities.h"
#include "entities/car.h"
#include "physics.h"

#include <string.h>

void human_init(Person *h)
{
    memset(h, 0, sizeof(*h));
    h->w = 0.50f;
    h->h = 0.95f;
    h->hidden = 1;
    h->facing = 1;
    h->max_hp = 100.0f;
    h->hp = 100.0f;
}

void human_hide(Person *h, int hide)
{
    h->hidden = hide ? 1 : 0;
}

void human_apply_damage(Person *h, float dmg)
{
    h->hp -= dmg;
}

int human_try_jump(Person *h)
{
    if (h->hidden) return 0;
    if (h->grounded) {
        h->vy = ABILITY_HUM_JUMP;
        h->grounded = 0;
        return 1;
    }
    if (h->wall != 0) {
        h->vx = -9.5f * (float)h->wall;
        h->vy = ABILITY_HUM_JUMP * 0.92f;
        h->lock = 0.22f;
        return 1;
    }
    return 0;
}

void human_step(Person *h, const struct Chassis *car, struct PhysWorld *world,
                int move, float dt)
{
    if (h->hidden) {
        h->x = car->x;
        h->y = car->y + 0.4f;
        h->vx = car->vx;
        h->vy = car->vy;
        h->grounded = 0;
        h->wall = 0;
        return;
    }
    if (h->lock > 0.0f) {
        h->lock -= dt;
        if (h->lock < 0.0f) h->lock = 0.0f;
    }
    if (h->lock <= 0.0f) {
        h->vx = ABILITY_HUM_SPD * (float)move;
        if (move > 0) h->facing = 1;
        if (move < 0) h->facing = -1;
    }
    h->vy += GRAV * dt;
    h->x += h->vx * dt;
    h->y += h->vy * dt;
    phys_aabb_world(world, &h->x, &h->y, &h->vx, &h->vy,
                    h->w * 0.5f, h->h * 0.5f, &h->grounded, &h->wall, 1);
    {
        float fx = h->x;
        float fy = h->y - h->h * 0.5f + 0.16f;
        float fr = 0.16f;
        int sg = 0;
        float nx = 0.0f, ny = 1.0f;
        if (phys_circle_segs(world, &fx, &fy, &h->vx, &h->vy, fr, &sg, &nx, &ny)) {
            h->x = fx;
            h->y = fy + h->h * 0.5f - 0.16f;
            if (sg) h->grounded = 1;
        }
    }
}
