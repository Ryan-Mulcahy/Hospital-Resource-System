// view3d_parts.h - the drawing pieces shared by the 3D files.
//
// view3d.c draws A&E and owns these helpers; rooms3d.c uses them to draw the
// rest of the ground floor in the same blocky style, floors3d.c the floors
// upstairs, and outside3d.c the outside of the building. Only the 3D files
// include this header.

#ifndef VIEW3D_PARTS_H
#define VIEW3D_PARTS_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// ---- Colours used only in 3D ----
#define C_SKIN      (Color){ 232, 190, 160, 255 }
#define C_HAIR      (Color){  70,  50,  40, 255 }
#define C_BEDFRAME  (Color){ 210, 216, 226, 255 }
#define C_MATTRESS  (Color){ 245, 248, 252, 255 }
#define C_WALL      (Color){ 238, 241, 245, 255 }   // upper wall, off-white
#define C_WALL_LOW  (Color){ 196, 226, 214, 255 }   // lower wall, hospital mint
#define C_STRIPE    (Color){  58, 150, 120, 255 }   // green stripe along the walls
#define C_DESK      (Color){ 150, 120,  95, 255 }
#define C_CHAIR     (Color){ 120, 135, 160, 255 }
#define C_TROUSERS  (Color){  45,  58,  80, 255 }
#define C_COAT      (Color){ 248, 248, 250, 255 }
#define C_SCRUBCAP  (Color){ 120, 165, 225, 255 }
#define C_STEEL     (Color){ 170, 178, 190, 255 }
#define C_DARK      (Color){  40,  44,  52, 255 }
#define C_CURTAIN   (Color){ 176, 214, 214, 255 }
#define C_ASPHALT   (Color){  92,  97, 105, 255 }
#define C_HIVIS     (Color){ 222, 226,  40, 255 }   // ambulance yellow
#define C_AMB_GREEN (Color){  40, 140,  70, 255 }   // ambulance check pattern
#define C_GLASS     (Color){ 170, 210, 235, 110 }   // see-through door glass
#define C_RECEPTION (Color){ 135,  95, 170, 255 }   // receptionist's top

// Which way a person (or a piece of furniture) faces. SOUTH is towards the camera (+z).
typedef enum { FACE_SOUTH, FACE_NORTH, FACE_EAST, FACE_WEST } Facing;

// ---- The building ----
// Three floors, each FLOOR_H high: the ground floor (A&E and the rooms round
// it), the first floor and the second floor. Upstairs covers the rectangle
// BUILDING_X0..X1 by BUILDING_Z0..Z1; the ground floor is the same except
// that A&E's front wall is further back (z = AE_FRONT_Z), so the floors above
// overhang the front of A&E like a canopy.
#define FLOOR_H       3.2f
#define BUILDING_X0 -16.5f
#define BUILDING_X1   8.1f
#define BUILDING_Z0 -13.0f
#define BUILDING_Z1   7.6f
#define AE_FRONT_Z    6.2f
// The lifts: at the far end of the corridor beside A&E, on every floor.
// Patients walk in at the bottom, ride up, and walk out upstairs.
#define LIFT_X       -8.9f   // the middle of the lift, and of the corridor
#define LIFT_DOOR_Z   6.4f   // the lift doors (the lift is behind them, to +z)
#define LIFT_Z        7.0f   // where a patient stands in the lift
#define VIEW_OUTSIDE  FLOORS // the "floor" number for the view of the outside of the building

// What a person is wearing and carrying.
typedef struct {
    Color top, legs;
    bool coat;          // doctor's long white coat
    bool stethoscope;
    bool cap;           // nurse's scrub cap
    bool clipboard;
} Outfit;

Vector3 v3(float x, float y, float z);

// Colour with a brightness factor, darker and bluer at night.
Color shade(Color c, float f);

// One flat four-sided face, and boxes shaded to fake light from above.
void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col);
void draw_box(Vector3 p, Vector3 size, Color c);
void draw_lit_box(Vector3 p, Vector3 size, Color c);      // not darkened at night

// A soft shadow on the floor, and a pool of light (inside BLEND_ADDITIVE).
void shadow(float x, float z, float w, float d);
void glow(float x, float z, float radius, Color c);

// Signs: a picture with text on it, stretched over four corners.
RenderTexture2D make_sign(const char *text, int w, int h, int size, Color bg, Color fg);
void sign_quad(Texture2D t, Vector3 tl, Vector3 tr, Vector3 br, Vector3 bl);
void sign_facing_front(Texture2D t, float x, float y, float z, float w, float h);

// Furniture and people.
void draw_person(Vector3 p, Facing face, Outfit o, bool walking, float phase);
void draw_monitor(float x, float z, const Patient *p);   // p == NULL: screen off
void draw_wall(Vector3 centre, float lengthX, float lengthZ, float height, bool low);
void draw_clock(float x, float y, float z, int hour, int minute);
void draw_plant(float x, float z);

// True while a patient is walking somewhere (view3d.c draws them walking, so
// the place they're heading for is drawn empty until they get there).
bool view3d_patient_walking(int id);

// The time of day as seen in 3D: 0 in daylight up to 1 in the middle of the
// night (it fades in at dusk), and whether the lamps are on.
float view3d_darkness(void);
bool  view3d_lamps_on(void);
float view3d_hour(void);         // the time of day in hours, 0 to 24 (14.5 is 14:30)
float view3d_rain(void);         // 0 dry, up to 1 pouring

// A piece of equipment (EquipType) standing at p: a heart monitor on its
// stand, a ventilator, a defibrillator trolley, a drip stand with its pump,
// or a wheelchair. `f` is the way it faces.
void draw_equipment_item(int e, Vector3 p, Facing f);

// ---- Furniture and people from rooms3d.c, for floors3d.c ----
// A box placed relative to `base` in a piece's own directions (lx to the
// side, lz to the front), turned to face `f`.
void part_box(Vector3 base, Facing f, float lx, float y, float lz, float sx, float sy, float sz, Color c);
void part_shadow(Vector3 p, Facing f, float w, float d);
void furniture_bed(Vector3 p, Facing f);                    // hospital bed, head to the back
void furniture_chair(Vector3 p, Facing f, Color c);
void furniture_computer(Vector3 p, Facing f, float deskY);
void furniture_trolley(Vector3 p, Facing f, float w, float d);
void furniture_drip(float x, float z);
void furniture_desk(Vector3 p, bool mug);                   // office desk with a computer
void furniture_bin(float x, float z, float h);              // yellow clinical waste bin
void floor_tiles(float x0, float z0, float x1, float z1, Color a, Color b);
void person_seated(Vector3 p, Facing f, Color top, Color legs, Color hair, bool typing);
void person_lying(Vector3 p, Facing f, float y, float lz, Color cover, Color hair);
void person_masked(Vector3 p, Facing f, Outfit o);           // theatre staff: cap, mask and gloves

#endif