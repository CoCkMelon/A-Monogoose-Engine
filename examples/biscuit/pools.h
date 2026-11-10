#ifndef BF_POOLS_H
#define BF_POOLS_H

/* Biscuit pool slot types: the trunk pool TEMPLATE (ame/pool.h) expanded
 * once here so sim.h can hold the slot structs inside BfSim.
 *
 * Only TYPES and static-inline ops land in this header - no state. The
 * slot INSTANCES live in BfSim (game-owned), exactly as data.txt wants:
 * a pool module owns its arrays, and here the "module" is the game.
 *
 *   bf_fuel_slots  — cookies / jump cookies (pickups)
 *   bf_mine_slots  — mines
 */
#include "gameplay.h" /* BF_MAX_FUEL / BF_MAX_MINE */

#define AME_POOL_PREFIX bf_fuel_
#define AME_POOL_CAP BF_MAX_FUEL
#include <ame/pool.h>
#undef AME_POOL_PREFIX
#undef AME_POOL_CAP

#define AME_POOL_PREFIX bf_mine_
#define AME_POOL_CAP BF_MAX_MINE
#include <ame/pool.h>
#undef AME_POOL_PREFIX
#undef AME_POOL_CAP

#endif /* BF_POOLS_H */
