// floors3d.h - the two floors upstairs in the 3D view, and the lifts.
//
//   First floor  (diagnostics and critical care): CT scanner, ultrasound,
//                cardiology (the cath lab), ICU, pharmacy, the laboratory
//                and the staff room.
//   Second floor (wards and therapy): maternity, the children's ward,
//                physiotherapy, dialysis, the chapel and the cafe.
//
// Each floor has the same corridors as the ground floor: one beside A&E's
// left wall, with the lifts at its far end, and one along the back. Patients
// going upstairs walk to the lifts, ride up, and walk on from there. Like the
// other 3D files, this only reads the simulation.

#ifndef FLOORS3D_H
#define FLOORS3D_H

#include "raylib.h"
#include "sim.h"

// Make the signs the first time. Call each frame before BeginMode3D().
void floors3d_prepare(void);

// Draw one floor upstairs (1 or 2) with everyone in it, at its own height.
// Call inside BeginMode3D(). The camera decides which walls are cut down.
void floors3d_draw(const Sim *sim, int floor, Vector3 camPos, Vector3 camTarget);

// The see-through glass on that floor. Call after everything else.
void floors3d_draw_glass(int floor);

// Pools of light on that floor. Call inside BeginBlendMode(BLEND_ADDITIVE).
void floors3d_lights(const Sim *sim, int floor);

// The lifts on floor `floor` (0, 1 or 2): the shaft, the doors (open by
// `doorsOpen`, 0 to 1), the car if it's on this floor (`carY` is the height
// of its floor), the floor indicator, and a sign of what's on this floor.
void floors3d_draw_lift(int floor, float carY, float doorsOpen);

// Free the signs. Call once before CloseWindow().
void floors3d_close(void);

// The walk from the junction outside A&E's side door to a place in a room
// upstairs (a RoomType on floor 1 or 2): place 0, 1, ... is a place in the
// room; -1, -2, ... a spot in its queue. Down the corridor, up in the lift,
// and along to the room. Returns how many points it wrote (at most `max`).
int floors3d_path(int room, int place, Vector3 *pts, int max);

// The middle of a room upstairs (a RoomType), at its floor's height, and a
// good camera distance for it.
float floors3d_focus(int room, Vector3 *centre);

// The name of the place at (x, z) on floor 1 or 2, e.g. "ICU" or "CORRIDOR".
const char *floors3d_name_at(int floor, float x, float z);

// The number keys on a floor upstairs: 1, 2, 3... are its rooms in turn.
// How many there are, the short name for key k (e.g. "ICU"), and the
// middle of that room and a good camera distance for it.
int floors3d_key_count(int floor);
const char *floors3d_key_name(int floor, int k);
float floors3d_key_focus(int floor, int k, Vector3 *centre);

// What's on each floor, e.g. "FIRST FLOOR".
const char *floors3d_floor_name(int floor);

#endif
