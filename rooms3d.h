// rooms3d.h - the rest of the hospital in the 3D view: the ward, X-ray, MRI,
// bloods and weight, the operating theatre, the nursery and the admin office.
// view3d.c calls these while it draws A&E.

#ifndef ROOMS3D_H
#define ROOMS3D_H

#include "raylib.h"
#include "sim.h"

// Rooms are numbered 1 to ROOM_COUNT, the same as the number keys in the 3D view:
// 1 Ward, 2 X-ray, 3 MRI, 4 Bloods & weight, 5 Theatre, 6 Nursery, 7 Admin office.
#define ROOM_COUNT 7

// Make the room signs the first time. Call each frame before BeginMode3D().
void rooms3d_prepare(const Sim *sim);

// Draw the rooms, corridors and everyone in them. Call inside BeginMode3D().
// The camera decides which walls are cut down so they don't block the view.
void rooms3d_draw(const Sim *sim, Vector3 camPos, Vector3 camTarget);

// The see-through windows. Call after everything else has been drawn.
void rooms3d_draw_glass(void);

// Pools of light on the floors. Call inside BeginBlendMode(BLEND_ADDITIVE).
void rooms3d_lights(const Sim *sim);

// Free the signs. Call once before CloseWindow().
void rooms3d_close(void);

// The centre of a room (1 to ROOM_COUNT) and a good camera distance for it.
float rooms3d_focus(int room, Vector3 *centre);

// The name of the room at a point on the floor, e.g. "X-RAY", "A&E" or "CORRIDOR".
const char *rooms3d_name_at(float x, float z);

// Walking routes for real patients. Every route starts at the junction in the
// corridor just outside A&E's side door. rooms3d_path fills `pts` with the
// way from there to a place for a simulation room (RoomType): place 0, 1, ...
// is a place in the room; -1, -2, ... is a spot in its queue. Returns how
// many points it wrote (at most `max`).
Vector3 rooms3d_junction(void);
int rooms3d_path(int room, int place, Vector3 *pts, int max);

#endif