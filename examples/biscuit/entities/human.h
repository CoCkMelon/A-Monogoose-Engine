#ifndef BF_HUMAN_H
#define BF_HUMAN_H

/* On-foot avatar. AABB vs platforms (floors on). Hidden while seated. */

typedef struct Person {
    float x, y, vx, vy;
    float w, h;
    int hidden, facing, grounded, wall;
    float hp, max_hp;
    float lock;
} Person;


struct PhysWorld;
struct Chassis;

void human_init(Person *h);
void human_step(Person *h, const struct Chassis *car, struct PhysWorld *world,
                int move, float dt);
int  human_try_jump(Person *h); /* 1 if jumped */
void human_apply_damage(Person *h, float dmg);
void human_hide(Person *h, int hide);

#endif
