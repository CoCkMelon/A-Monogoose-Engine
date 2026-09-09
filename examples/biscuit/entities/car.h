#ifndef BF_CAR_H
#define BF_CAR_H

/*
 * Car body + round wheels. Chassis is an AABB (walls/ceiling only).
 * Wheels are circles on spring-damper struts. No Box2D.
 *
 * Layout: Chassis / Wheel leading fields match ame_phys_body / ame_phys_wheel
 * so library strut/collision helpers can cast the prefix safely. Game-only
 * fields (hp, fuel, spin) stay after that prefix.
 */

#define BODY_W      1.70f
#define BODY_H      0.95f
#define WHEEL_R     0.32f
#define AXLE_B      (-0.42f)
#define AXLE_F      (0.58f)
#define REST_LEN    0.52f
#define SUSP_MIN    0.30f
#define SUSP_MAX    0.88f
#define SUSP_K      220.0f
#define SUSP_D      16.0f
#define BODY_MASS   3.6f
#define WHEEL_MASS  0.48f
#define BODY_I      1.55f
#define MOTOR_F     11.0f
#define MOTOR_W     24.0f
#define GYRO        12.0f
#define GRAV        (-32.0f)

enum { N_W = 2 };

typedef struct Chassis {
    float x, y, vx, vy;
    float a, omega;
    float w, h;
    float mass, I;
    float hp, max_hp;
    float fuel, max_fuel;
} Chassis;

typedef struct Wheel {
    /* ame_phys_wheel prefix — keep order/size in sync with include/ame/phys.h */
    float lx;           /* body-space axle x */
    float x, y, vx, vy;
    float r, mass;
    int   grounded;
    float nx, ny;
    /* biscuit-only */
    float spin, spin_vel;
} Wheel;


typedef struct ame_phys_world PhysWorld; /* full type in physics.h / ame/phys.h */

void car_init(Chassis *c, Wheel *wheels);
void car_seat_wheels(Chassis *c, Wheel *wheels);
void car_step(Chassis *c, Wheel *wheels, PhysWorld *world,
              int driving, int accel, int yaw, int boost, float dt);
void car_apply_damage(Chassis *c, float dmg);
void car_refuel(Chassis *c, float amount);
int  car_try_hop(Chassis *c, Wheel *wheels);

#endif
