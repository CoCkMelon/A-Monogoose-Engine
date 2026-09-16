/* ame-next — convenient public umbrella header.
 *
 * This includes the ordinary runtime modules in both AME_2D and AME_3D
 * builds.  pool.h is intentionally not included: it is a macro template and
 * must be included at the point where a game defines its pool type.
 *
 * For a no-state/common-types-only include, use <ame/ame.h>.
 */
#ifndef AME_AME_ALL_H
#define AME_AME_ALL_H

#include <ame/ame.h>
#include <ame/app.h>
#include <ame/audio.h>
#include <ame/audio_ray.h>
#include <ame/camera.h>
#include <ame/dialogue.h>
#include <ame/events.h>
#include <ame/geometry.h>
#include <ame/input.h>
#include <ame/math.h>
#include <ame/particles.h>
#include <ame/render.h>
#include <ame/text.h>
#include <ame/tilemap.h>

#endif /* AME_AME_ALL_H */
