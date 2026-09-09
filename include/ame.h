#ifndef AME_H
#define AME_H

/*
 * A Mongoose Engine — public library umbrella.
 *
 * Link `ame` (static). You own the program entry and init order.
 * Nothing starts a window, audio device, or logic thread until YOU call it.
 *
 * Typical custom init (edit freely — this is not hidden inside the lib):
 *
 *   1. ame_settings_load_file(&S, "settings.yaml");
 *   2. ame_app_open(...)            // optional SDL/GL/audio host
 *      OR your own window + ame_gl_load(get_proc)
 *   3. build your pipeline / camera / meshes
 *   4. ame_input_open(handler, user) // optional asyncinput
 *   5. ame_logic_start(&L)          // optional fixed-step thread
 *   6. main loop: poll → copy snap → draw → swap
 *   7. ame_logic_stop / ame_app_close / ame_input_close
 *
 * Physics is NOT in the library — examples/biscuit owns its solver.
 * The library gives: pools, events, geo queries, batch renderer, audio
 * synth, settings YAML, logic-thread helper, snap seqlock, net framing.
 */

#include "ame/handle.h"
#include "ame/pool.h"
#include "ame/events.h"
#include "ame/math.h"
#include "ame/geo.h"
#include "ame/time.h"
#include "ame/snap.h"
#include "ame/settings.h"
#include "ame/logic.h"
#include "ame/camera.h"
#include "ame/gfx.h"
#include "ame/mesh.h"
#include "ame/text.h"
#include "ame/audio.h"
#include "ame/input.h"
#include "ame/app.h"
#include "ame/log.h"

#endif
