#ifndef BF_ABILITIES_H
#define BF_ABILITIES_H

/*
 * Jam-shaped ability table. Boost is always available (burns biscuit fuel).
 * On-foot jump is always on. Car hop unlocks from the shelf jump-cookie.
 * Fly stays locked.
 */

#define ABILITY_BOOST_MUL 2.2f
#define ABILITY_FUEL_USE  7.0f
#define ABILITY_HUM_JUMP  13.2f
#define ABILITY_HUM_SPD   7.2f
#define ABILITY_SWITCH_R  2.6f

void abilities_init(void);
void abilities_reset(void);
int  ability_get_car_boost(void);
int  ability_get_car_jump(void);
void ability_set_car_jump(int v);
int  ability_get_car_fly(void);

float ability_boost_mul(int boost_held, float fuel);
float ability_fuel_use(int boost_held, float dt);

#endif
