// view3d_parts.h - the drawing pieces shared by the 3D files.
//
// view3d.c draws A&E and owns these helpers; rooms3d.c uses them to draw the
// rest of the hospital in the same blocky style. Only the 3D files include
// this header.

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

#endif