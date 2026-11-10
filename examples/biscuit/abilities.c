#include "abilities.h"

static int g_car_jump;

void abilities_init(void) { g_car_jump = 0; }
void abilities_reset(void) { g_car_jump = 0; }
int  ability_get_car_boost(void) { return 1; }
int  ability_get_car_jump(void) { return g_car_jump; }
void ability_set_car_jump(int v) { g_car_jump = v ? 1 : 0; }
int  ability_get_car_fly(void) { return 0; }

float ability_boost_mul(int boost_held, float fuel)
{
    return (boost_held && fuel > 0.0f) ? ABILITY_BOOST_MUL : 1.0f;
}

float ability_fuel_use(int boost_held, float dt)
{
    return ABILITY_FUEL_USE * (boost_held ? 1.5f : 1.0f) * dt;
}
