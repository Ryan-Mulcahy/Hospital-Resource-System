// rooms3d.c - the rest of the hospital in 3D, built round the A&E department.
//
// view3d.c draws A&E: the bays, the waiting room and the ambulance bay. This
// file adds seven more rooms, joined to A&E by an L-shaped corridor:
//
//   behind A&E (north):  2 X-ray    3 MRI    5 Operating theatre
//   left of A&E (west):  1 Ward   4 Bloods & weight   6 Nursery   7 Admin office
//
//   z -13 +--------+--------+--------+--------+
//         | 1 WARD | 2 X-RAY| 3 MRI  |5 THEATRE
//         |        +--------+--------+--------+  corridor
//    -4.2 +--------+ |                         |
//         |4 BLOODS| |          A&E            |  ambulance
//    -0.2 +--------+ |   (drawn by view3d.c)   |     bay
//         |6 NURSERY |                         |
//     4.0 +--------+ |                         |
//         |7 ADMIN | |
//     7.6 +--------+-+
//      x -16.5   -9.7 -8.1                    8.1
//
// The walls between rooms are half height, like a doll's house, so you can
// see into every room from above. The outside walls stay full height except
// the ones between the camera and the building.
//
// The people here are dummies: they aren't part of the simulation, they just
// make the building feel lived in. Like view3d.c, this file only reads the
// simulation and never changes it.

#include <math.h>
#include <stddef.h>
#include <string.h>
#include "raylib.h"
#include "draw.h"            // colours
#include "view3d_parts.h"    // boxes, walls, people and signs from view3d.c
#include "rooms3d.h"

// ------------------------------------------------------------------------
// Layout (1 unit is roughly 1 metre)
// ------------------------------------------------------------------------

#define NORTH_Z     -13.0f   // back wall of the building
#define WEST_X      -16.5f   // left wall of the building
#define EAST_X        8.1f   // right wall of the back rooms, in line with A&E's
#define SOUTH_Z       7.6f   // front wall of the left wing
#define BACK_ROOMS_Z -6.7f   // front of X-ray, MRI and theatre
#define AE_BACK_Z    -5.2f   // back of A&E's back wall; the corridor runs between
#define WING_X       -9.7f   // right-hand side of the ward, bloods, nursery and admin
#define AE_LEFT_X    -8.1f   // A&E's left wall
#define OUTER_H       2.6f   // outside walls
#define PART_H        1.15f  // walls between rooms

typedef struct {
    const char *name;
    float x0, z0, x1, z1;    // the floor
    Color colour;            // its sign
    float camDist;           // how far back the camera sits for its number key
} Room;

enum { AE, WARD, XRAY, MRI, BLOODS, THEATRE, NURSERY, ADMIN };

static const Room ROOMS[ROOM_COUNT + 1] = {
    { "A&E",               -8.1f,  -5.1f,   8.1f,  6.2f, {  62, 116, 196, 255 }, 21.0f },
    { "WARD",             WEST_X, NORTH_Z, WING_X, -4.2f, {  62, 116, 196, 255 }, 15.0f },
    { "X-RAY",            WING_X, NORTH_Z, -3.3f, BACK_ROOMS_Z, { 120,  90, 170, 255 }, 13.0f },
    { "MRI",               -3.3f, NORTH_Z,  2.6f, BACK_ROOMS_Z, {  40, 150, 160, 255 }, 13.0f },
    { "BLOODS & WEIGHT",  WEST_X,  -4.2f, WING_X, -0.2f, { 200,  70,  80, 255 }, 12.0f },
    { "OPERATING THEATRE",  2.6f, NORTH_Z, EAST_X, BACK_ROOMS_Z, {  40, 140, 110, 255 }, 13.0f },
    { "NURSERY",          WEST_X,  -0.2f, WING_X,  4.0f, { 220, 110, 150, 255 }, 12.0f },
    { "ADMIN OFFICE",     WEST_X,   4.0f, WING_X, SOUTH_Z, {  90, 100, 120, 255 }, 12.0f },
};

// ---- Colours for this file ----
#define C_GOWN      (Color){ 170, 200, 230, 255 }   // ward patients' gowns
#define C_SCRUBS    (Color){  60, 150, 130, 255 }   // theatre scrubs
#define C_MASK      (Color){ 150, 205, 220, 255 }
#define C_GLOVE     (Color){ 160, 170, 230, 255 }
#define C_RADIOG    (Color){  45,  75, 125, 255 }   // radiographer's tunic
#define C_PHLEB     (Color){ 150,  50,  80, 255 }   // phlebotomist's tunic
#define C_MIDWIFE   (Color){ 150, 110, 190, 255 }
#define C_SHIRT     (Color){ 236, 238, 242, 255 }
#define C_NAVY      (Color){  50,  60, 100, 255 }
#define C_GREY_HAIR (Color){ 175, 175, 180, 255 }
#define C_FAIR_HAIR (Color){ 205, 170, 105, 255 }
#define C_RED_HAIR  (Color){ 170,  80,  40, 255 }
#define C_MACHINE   (Color){ 228, 231, 237, 255 }   // white hospital equipment
#define C_SCREEN    (Color){ 110, 170, 235, 255 }   // a computer screen that's on
#define C_TUB       (Color){ 214, 232, 244, 255 }   // clear plastic baby cot
#define C_PINK      (Color){ 245, 180, 200, 255 }
#define C_BLUE      (Color){ 160, 200, 240, 255 }
#define C_LEMON     (Color){ 250, 225, 140, 255 }
#define C_MINT      (Color){ 170, 225, 195, 255 }
#define C_CABINET   (Color){ 150, 156, 168, 255 }
#define C_WOOD      (Color){ 196, 160, 120, 255 }

// ------------------------------------------------------------------------
// State
// ------------------------------------------------------------------------

static bool ready = false;                         // signs made yet?
static RenderTexture2D roomSign[ROOM_COUNT + 1];
static float roomSignW[ROOM_COUNT + 1];            // width of each sign, in metres
static RenderTexture2D viewingSign, xraySign, metalSign, scaleScreen;

static bool  night;            // night shift: rooms dim, lamps on
static float now;              // seconds, for the small movements
static bool  lowNorth, lowWest, lowEast, lowSouth;   // outside walls facing the camera

// Monitors only look at a patient's severity, which sets the speed of the trace.
static Patient calmPatient, unwellPatient;

// ------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------

// A box placed relative to `base` in a piece's own directions: lx to the
// side, lz to the front. FACE_SOUTH means its front faces +z, like part() in
// view3d.c, so furniture and dummies can be turned to face any way.
static void lbox(Vector3 base, Facing f, float lx, float y, float lz,
                 float sx, float sy, float sz, Color c)
{
    float wx, wz, wsx = sx, wsz = sz;
    switch (f) {
        case FACE_SOUTH: wx =  lx; wz =  lz;                     break;
        case FACE_NORTH: wx = -lx; wz = -lz;                     break;
        case FACE_EAST:  wx =  lz; wz = -lx; wsx = sz; wsz = sx; break;
        default:         wx = -lz; wz =  lx; wsx = sz; wsz = sx; break;
    }
    draw_box(v3(base.x + wx, y, base.z + wz), v3(wsx, sy, wsz), c);
}

// A shadow under something that may be turned sideways.
static void lshadow(Vector3 p, Facing f, float w, float d)
{
    if (f == FACE_EAST || f == FACE_WEST) shadow(p.x, p.z, d, w);
    else                                  shadow(p.x, p.z, w, d);
}

// A floor of square tiles in two shades, cut to the rectangle.
static void floor_area(float x0, float z0, float x1, float z1, Color a, Color b)
{
    for (float x = floorf(x0); x < x1; x += 1.0f) {
        for (float z = floorf(z0); z < z1; z += 1.0f) {
            float ax = fmaxf(x, x0), bx = fminf(x + 1, x1);
            float az = fmaxf(z, z0), bz = fminf(z + 1, z1);
            if (bx <= ax || bz <= az) continue;
            Color c = shade((((int)floorf(x) + (int)floorf(z)) & 1) ? a : b, 1.0f);
            quad(v3(ax, 0, az), v3(bx, 0, az), v3(bx, 0, bz), v3(ax, 0, bz), c);
        }
    }
}

// Walls running along x (at depth z) and along z (at position x).
static void wall_x(float z, float x0, float x1, float h, bool low)
{
    if (x1 - x0 > 0.01f) draw_wall(v3((x0 + x1) / 2, 0, z), x1 - x0, 0.2f, h, low);
}

static void wall_z(float x, float z0, float z1, float h, bool low)
{
    if (z1 - z0 > 0.01f) draw_wall(v3(x, 0, (z0 + z1) / 2), 0.2f, z1 - z0, h, low);
}

// A room's name on a sign hanging from the ceiling, facing the front. The back
// rooms hang theirs by the back wall; the left rooms by their front wall, so
// that from above the sign sits over its own room.
static void hanging_sign(int room, float x, float z)
{
    float w = roomSignW[room], h = 0.46f, y = 2.32f;
    Color plate = ROOMS[room].colour;
    draw_box(v3(x - w / 2 + 0.15f, y + 0.40f, z), v3(0.03f, 0.56f, 0.03f), C_STEEL);   // wires
    draw_box(v3(x + w / 2 - 0.15f, y + 0.40f, z), v3(0.03f, 0.56f, 0.03f), C_STEEL);
    draw_box(v3(x, y, z), v3(w, h, 0.05f), plate);
    sign_facing_front(roomSign[room].texture, x, y, z + 0.03f, w - 0.06f, h - 0.06f);
}

// A window in the left outside wall, showing the sky.
static void window_west(float z0, float z1)
{
    float x = WEST_X + 0.11f, mid = (z0 + z1) / 2;
    Color sky = night ? (Color){ 28, 40, 72, 255 } : (Color){ 178, 214, 244, 255 };
    draw_box(v3(x, 1.85f, mid), v3(0.04f, 1.02f, z1 - z0 + 0.12f), C_CARD);           // frame
    draw_lit_box(v3(x + 0.025f, 1.85f, mid), v3(0.01f, 0.90f, z1 - z0), sky);          // glass
    draw_box(v3(x + 0.035f, 1.85f, mid), v3(0.02f, 0.90f, 0.05f), C_CARD);             // middle bar
}

// A see-through box with no bottom, for the incubator hood.
static void glass_box(Vector3 p, Vector3 s)
{
    float x0 = p.x - s.x / 2, x1 = p.x + s.x / 2, y0 = p.y - s.y / 2, y1 = p.y + s.y / 2;
    float z0 = p.z - s.z / 2, z1 = p.z + s.z / 2;
    Color g = shade(C_GLASS, 1.0f);
    quad(v3(x0, y1, z0), v3(x1, y1, z0), v3(x1, y1, z1), v3(x0, y1, z1), g);
    quad(v3(x0, y0, z1), v3(x1, y0, z1), v3(x1, y1, z1), v3(x0, y1, z1), g);
    quad(v3(x0, y0, z0), v3(x1, y0, z0), v3(x1, y1, z0), v3(x0, y1, z0), g);
    quad(v3(x0, y0, z0), v3(x0, y0, z1), v3(x0, y1, z1), v3(x0, y1, z0), g);
    quad(v3(x1, y0, z0), v3(x1, y0, z1), v3(x1, y1, z1), v3(x1, y1, z0), g);
}

// A yellow and black hazard line on the floor, along x or along z.
static void hazard_line(float x0, float z0, float x1, float z1)
{
    bool alongX = fabsf(x1 - x0) > fabsf(z1 - z0);
    float len = alongX ? x1 - x0 : z1 - z0, step = 0.3f, t = 0.06f;
    for (int i = 0; i * step < len; i++) {
        float a = i * step, b = fminf(a + step, len);
        Color c = shade((i % 2) ? C_DARK : (Color){ 245, 200, 40, 255 }, 1.0f);
        if (alongX) quad(v3(x0 + a, 0.013f, z0 - t), v3(x0 + b, 0.013f, z0 - t),
                         v3(x0 + b, 0.013f, z0 + t), v3(x0 + a, 0.013f, z0 + t), c);
        else        quad(v3(x0 - t, 0.013f, z0 + a), v3(x0 + t, 0.013f, z0 + a),
                         v3(x0 + t, 0.013f, z0 + b), v3(x0 - t, 0.013f, z0 + b), c);
    }
}

// ------------------------------------------------------------------------
// Dummies
// ------------------------------------------------------------------------

static Outfit outfit(Color top, Color legs, bool cap)
{
    return (Outfit){ top, legs, false, false, cap, false };
}

// Theatre staff: scrubs, a cap, a mask and gloves.
static void draw_masked(Vector3 p, Facing f, Outfit o)
{
    draw_person(p, f, o, false, 0);
    lbox(p, f, 0, 1.19f, 0.17f, 0.28f, 0.14f, 0.02f, C_MASK);                 // mask
    lbox(p, f, -0.31f, 0.54f, 0, 0.13f, 0.11f, 0.15f, C_GLOVE);               // gloves
    lbox(p, f,  0.31f, 0.54f, 0, 0.13f, 0.11f, 0.15f, C_GLOVE);
}

// Someone sitting on a chair (the chair is drawn separately). `typing` brings
// the forearms forward onto a desk and taps the hands.
static void seated_person(Vector3 p, Facing f, Color top, Color legs, Color hair, bool typing)
{
    lshadow(p, f, 0.6f, 0.7f);
    lbox(p, f, 0, 0.52f,  0.12f, 0.40f, 0.16f, 0.50f, legs);                 // thighs
    lbox(p, f, 0, 0.26f,  0.38f, 0.40f, 0.42f, 0.16f, legs);                 // shins
    lbox(p, f, 0, 0.86f, -0.08f, 0.46f, 0.55f, 0.28f, top);                  // body
    if (typing) {
        float tap = 0.012f * sinf(now * 18.0f);
        lbox(p, f, -0.29f, 0.86f, -0.04f, 0.12f, 0.36f, 0.14f, top);          // upper arms
        lbox(p, f,  0.29f, 0.86f, -0.04f, 0.12f, 0.36f, 0.14f, top);
        lbox(p, f, -0.24f, 0.70f,  0.18f, 0.11f, 0.10f, 0.36f, top);          // forearms
        lbox(p, f,  0.24f, 0.70f,  0.18f, 0.11f, 0.10f, 0.36f, top);
        lbox(p, f, -0.20f, 0.70f + tap, 0.40f, 0.10f, 0.07f, 0.12f, C_SKIN);  // hands
        lbox(p, f,  0.20f, 0.70f - tap, 0.40f, 0.10f, 0.07f, 0.12f, C_SKIN);
    } else {
        lbox(p, f, -0.30f, 0.80f, -0.02f, 0.12f, 0.42f, 0.14f, top);          // arms down
        lbox(p, f,  0.30f, 0.80f, -0.02f, 0.12f, 0.42f, 0.14f, top);
        lbox(p, f, -0.30f, 0.57f,  0.04f, 0.11f, 0.10f, 0.13f, C_SKIN);       // hands in the lap
        lbox(p, f,  0.30f, 0.57f,  0.04f, 0.11f, 0.10f, 0.13f, C_SKIN);
    }
    lbox(p, f, 0, 1.30f, -0.08f, 0.32f, 0.32f, 0.32f, C_SKIN);                // head
    lbox(p, f, -0.07f, 1.33f, 0.085f, 0.05f, 0.05f, 0.01f, C_DARK);           // eyes
    lbox(p, f,  0.07f, 1.33f, 0.085f, 0.05f, 0.05f, 0.01f, C_DARK);
    lbox(p, f, 0, 1.48f, -0.10f, 0.34f, 0.08f, 0.34f, hair);                  // hair
    lbox(p, f, 0, 1.32f, -0.245f, 0.34f, 0.26f, 0.03f, hair);
}

// Someone lying on their back with their head towards the back (-z) of the
// piece they're on. `y` is the top of what they're lying on.
static void lying_person(Vector3 p, Facing f, float y, float lz, Color cover, Color hair)
{
    lbox(p, f, 0, y + 0.13f, lz + 0.15f, 0.56f, 0.26f, 1.30f, cover);         // body, covered
    lbox(p, f, 0, y + 0.17f, lz - 0.70f, 0.34f, 0.32f, 0.32f, C_SKIN);        // head
    lbox(p, f, 0, y + 0.17f, lz - 0.86f, 0.36f, 0.34f, 0.06f, hair);
}

// An office or visitor's chair.
static void chair(Vector3 p, Facing f, Color c)
{
    lshadow(p, f, 0.6f, 0.6f);
    lbox(p, f, 0, 0.42f, 0, 0.56f, 0.08f, 0.54f, c);                          // seat
    lbox(p, f, 0, 0.78f, -0.27f, 0.52f, 0.62f, 0.07f, c);                     // back
    lbox(p, f, 0, 0.20f, 0, 0.06f, 0.40f, 0.06f, C_STEEL);                    // post
    lbox(p, f, 0, 0.03f, 0, 0.50f, 0.05f, 0.08f, C_DARK);                     // feet
    lbox(p, f, 0, 0.03f, 0, 0.08f, 0.05f, 0.50f, C_DARK);
}

// ------------------------------------------------------------------------
// Furniture used in more than one room
// ------------------------------------------------------------------------

// A hospital bed with its head at the back (-z) of the piece.
static void bed(Vector3 p, Facing f)
{
    lshadow(p, f, 1.4f, 2.4f);
    lbox(p, f, 0, 0.25f, 0, 1.2f, 0.40f, 2.2f, C_BEDFRAME);                   // frame
    lbox(p, f, 0, 0.53f, 0.05f, 1.1f, 0.16f, 2.0f, C_MATTRESS);               // mattress
    lbox(p, f, 0, 0.66f, -0.75f, 0.8f, 0.12f, 0.35f, C_CARD);                 // pillow
    lbox(p, f, 0, 0.60f, -1.10f, 1.2f, 1.00f, 0.10f, C_BEDFRAME);             // headboard
    lbox(p, f, 0, 0.55f, 1.08f, 1.2f, 0.50f, 0.06f, C_BEDFRAME);              // footboard
}

// A computer monitor on a stand, its screen facing the piece's front.
static void computer(Vector3 p, Facing f, float deskY)
{
    lbox(p, f, 0, deskY + 0.02f, 0, 0.22f, 0.03f, 0.16f, C_DARK);            // foot
    lbox(p, f, 0, deskY + 0.14f, -0.02f, 0.05f, 0.22f, 0.04f, C_DARK);        // neck
    lbox(p, f, 0, deskY + 0.38f, 0, 0.56f, 0.34f, 0.05f, C_DARK);             // screen case
    // The picture: not darkened at night, because screens glow
    Vector3 s = p;
    float lz = 0.03f;
    float wx, wz, sx = 0.50f, sz = 0.01f;
    switch (f) {
        case FACE_SOUTH: wx = 0;   wz =  lz; break;
        case FACE_NORTH: wx = 0;   wz = -lz; break;
        case FACE_EAST:  wx =  lz; wz = 0;   sx = 0.01f; sz = 0.50f; break;
        default:         wx = -lz; wz = 0;   sx = 0.01f; sz = 0.50f; break;
    }
    draw_lit_box(v3(s.x + wx, deskY + 0.38f, s.z + wz), v3(sx, 0.28f, sz), C_SCREEN);
}

// A steel trolley with a shelf underneath.
static void trolley(Vector3 p, Facing f, float w, float d)
{
    lshadow(p, f, w + 0.1f, d + 0.1f);
    lbox(p, f, 0, 0.90f, 0, w, 0.04f, d, C_STEEL);                            // top
    lbox(p, f, 0, 0.35f, 0, w, 0.03f, d, C_STEEL);                            // shelf
    for (int i = 0; i < 4; i++) {
        float lx = (i % 2 ? 1 : -1) * (w / 2 - 0.03f), lz = (i < 2 ? 1 : -1) * (d / 2 - 0.03f);
        lbox(p, f, lx, 0.46f, lz, 0.03f, 0.90f, 0.03f, C_STEEL);              // legs
    }
}

// A drip stand with a bag of fluid.
static void drip(float x, float z)
{
    draw_box(v3(x, 0.03f, z), v3(0.40f, 0.04f, 0.06f), C_STEEL);
    draw_box(v3(x, 0.03f, z), v3(0.06f, 0.04f, 0.40f), C_STEEL);
    draw_box(v3(x, 0.98f, z), v3(0.04f, 1.95f, 0.04f), C_STEEL);
    draw_box(v3(x, 1.94f, z), v3(0.30f, 0.03f, 0.03f), C_STEEL);
    draw_box(v3(x + 0.11f, 1.74f, z), v3(0.15f, 0.25f, 0.05f), (Color){ 205, 230, 245, 255 });
}

// A yellow sharps or clinical waste bin.
static void yellow_bin(float x, float z, float h)
{
    shadow(x, z, 0.4f, 0.4f);
    draw_box(v3(x, h / 2, z), v3(0.34f, h, 0.30f), (Color){ 245, 200, 40, 255 });
    draw_box(v3(x, h + 0.02f, z), v3(0.36f, 0.04f, 0.32f), (Color){ 240, 130, 40, 255 });
}

// ------------------------------------------------------------------------
// The building: floors and walls
// ------------------------------------------------------------------------

static void draw_shell(void)
{
    // Floors: each room has its own colour
    Color corridorA = { 226, 222, 214, 255 }, corridorB = { 232, 228, 220, 255 };
    floor_area(WING_X, BACK_ROOMS_Z, EAST_X, AE_BACK_Z, corridorA, corridorB);        // behind A&E
    floor_area(WING_X, AE_BACK_Z, AE_LEFT_X - 0.1f, SOUTH_Z, corridorA, corridorB);    // left of A&E
    floor_area(ROOMS[WARD].x0, ROOMS[WARD].z0, ROOMS[WARD].x1, ROOMS[WARD].z1,
               (Color){ 210, 218, 228, 255 }, (Color){ 216, 223, 233, 255 });
    floor_area(ROOMS[XRAY].x0, ROOMS[XRAY].z0, ROOMS[XRAY].x1, ROOMS[XRAY].z1,
               (Color){ 222, 220, 230, 255 }, (Color){ 228, 226, 236, 255 });
    floor_area(ROOMS[MRI].x0, ROOMS[MRI].z0, ROOMS[MRI].x1, ROOMS[MRI].z1,
               (Color){ 214, 226, 230, 255 }, (Color){ 220, 232, 236, 255 });
    floor_area(ROOMS[BLOODS].x0, ROOMS[BLOODS].z0, ROOMS[BLOODS].x1, ROOMS[BLOODS].z1,
               (Color){ 222, 228, 240, 255 }, (Color){ 228, 234, 246, 255 });
    floor_area(ROOMS[THEATRE].x0, ROOMS[THEATRE].z0, ROOMS[THEATRE].x1, ROOMS[THEATRE].z1,
               (Color){ 196, 224, 216, 255 }, (Color){ 202, 230, 222, 255 });
    floor_area(ROOMS[NURSERY].x0, ROOMS[NURSERY].z0, ROOMS[NURSERY].x1, ROOMS[NURSERY].z1,
               (Color){ 242, 228, 214, 255 }, (Color){ 246, 234, 222, 255 });
    floor_area(ROOMS[ADMIN].x0, ROOMS[ADMIN].z0, ROOMS[ADMIN].x1, ROOMS[ADMIN].z1,
               (Color){ 150, 160, 178, 255 }, (Color){ 156, 166, 184, 255 });

    // Outside walls
    wall_x(NORTH_Z, WEST_X - 0.1f, EAST_X + 0.1f, OUTER_H, lowNorth);
    wall_z(WEST_X, NORTH_Z, SOUTH_Z + 0.1f, OUTER_H, lowWest);
    wall_z(EAST_X, NORTH_Z, AE_BACK_Z, OUTER_H, lowEast);
    wall_x(SOUTH_Z, WEST_X, AE_LEFT_X + 0.1f, OUTER_H, lowSouth);
    wall_z(AE_LEFT_X, 6.2f, SOUTH_Z, PART_H, false);                     // end of the corridor

    // Between the back rooms and the corridor, with a door into each room
    wall_x(BACK_ROOMS_Z, WING_X, -6.6f, PART_H, false);
    wall_x(BACK_ROOMS_Z, -5.4f, -2.9f, PART_H, false);
    wall_x(BACK_ROOMS_Z, -1.7f,  3.0f, PART_H, false);
    wall_x(BACK_ROOMS_Z,  4.2f, EAST_X, PART_H, false);
    wall_z(-3.3f, NORTH_Z, BACK_ROOMS_Z, PART_H, false);                 // X-ray | MRI
    wall_z( 2.6f, NORTH_Z, BACK_ROOMS_Z, PART_H, false);                 // MRI | theatre

    // Between the left rooms and the corridor: doors, and the nursery's viewing window
    wall_z(WING_X, NORTH_Z, -6.5f, PART_H, false);                       // ward (and X-ray behind)
    wall_z(WING_X, -5.4f, -3.0f, PART_H, false);                         // ward | bloods
    wall_z(WING_X, -1.9f,  3.0f, PART_H, false);                         // bloods | nursery window
    wall_z(WING_X,  3.8f,  4.6f, PART_H, false);                         // nursery | admin
    wall_z(WING_X,  5.6f, SOUTH_Z, PART_H, false);                       // admin
    wall_x(-4.2f, WEST_X, WING_X, PART_H, false);                        // ward | bloods
    wall_x(-0.2f, WEST_X, WING_X, PART_H, false);                        // bloods | nursery
    wall_x( 4.0f, WEST_X, WING_X, PART_H, false);                        // nursery | admin

    // A bench in the corridor
    draw_box(v3(-9.35f, 0.45f, -4.5f), v3(0.42f, 0.08f, 1.30f), C_CHAIR);
    draw_box(v3(-9.35f, 0.22f, -5.0f), v3(0.36f, 0.42f, 0.08f), C_STEEL);
    draw_box(v3(-9.35f, 0.22f, -4.0f), v3(0.36f, 0.42f, 0.08f), C_STEEL);
    draw_plant(-8.9f, 7.1f);
    draw_plant(7.5f, -6.0f);
}

// ------------------------------------------------------------------------
// 1. Ward: four beds, three patients, a visitor and a nurse doing her rounds
// ------------------------------------------------------------------------

#define WARD_BED_X -15.15f
static const float WARD_BED_Z[4] = { -11.9f, -9.9f, -7.9f, -5.9f };

static void draw_ward(void)
{
    Color blankets[3] = { (Color){ 150, 190, 230, 255 }, (Color){ 190, 215, 175, 255 },
                          (Color){ 200, 185, 225, 255 } };
    Color hair[3] = { C_GREY_HAIR, C_HAIR, C_FAIR_HAIR };

    for (int i = 0; i < 4; i++) {
        float z = WARD_BED_Z[i];
        Vector3 p = v3(WARD_BED_X, 0, z);
        bed(p, FACE_EAST);                                                  // head against the wall
        if (i < 3) {
            lying_person(p, FACE_EAST, 0.61f, 0.0f, blankets[i], hair[i]);
        } else {
            lbox(p, FACE_EAST, 0, 0.66f, 0.70f, 1.0f, 0.10f, 0.40f, (Color){ 150, 190, 230, 255 }); // folded blanket
        }
        // Bedside locker with a jug of water
        draw_box(v3(-16.05f, 0.40f, z - 0.95f), v3(0.45f, 0.80f, 0.42f), C_WOOD);
        draw_box(v3(-16.05f, 0.62f, z - 0.74f), v3(0.30f, 0.02f, 0.02f), C_DARK);      // drawer handle
        draw_box(v3(-16.12f, 0.92f, z - 0.95f), v3(0.10f, 0.20f, 0.10f), (Color){ 150, 200, 240, 255 });
        draw_box(v3(-15.96f, 0.86f, z - 0.90f), v3(0.07f, 0.10f, 0.07f), C_CARD);      // cup

        // Curtain rail between beds, with the curtain pulled back to the foot
        if (i < 3) {
            float cz = z + 1.0f;
            draw_box(v3(-15.0f, 2.15f, cz), v3(2.6f, 0.04f, 0.04f), C_STEEL);
            for (int k = 0; k < 4; k++) {
                draw_box(v3(-13.95f - k * 0.1f, 1.22f, cz + ((k % 2) ? 0.03f : -0.03f)),
                         v3(0.10f, 1.80f, 0.03f), shade(C_CURTAIN, (k % 2) ? 0.9f : 1.0f));
            }
        }
    }
    draw_monitor(-15.4f, WARD_BED_Z[0] + 0.98f, &unwellPatient);
    drip(-14.3f, WARD_BED_Z[0] - 0.75f);
    drip(-14.3f, WARD_BED_Z[2] - 0.75f);

    // An over-bed table with a meal tray across bed 2
    float tz = WARD_BED_Z[1];
    draw_box(v3(-14.6f, 1.02f, tz), v3(0.42f, 0.04f, 0.95f), C_WOOD);
    draw_box(v3(-14.6f, 0.52f, tz + 0.55f), v3(0.06f, 1.00f, 0.06f), C_STEEL);
    draw_box(v3(-14.6f, 0.03f, tz + 0.55f), v3(0.40f, 0.05f, 0.40f), C_STEEL);
    draw_box(v3(-14.6f, 1.06f, tz - 0.1f), v3(0.30f, 0.03f, 0.40f), C_CARD);         // tray
    draw_box(v3(-14.6f, 1.10f, tz - 0.2f), v3(0.14f, 0.05f, 0.14f), (Color){ 230, 160, 80, 255 });

    // A visitor sitting by bed 3
    Vector3 seat = v3(-13.7f, 0, WARD_BED_Z[2] + 0.75f);
    chair(seat, FACE_NORTH, C_CHAIR);
    seated_person(seat, FACE_NORTH, (Color){ 200, 120, 60, 255 }, C_TROUSERS, C_RED_HAIR, false);

    // The nurse walks from bed to bed: 3.5 seconds at each, 1.5 seconds walking
    const float stand = 3.5f, walk = 1.5f, x = -13.5f;
    float t = fmodf(now, 3 * (stand + walk));
    int stop = (int)(t / (stand + walk));
    float into = t - stop * (stand + walk);
    float zFrom = WARD_BED_Z[stop] + 0.35f, zTo = WARD_BED_Z[(stop + 1) % 3] + 0.35f;
    bool walking = into > stand;
    float k = walking ? (into - stand) / walk : 0.0f;
    float nz = zFrom + (zTo - zFrom) * (k * k * (3 - 2 * k));                // ease in and out
    Facing face = walking ? (zTo > zFrom ? FACE_SOUTH : FACE_NORTH) : FACE_WEST;
    draw_person(v3(x, 0, nz), face, (Outfit){ C_BRAND, C_BRAND, false, false, true, true },
                walking, now * 12.0f);

    if (!lowWest) {
        for (int i = 0; i < 4; i++) window_west(WARD_BED_Z[i] - 0.55f, WARD_BED_Z[i] + 0.55f);
    }
    if (!lowNorth) draw_clock(-12.6f, 1.95f, NORTH_Z + 0.12f, 10, 10);
    hanging_sign(WARD, -13.1f, -4.75f);
}

// ------------------------------------------------------------------------
// 2. X-ray: the table, the tube on its ceiling rails, a chest stand,
//    lead aprons, and the radiographer behind a screen
// ------------------------------------------------------------------------

static bool xray_exposing(void) { return fmodf(now, 7.0f) < 0.8f; }

static void draw_xray(void)
{
    // Table, with a patient on it
    float tx = -5.6f, tz = -10.2f;
    shadow(tx, tz, 2.4f, 1.0f);
    draw_box(v3(tx, 0.32f, tz), v3(0.90f, 0.64f, 0.50f), C_MACHINE);                 // pedestal
    draw_box(v3(tx, 0.70f, tz), v3(2.30f, 0.10f, 0.85f), (Color){ 238, 232, 214, 255 }); // table top
    lying_person(v3(tx, 0, tz), FACE_EAST, 0.75f, 0.0f, (Color){ 150, 180, 220, 255 }, C_HAIR);
    draw_box(v3(tx + 0.2f, 0.98f, tz), v3(0.46f, 0.04f, 0.62f), C_NAVY);           // lead apron over them

    // Ceiling rails, and the tube hanging from them, drifting along the table
    float cx = tx + 0.4f * sinf(now * 0.4f);
    draw_box(v3(tx, 2.62f, tz - 0.55f), v3(4.0f, 0.06f, 0.08f), C_STEEL);
    draw_box(v3(tx, 2.62f, tz + 0.55f), v3(4.0f, 0.06f, 0.08f), C_STEEL);
    draw_box(v3(cx, 2.54f, tz), v3(0.50f, 0.12f, 1.25f), C_MACHINE);               // carriage
    draw_box(v3(cx, 2.08f, tz), v3(0.14f, 0.85f, 0.14f), C_STEEL);                 // column
    draw_box(v3(cx, 1.60f, tz), v3(0.66f, 0.28f, 0.38f), C_MACHINE);               // tube head
    Color beam = { 255, 240, 170, 255 };
    if (xray_exposing()) draw_lit_box(v3(cx, 1.38f, tz), v3(0.32f, 0.18f, 0.32f), beam);
    else                 draw_box    (v3(cx, 1.38f, tz), v3(0.32f, 0.18f, 0.32f), C_STEEL);

    // Chest X-ray stand by the back wall: two posts and the detector panel
    float bx = -4.3f, bz = NORTH_Z + 0.45f;
    draw_box(v3(bx, 0.02f, bz), v3(0.70f, 0.04f, 0.50f), C_STEEL);
    draw_box(v3(bx - 0.30f, 1.00f, bz - 0.15f), v3(0.07f, 2.0f, 0.07f), C_STEEL);
    draw_box(v3(bx + 0.30f, 1.00f, bz - 0.15f), v3(0.07f, 2.0f, 0.07f), C_STEEL);
    draw_box(v3(bx, 1.40f, bz - 0.12f), v3(0.56f, 0.56f, 0.08f), C_MACHINE);
    draw_box(v3(bx, 1.40f, bz - 0.07f), v3(0.40f, 0.02f, 0.01f), C_DARK);          // cross-hairs
    draw_box(v3(bx, 1.40f, bz - 0.07f), v3(0.02f, 0.40f, 0.01f), C_DARK);

    // On the back wall: lead aprons on hooks, and a warning sign
    if (!lowNorth) {
        float wz = NORTH_Z + 0.13f;
        draw_box(v3(-7.4f, 1.95f, wz), v3(1.2f, 0.05f, 0.05f), C_STEEL);
        Color aprons[3] = { C_NAVY, (Color){ 120, 80, 160, 255 }, (Color){ 40, 140, 150, 255 } };
        for (int i = 0; i < 3; i++) {
            draw_box(v3(-7.8f + i * 0.4f, 1.55f, wz + 0.03f), v3(0.34f, 0.72f, 0.05f), aprons[i]);
        }
        draw_box(v3(-5.2f, 1.85f, wz), v3(1.20f, 0.44f, 0.03f), (Color){ 245, 200, 40, 255 });
        sign_facing_front(xraySign.texture, -5.2f, 1.85f, wz + 0.02f, 1.14f, 0.38f);
    }

    // The control area: a screen with a window, a desk and the radiographer
    float sx = -8.0f;
    draw_box(v3(sx, 0.50f, -8.4f), v3(0.12f, 1.00f, 1.60f), C_MACHINE);           // screen, lower half
    draw_box(v3(sx, 2.05f, -8.4f), v3(0.12f, 0.10f, 1.60f), C_MACHINE);           // top of the window
    draw_box(v3(sx, 1.50f, -9.15f), v3(0.12f, 1.00f, 0.10f), C_MACHINE);          // window frame
    draw_box(v3(sx, 1.50f, -7.65f), v3(0.12f, 1.00f, 0.10f), C_MACHINE);
    draw_box(v3(-8.8f, 1.05f, -9.2f), v3(1.6f, 2.1f, 0.10f), C_MACHINE);          // side panel
    draw_box(v3(-8.45f, 0.40f, -8.4f), v3(0.55f, 0.80f, 1.2f), C_DESK);           // desk
    computer(v3(-8.45f, 0, -8.65f), FACE_WEST, 0.80f);
    computer(v3(-8.45f, 0, -8.10f), FACE_WEST, 0.80f);
    draw_person(v3(-9.2f, 0, -8.35f), FACE_EAST, outfit(C_RADIOG, C_SHIRT, false), false, 0);

    // "In use" lamp by the door: lit while an X-ray is being taken
    Color lamp = xray_exposing() ? (Color){ 255, 70, 70, 255 } : (Color){ 120, 40, 45, 255 };
    draw_box(v3(-5.0f, 1.25f, BACK_ROOMS_Z), v3(0.20f, 0.20f, 0.24f), C_DARK);
    if (xray_exposing()) draw_lit_box(v3(-5.0f, 1.25f, BACK_ROOMS_Z + 0.13f), v3(0.14f, 0.14f, 0.02f), lamp);
    else                 draw_box    (v3(-5.0f, 1.25f, BACK_ROOMS_Z + 0.13f), v3(0.14f, 0.14f, 0.02f), lamp);

    hanging_sign(XRAY, -6.5f, -12.3f);
}

// ------------------------------------------------------------------------
// 3. MRI: the scanner, a patient sliding in and out on the table,
//    the safety line on the floor and the control desk
// ------------------------------------------------------------------------

#define MRI_X   -0.6f    // centre of the scanner
#define MRI_Z  -11.4f
#define BORE_Y   1.0f    // height of the middle of the tunnel

static void draw_mri(void)
{
    // The magnet: a big block with a square tunnel, a rounded top, and rings on the front
    float front = MRI_Z + 0.8f;
    shadow(MRI_X, MRI_Z, 3.0f, 2.0f);
    draw_box(v3(MRI_X - 0.9f, 1.15f, MRI_Z), v3(0.80f, 2.30f, 1.60f), C_MACHINE);   // left
    draw_box(v3(MRI_X + 0.9f, 1.15f, MRI_Z), v3(0.80f, 2.30f, 1.60f), C_MACHINE);   // right
    draw_box(v3(MRI_X, 1.90f, MRI_Z), v3(1.00f, 0.80f, 1.60f), C_MACHINE);          // above the tunnel
    draw_box(v3(MRI_X, 0.25f, MRI_Z), v3(1.00f, 0.50f, 1.60f), C_MACHINE);          // below it
    draw_box(v3(MRI_X, 2.36f, MRI_Z), v3(1.80f, 0.14f, 1.40f), C_MACHINE);          // rounded top
    draw_box(v3(MRI_X, 1.15f, MRI_Z - 0.75f), v3(2.0f, 2.0f, 0.10f), shade(C_MACHINE, 0.95f));
    // the inside of the tunnel, and its light
    draw_box(v3(MRI_X, BORE_Y, MRI_Z - 0.70f), v3(1.0f, 1.0f, 0.02f), (Color){ 70, 80, 95, 255 });
    draw_lit_box(v3(MRI_X, BORE_Y + 0.48f, MRI_Z), v3(0.5f, 0.02f, 1.2f), (Color){ 210, 230, 255, 255 });
    // rings round the opening
    const int n = 28;
    for (int i = 0; i < n; i++) {
        float a = i * 2 * PI / n;
        draw_box(v3(MRI_X + cosf(a) * 0.68f, BORE_Y + sinf(a) * 0.68f, front + 0.02f),
                 v3(0.17f, 0.17f, 0.04f), ROOMS[MRI].colour);
        draw_box(v3(MRI_X + cosf(a) * 1.02f, BORE_Y + sinf(a) * 1.02f, front + 0.01f),
                 v3(0.20f, 0.20f, 0.03f), shade(C_MACHINE, 0.92f));
    }
    draw_lit_box(v3(MRI_X + 0.7f, 2.05f, front + 0.03f), v3(0.18f, 0.06f, 0.02f), (Color){ 90, 220, 140, 255 });

    // The table slides in and out of the tunnel, with the patient on it
    float slide = 0.5f - 0.5f * cosf(now * 0.3f);                               // 0 out .. 1 in
    float tz = -9.2f - slide * 1.3f;
    draw_box(v3(MRI_X, 0.34f, -9.3f), v3(0.70f, 0.68f, 1.40f), C_MACHINE);          // base
    draw_box(v3(MRI_X, 0.76f, tz), v3(0.78f, 0.10f, 2.40f), (Color){ 238, 232, 214, 255 });
    Vector3 pt = v3(MRI_X, 0, tz);
    lying_person(pt, FACE_SOUTH, 0.81f, -0.1f, (Color){ 150, 180, 220, 255 }, C_HAIR);
    lbox(pt, FACE_SOUTH, -0.22f, 1.10f, -0.80f, 0.05f, 0.34f, 0.40f, C_MACHINE);   // head coil
    lbox(pt, FACE_SOUTH,  0.22f, 1.10f, -0.80f, 0.05f, 0.34f, 0.40f, C_MACHINE);
    lbox(pt, FACE_SOUTH,  0.00f, 1.26f, -0.80f, 0.48f, 0.05f, 0.40f, C_MACHINE);

    // The safety line round the magnet: no metal past here
    float x0 = -2.9f, x1 = 0.75f, z0 = NORTH_Z + 0.3f, z1 = -7.6f;
    hazard_line(x0, z0, x1, z0);
    hazard_line(x0, z1, x1, z1);
    hazard_line(x0, z0, x0, z1);
    hazard_line(x1, z0, x1, z1);

    // Control desk behind a window, with a brain scan on the screen
    float sx = 0.95f;
    draw_box(v3(sx, 0.50f, -8.4f), v3(0.12f, 1.00f, 1.60f), C_MACHINE);
    draw_box(v3(sx, 2.05f, -8.4f), v3(0.12f, 0.10f, 1.60f), C_MACHINE);
    draw_box(v3(sx, 1.50f, -9.15f), v3(0.12f, 1.00f, 0.10f), C_MACHINE);
    draw_box(v3(sx, 1.50f, -7.65f), v3(0.12f, 1.00f, 0.10f), C_MACHINE);
    draw_box(v3(1.75f, 1.05f, -9.2f), v3(1.6f, 2.1f, 0.10f), C_MACHINE);
    draw_box(v3(1.45f, 0.40f, -8.4f), v3(0.55f, 0.80f, 1.2f), C_DESK);
    computer(v3(1.45f, 0, -8.65f), FACE_EAST, 0.80f);
    computer(v3(1.45f, 0, -8.10f), FACE_EAST, 0.80f);
    draw_lit_box(v3(1.49f, 1.18f, -8.10f), v3(0.01f, 0.20f, 0.16f), (Color){ 200, 200, 205, 255 }); // the scan
    draw_lit_box(v3(1.50f, 1.18f, -8.10f), v3(0.01f, 0.12f, 0.08f), (Color){ 120, 120, 128, 255 });
    draw_person(v3(2.15f, 0, -8.4f), FACE_WEST, outfit(C_RADIOG, C_SHIRT, false), false, 0);

    // On the back wall: NO METAL
    if (!lowNorth) {
        float wz = NORTH_Z + 0.13f;
        draw_box(v3(1.5f, 1.85f, wz), v3(1.10f, 0.40f, 0.03f), C_EMERGENCY);
        sign_facing_front(metalSign.texture, 1.5f, 1.85f, wz + 0.02f, 1.04f, 0.34f);
    }
    hanging_sign(MRI, -0.35f, -12.3f);
}

// ------------------------------------------------------------------------
// 4. Bloods & weight: a phlebotomy chair, a trolley of blood tubes,
//    scales with a height measure, a sink and two people waiting
// ------------------------------------------------------------------------

static void draw_bloods(void)
{
    // Phlebotomy chair, facing into the room, with a patient giving blood
    Vector3 c = v3(-15.3f, 0, -2.6f);
    Color seat = { 70, 110, 170, 255 };
    lshadow(c, FACE_EAST, 0.9f, 0.9f);
    lbox(c, FACE_EAST, 0, 0.18f, 0, 0.60f, 0.36f, 0.60f, C_DARK);               // base
    lbox(c, FACE_EAST, 0, 0.42f, 0.05f, 0.70f, 0.14f, 0.70f, seat);             // seat
    lbox(c, FACE_EAST, 0, 0.95f, -0.36f, 0.70f, 0.95f, 0.14f, seat);            // back
    lbox(c, FACE_EAST, -0.42f, 0.66f, 0.15f, 0.14f, 0.07f, 0.80f, seat);        // arm rests,
    lbox(c, FACE_EAST,  0.42f, 0.66f, 0.15f, 0.14f, 0.07f, 0.80f, seat);        // reaching forwards
    seated_person(c, FACE_EAST, (Color){ 120, 160, 110, 255 }, C_TROUSERS, C_HAIR, false);
    lbox(c, FACE_EAST, 0.42f, 0.73f, 0.35f, 0.11f, 0.08f, 0.40f, C_SKIN);       // arm out on the rest
    lbox(c, FACE_EAST, 0.42f, 0.78f, 0.25f, 0.12f, 0.03f, 0.10f, (Color){ 200, 60, 60, 255 }); // tourniquet

    // The phlebotomist, at the patient's arm
    draw_person(v3(-14.3f, 0, -3.35f), FACE_WEST, outfit(C_PHLEB, C_PHLEB, true), false, 0);

    // Trolley with a rack of blood tubes (the caps are the real colours),
    // a sharps bin and a box of gloves
    Vector3 t = v3(-13.4f, 0, -3.6f);
    trolley(t, FACE_SOUTH, 0.70f, 0.45f);
    draw_box(v3(t.x - 0.1f, 0.95f, t.z), v3(0.36f, 0.06f, 0.12f), C_CARD);
    Color caps[6] = { { 140, 80, 180, 255 }, { 230, 180, 40, 255 }, { 120, 190, 230, 255 },
                      { 200, 50, 50, 255 }, { 60, 160, 90, 255 }, { 150, 150, 150, 255 } };
    for (int i = 0; i < 6; i++) {
        float x = t.x - 0.24f + i * 0.055f;
        draw_box(v3(x, 1.03f, t.z), v3(0.035f, 0.11f, 0.035f), (Color){ 235, 240, 245, 255 });
        draw_box(v3(x, 1.10f, t.z), v3(0.04f, 0.03f, 0.04f), caps[i]);
    }
    draw_box(v3(t.x + 0.22f, 1.04f, t.z), v3(0.18f, 0.22f, 0.16f), (Color){ 245, 200, 40, 255 }); // sharps bin
    draw_box(v3(t.x + 0.22f, 1.16f, t.z), v3(0.19f, 0.03f, 0.17f), (Color){ 240, 130, 40, 255 });
    draw_box(v3(t.x, 0.42f, t.z), v3(0.24f, 0.10f, 0.14f), C_BRAND);                              // gloves

    // Scales with a height measure, and someone being weighed
    Vector3 s = v3(-11.2f, 0, -1.25f);
    shadow(s.x, s.z, 0.7f, 0.7f);
    draw_box(v3(s.x, 0.025f, s.z), v3(0.56f, 0.05f, 0.52f), (Color){ 90, 95, 105, 255 });
    draw_box(v3(s.x, 1.05f, s.z - 0.30f), v3(0.07f, 2.10f, 0.07f), C_STEEL);         // height pole
    draw_box(v3(s.x, 1.64f, s.z - 0.16f), v3(0.06f, 0.03f, 0.30f), C_STEEL);         // head plate
    draw_person(s, FACE_SOUTH, outfit((Color){ 200, 160, 80, 255 }, C_TROUSERS, false), false, 0);
    draw_box(v3(s.x + 0.62f, 0.55f, s.z), v3(0.08f, 1.10f, 0.08f), C_STEEL);         // read-out stand
    draw_box(v3(s.x + 0.62f, 1.15f, s.z), v3(0.36f, 0.20f, 0.08f), C_DARK);
    sign_facing_front(scaleScreen.texture, s.x + 0.62f, 1.15f, s.z + 0.045f, 0.30f, 0.14f);

    // Sink against the back wall, with soap
    draw_box(v3(-12.6f, 0.42f, -3.85f), v3(0.70f, 0.84f, 0.45f), C_CARD);
    draw_box(v3(-12.6f, 0.88f, -3.85f), v3(0.62f, 0.10f, 0.40f), C_STEEL);
    draw_box(v3(-12.6f, 1.02f, -4.0f), v3(0.05f, 0.20f, 0.05f), C_STEEL);            // tap
    draw_box(v3(-12.2f, 1.00f, -4.0f), v3(0.10f, 0.16f, 0.08f), (Color){ 120, 190, 230, 255 });

    // Two chairs by the door; one patient waiting
    Vector3 w1 = v3(-12.2f, 0, -0.75f), w2 = v3(-12.9f, 0, -0.75f);
    chair(w1, FACE_NORTH, C_CHAIR);
    chair(w2, FACE_NORTH, C_CHAIR);
    seated_person(w1, FACE_NORTH, (Color){ 90, 120, 170, 255 }, C_TROUSERS, C_GREY_HAIR, false);
    yellow_bin(-16.1f, -0.6f, 0.55f);

    hanging_sign(BLOODS, -13.6f, -0.65f);
}

// ------------------------------------------------------------------------
// 5. Operating theatre: the table and patient under the drapes, the big
//    lights, the anaesthetic machine, and the team in scrubs
// ------------------------------------------------------------------------

static void theatre_lamp(float x, float y, float z)
{
    Color body = { 225, 228, 234, 255 };
    draw_box(v3(x, y, z), v3(0.80f, 0.12f, 0.80f), body);
    draw_box(v3(x, y + 0.02f, z), v3(0.56f, 0.12f, 1.04f), body);                    // rounder corners
    draw_box(v3(x, y + 0.02f, z), v3(1.04f, 0.12f, 0.56f), body);
    draw_lit_box(v3(x, y - 0.07f, z), v3(0.62f, 0.02f, 0.62f), (Color){ 255, 252, 230, 255 });
    draw_box(v3(x, y + 0.16f, z), v3(0.12f, 0.20f, 0.12f), C_STEEL);
}

static void draw_theatre(void)
{
    float tx = 5.4f, tz = -10.0f;

    // Operating table, with the patient covered in green drapes
    shadow(tx, tz, 1.0f, 2.3f);
    draw_box(v3(tx, 0.04f, tz), v3(0.80f, 0.08f, 1.00f), C_STEEL);
    draw_box(v3(tx, 0.45f, tz), v3(0.40f, 0.80f, 0.60f), C_STEEL);
    draw_box(v3(tx, 0.88f, tz), v3(0.70f, 0.10f, 2.10f), (Color){ 60, 66, 78, 255 });
    Vector3 p = v3(tx, 0, tz);
    lying_person(p, FACE_SOUTH, 0.93f, 0.0f, C_SCRUBS, C_HAIR);
    Color drape = { 50, 130, 115, 255 };
    lbox(p, FACE_SOUTH, 0, 1.12f, 0.25f, 0.92f, 0.05f, 1.55f, drape);
    lbox(p, FACE_SOUTH, -0.45f, 0.86f, 0.25f, 0.03f, 0.50f, 1.55f, drape);           // hanging over the sides
    lbox(p, FACE_SOUTH,  0.45f, 0.86f, 0.25f, 0.03f, 0.50f, 1.55f, drape);
    lbox(p, FACE_SOUTH, 0, 1.40f, -0.55f, 1.00f, 0.55f, 0.03f, drape);               // screen at the head end
    lbox(p, FACE_SOUTH, 0, 1.18f, -0.70f, 0.34f, 0.06f, 0.33f, C_SCRUBCAP);          // patient's cap

    // Overhead lights on arms from the ceiling
    draw_box(v3(tx, 2.80f, tz), v3(0.30f, 0.10f, 0.30f), C_STEEL);
    draw_box(v3(tx, 2.62f, tz), v3(0.10f, 0.36f, 0.10f), C_STEEL);
    draw_box(v3(tx - 0.25f, 2.46f, tz), v3(0.50f, 0.07f, 0.07f), C_STEEL);
    draw_box(v3(tx + 0.30f, 2.50f, tz - 0.2f), v3(0.07f, 0.07f, 0.45f), C_STEEL);
    draw_box(v3(tx + 0.30f, 2.50f, tz), v3(0.60f, 0.07f, 0.07f), C_STEEL);
    theatre_lamp(tx - 0.5f, 2.24f, tz + 0.3f);
    theatre_lamp(tx + 0.6f, 2.32f, tz - 0.4f);

    // Anaesthetic machine at the head end, with gas cylinders and a breathing tube
    float ax = 4.15f, az = -12.2f;
    shadow(ax, az, 0.9f, 0.7f);
    draw_box(v3(ax, 0.62f, az), v3(0.80f, 1.24f, 0.60f), (Color){ 205, 212, 224, 255 });
    draw_box(v3(ax, 1.30f, az), v3(0.84f, 0.06f, 0.64f), C_STEEL);
    draw_box(v3(ax, 1.55f, az + 0.05f), v3(0.50f, 0.36f, 0.06f), C_DARK);
    draw_lit_box(v3(ax, 1.55f, az + 0.09f), v3(0.42f, 0.28f, 0.01f), (Color){ 20, 60, 50, 255 });
    draw_lit_box(v3(ax, 1.58f, az + 0.10f), v3(0.32f, 0.02f, 0.01f), (Color){ 80, 255, 140, 255 });
    draw_lit_box(v3(ax, 1.50f, az + 0.10f), v3(0.32f, 0.02f, 0.01f), (Color){ 255, 220, 80, 255 });
    draw_box(v3(ax + 0.25f, 0.95f, az + 0.32f), v3(0.18f, 0.28f, 0.10f), (Color){ 190, 220, 240, 255 }); // bellows
    for (int i = 0; i < 2; i++) {
        float cx = ax - 0.52f, cz = az - 0.15f + i * 0.28f;
        draw_box(v3(cx, 0.40f, cz), v3(0.16f, 0.80f, 0.16f), i ? C_DARK : C_CARD);    // cylinders
        draw_box(v3(cx, 0.84f, cz), v3(0.12f, 0.08f, 0.12f), C_CARD);
    }
    DrawLine3D(v3(ax + 0.25f, 1.05f, az + 0.35f), v3(tx, 1.15f, tz - 0.85f), shade((Color){ 120, 160, 200, 255 }, 1.0f));

    // The anaesthetist sits at the head; two surgeons and a scrub nurse stand
    Outfit scrubs = outfit(C_SCRUBS, C_SCRUBS, true);
    Vector3 stool = v3(tx, 0, -11.55f);
    draw_box(v3(stool.x, 0.22f, stool.z), v3(0.40f, 0.44f, 0.40f), C_STEEL);
    seated_person(stool, FACE_SOUTH, C_SCRUBS, C_SCRUBS, C_SCRUBCAP, false);
    lbox(stool, FACE_SOUTH, 0, 1.22f, 0.09f, 0.28f, 0.14f, 0.02f, C_MASK);
    draw_masked(v3(tx - 0.75f, 0, tz + 0.2f), FACE_EAST, scrubs);
    draw_masked(v3(tx + 0.75f, 0, tz + 0.2f), FACE_WEST, scrubs);
    draw_masked(v3(6.55f, 0, -8.3f), FACE_NORTH, scrubs);

    // Instrument trolley with a green cloth and instruments laid out
    Vector3 it = v3(6.6f, 0, -9.05f);
    trolley(it, FACE_SOUTH, 0.90f, 0.55f);
    draw_box(v3(it.x, 0.93f, it.z), v3(0.86f, 0.02f, 0.52f), drape);
    for (int i = 0; i < 7; i++) {
        draw_box(v3(it.x - 0.33f + i * 0.11f, 0.95f, it.z), v3(0.03f, 0.02f, 0.30f), C_STEEL);
    }

    // Heart monitor, scrub sink, and a clinical waste bin
    draw_monitor(7.1f, -11.4f, &unwellPatient);
    draw_box(v3(7.75f, 0.45f, -12.2f), v3(0.50f, 0.90f, 1.20f), C_STEEL);
    draw_box(v3(7.75f, 0.92f, -12.2f), v3(0.46f, 0.06f, 1.10f), shade(C_STEEL, 0.8f));
    draw_box(v3(7.95f, 1.10f, -12.45f), v3(0.05f, 0.30f, 0.05f), C_STEEL);
    draw_box(v3(7.95f, 1.10f, -11.95f), v3(0.05f, 0.30f, 0.05f), C_STEEL);
    yellow_bin(7.6f, -7.3f, 0.65f);

    hanging_sign(THEATRE, 5.35f, -12.3f);
}

// ------------------------------------------------------------------------
// 6. Nursery: babies in cots facing the viewing window, an incubator,
//    a midwife, a nurse feeding a baby, and proud visitors in the corridor
// ------------------------------------------------------------------------

// A baby lying in a blanket, head towards the back of the piece.
static void baby(Vector3 p, Facing f, float y, Color blanket, int i)
{
    float breathe = 0.006f * sinf(now * 2.2f + i);
    lbox(p, f, 0, y + 0.06f + breathe, 0.06f, 0.20f, 0.12f + breathe, 0.34f, blanket);
    lbox(p, f, 0, y + 0.07f, -0.19f, 0.15f, 0.14f, 0.14f, C_SKIN);                  // head
    lbox(p, f, 0, y + 0.15f, -0.22f, 0.16f, 0.05f, 0.11f, blanket);                 // hat
    lbox(p, f, -0.03f, y + 0.143f, -0.15f, 0.035f, 0.005f, 0.01f, C_DARK);          // eyes, asleep
    lbox(p, f,  0.03f, y + 0.143f, -0.15f, 0.035f, 0.005f, 0.01f, C_DARK);
}

// A clear plastic cot on a trolley, feet towards the window.
static void bassinet(float x, float z, Color blanket, int i)
{
    Vector3 p = v3(x, 0, z);
    Facing f = FACE_EAST;
    lshadow(p, f, 0.55f, 0.85f);
    lbox(p, f, 0, 0.05f, 0, 0.46f, 0.04f, 0.70f, C_STEEL);                          // base
    lbox(p, f, 0, 0.35f, -0.25f, 0.05f, 0.56f, 0.05f, C_STEEL);                     // posts
    lbox(p, f, 0, 0.35f,  0.25f, 0.05f, 0.56f, 0.05f, C_STEEL);
    lbox(p, f, 0, 0.30f, 0, 0.42f, 0.03f, 0.60f, C_CARD);                           // shelf
    lbox(p, f, 0, 0.64f, 0, 0.50f, 0.04f, 0.80f, C_TUB);                            // the cot
    lbox(p, f, -0.235f, 0.76f, 0, 0.03f, 0.22f, 0.80f, C_TUB);
    lbox(p, f,  0.235f, 0.76f, 0, 0.03f, 0.22f, 0.80f, C_TUB);
    lbox(p, f, 0, 0.76f, -0.385f, 0.50f, 0.22f, 0.03f, C_TUB);
    lbox(p, f, 0, 0.76f,  0.385f, 0.50f, 0.22f, 0.03f, C_TUB);
    lbox(p, f, 0, 0.675f, 0, 0.44f, 0.03f, 0.74f, C_MATTRESS);
    lbox(p, f, 0, 0.80f, 0.405f, 0.24f, 0.11f, 0.01f, C_CARD);                      // name card
    lbox(p, f, 0, 0.835f, 0.41f, 0.24f, 0.03f, 0.01f, blanket);
    baby(p, f, 0.69f, blanket, i);
}

static void draw_nursery(void)
{
    // Two columns of cots facing the viewing window
    Color blankets[4] = { C_PINK, C_BLUE, C_LEMON, C_MINT };
    for (int col = 0; col < 2; col++) {
        for (int row = 0; row < 4; row++) {
            int i = col * 4 + row;
            bassinet(-10.9f - col * 1.4f, 0.45f + row * 0.85f, blankets[(i * 3 + col) % 4], i);
        }
    }

    // An incubator with a tiny baby, and its monitor
    Vector3 inc = v3(-15.4f, 0, 0.75f);
    lshadow(inc, FACE_EAST, 0.75f, 1.1f);
    lbox(inc, FACE_EAST, 0, 0.42f, 0, 0.62f, 0.66f, 1.00f, (Color){ 200, 210, 225, 255 });
    lbox(inc, FACE_EAST, 0, 0.78f, 0, 0.66f, 0.06f, 1.04f, C_MACHINE);
    lbox(inc, FACE_EAST, 0, 0.82f, 0, 0.50f, 0.03f, 0.86f, C_MATTRESS);
    baby(inc, FACE_EAST, 0.84f, C_LEMON, 9);
    lbox(inc, FACE_EAST, -0.33f, 1.02f, -0.15f, 0.01f, 0.14f, 0.14f, C_MACHINE);    // arm holes
    lbox(inc, FACE_EAST, -0.33f, 1.02f,  0.20f, 0.01f, 0.14f, 0.14f, C_MACHINE);
    draw_monitor(-14.6f, 0.15f, &calmPatient);

    // Changing table against the wall, with a stack of nappies
    draw_box(v3(-16.05f, 0.45f, 2.05f), v3(0.60f, 0.90f, 0.95f), C_CARD);
    draw_box(v3(-16.05f, 0.94f, 2.05f), v3(0.52f, 0.08f, 0.85f), C_BLUE);
    draw_box(v3(-15.76f, 0.45f, 2.05f), v3(0.02f, 0.02f, 0.40f), C_DARK);           // drawer handles
    draw_box(v3(-15.76f, 0.70f, 2.05f), v3(0.02f, 0.02f, 0.40f), C_DARK);
    for (int i = 0; i < 4; i++) {
        draw_box(v3(-16.1f, 1.02f + i * 0.05f, 2.35f), v3(0.24f, 0.045f, 0.18f), C_CARD);
    }

    // A nurse in the feeding chair, holding a baby
    Vector3 fc = v3(-15.55f, 0, 3.25f);
    lshadow(fc, FACE_EAST, 0.9f, 0.9f);
    lbox(fc, FACE_EAST, 0, 0.25f, 0.05f, 0.80f, 0.50f, 0.70f, ROOMS[NURSERY].colour);   // armchair
    lbox(fc, FACE_EAST, 0, 0.85f, -0.32f, 0.80f, 0.80f, 0.16f, ROOMS[NURSERY].colour);
    lbox(fc, FACE_EAST, -0.36f, 0.62f, 0.05f, 0.12f, 0.24f, 0.70f, ROOMS[NURSERY].colour);
    lbox(fc, FACE_EAST,  0.36f, 0.62f, 0.05f, 0.12f, 0.24f, 0.70f, ROOMS[NURSERY].colour);
    seated_person(fc, FACE_EAST, C_MIDWIFE, C_MIDWIFE, C_FAIR_HAIR, false);
    lbox(fc, FACE_EAST, 0, 0.96f, 0.12f, 0.34f, 0.14f, 0.18f, C_PINK);              // baby in her arms
    lbox(fc, FACE_EAST, 0.12f, 1.00f, 0.12f, 0.13f, 0.13f, 0.13f, C_SKIN);

    // The midwife, checking on the cots
    draw_person(v3(-13.35f, 0, 2.15f), FACE_EAST, outfit(C_MIDWIFE, C_MIDWIFE, true), false, 0);

    // Bunting on the outside wall
    if (!lowWest) {
        Color flags[4] = { C_PINK, C_BLUE, C_LEMON, C_MINT };
        draw_box(v3(WEST_X + 0.12f, 2.32f, 1.9f), v3(0.01f, 0.01f, 3.9f), C_DARK);
        for (int i = 0; i < 12; i++) {
            draw_box(v3(WEST_X + 0.13f, 2.22f, 0.15f + i * 0.32f), v3(0.01f, 0.18f, 0.16f), flags[i % 4]);
        }
    }

    // Proud visitors at the window, one with a balloon
    Vector3 dad = v3(-9.05f, 0, 0.95f), mum = v3(-9.05f, 0, 1.75f);
    draw_person(dad, FACE_WEST, outfit((Color){ 90, 120, 90, 255 }, C_TROUSERS, false), false, 0);
    draw_person(mum, FACE_WEST, outfit((Color){ 190, 90, 110, 255 }, C_NAVY, false), false, 0);
    float bob = 0.05f * sinf(now * 1.5f);
    Vector3 hand = v3(mum.x, 0.56f, mum.z + 0.31f), balloon = v3(mum.x + 0.1f, 2.35f + bob, mum.z + 0.45f);
    DrawLine3D(hand, v3(balloon.x, balloon.y - 0.24f, balloon.z), shade(C_DARK, 1.0f));
    draw_box(balloon, v3(0.34f, 0.42f, 0.34f), (Color){ 240, 110, 160, 255 });
    draw_box(v3(balloon.x, balloon.y - 0.23f, balloon.z), v3(0.06f, 0.05f, 0.06f), (Color){ 240, 110, 160, 255 });

    // The viewing window's frame, with a sign on top facing the corridor
    float wx = WING_X;
    draw_box(v3(wx, 1.75f, 0.2f), v3(0.14f, 1.20f, 0.10f), C_CARD);
    draw_box(v3(wx, 1.75f, 2.95f), v3(0.14f, 1.20f, 0.10f), C_CARD);
    draw_box(v3(wx, 1.75f, 1.575f), v3(0.12f, 1.20f, 0.06f), C_CARD);
    draw_box(v3(wx, 2.40f, 1.575f), v3(0.16f, 0.12f, 2.85f), C_CARD);
    draw_box(v3(wx + 0.09f, 2.62f, 1.575f), v3(0.03f, 0.34f, 1.80f), ROOMS[NURSERY].colour);
    sign_quad(viewingSign.texture, v3(wx + 0.11f, 2.77f, 2.45f), v3(wx + 0.11f, 2.77f, 0.70f),
                                   v3(wx + 0.11f, 2.47f, 0.70f), v3(wx + 0.11f, 2.47f, 2.45f));

    hanging_sign(NURSERY, -13.1f, 3.55f);
}

// ------------------------------------------------------------------------
// 7. Admin office: desks with people typing, filing cabinets, a printer
// ------------------------------------------------------------------------

static void desk(Vector3 p, bool mug)
{
    float y = 0.75f;
    shadow(p.x, p.z, 1.5f, 0.8f);
    draw_box(v3(p.x, y, p.z), v3(1.40f, 0.05f, 0.70f), C_WOOD);
    draw_box(v3(p.x - 0.66f, y / 2, p.z), v3(0.05f, y, 0.66f), shade(C_WOOD, 0.9f));
    draw_box(v3(p.x + 0.66f, y / 2, p.z), v3(0.05f, y, 0.66f), shade(C_WOOD, 0.9f));
    draw_box(v3(p.x, 0.45f, p.z - 0.30f), v3(1.30f, 0.50f, 0.03f), shade(C_WOOD, 0.9f));
    computer(v3(p.x, 0, p.z - 0.15f), FACE_SOUTH, y);
    draw_box(v3(p.x, y + 0.035f, p.z + 0.15f), v3(0.46f, 0.02f, 0.15f), C_DARK);    // keyboard
    draw_box(v3(p.x + 0.35f, y + 0.035f, p.z + 0.15f), v3(0.06f, 0.02f, 0.09f), C_DARK); // mouse
    draw_box(v3(p.x - 0.45f, y + 0.04f, p.z), v3(0.26f, 0.03f, 0.34f), C_CARD);     // papers
    if (mug) draw_box(v3(p.x + 0.5f, y + 0.07f, p.z - 0.1f), v3(0.08f, 0.10f, 0.08f), C_EMERGENCY);
}

static void draw_admin(void)
{
    // Two desks; the staff face their screens, away from the camera
    Vector3 d1 = v3(-14.7f, 0, 5.5f), d2 = v3(-12.5f, 0, 5.5f);
    desk(d1, true);
    desk(d2, false);
    Vector3 s1 = v3(d1.x, 0, 6.2f), s2 = v3(d2.x, 0, 6.2f);
    chair(s1, FACE_NORTH, C_DARK);
    chair(s2, FACE_NORTH, C_DARK);
    seated_person(s1, FACE_NORTH, C_SHIRT, C_NAVY, C_HAIR, true);
    seated_person(s2, FACE_NORTH, C_NAVY, C_TROUSERS, C_RED_HAIR, true);

    // Filing cabinets along the outside wall
    for (int i = 0; i < 3; i++) {
        float z = 4.5f + i * 0.62f;
        draw_box(v3(-16.12f, 0.65f, z), v3(0.55f, 1.30f, 0.58f), C_CABINET);
        for (int k = 0; k < 4; k++) {
            draw_box(v3(-15.84f, 0.22f + k * 0.31f, z), v3(0.02f, 0.02f, 0.22f), C_DARK);
        }
    }

    // Printer by the door, with paper in the tray and its panel lit
    float px = -10.45f, pz = 7.05f;
    shadow(px, pz, 0.8f, 0.6f);
    draw_box(v3(px, 0.45f, pz), v3(0.70f, 0.90f, 0.55f), (Color){ 215, 215, 210, 255 });
    draw_box(v3(px, 0.93f, pz), v3(0.72f, 0.06f, 0.57f), C_DARK);
    draw_box(v3(px - 0.1f, 0.70f, pz - 0.29f), v3(0.40f, 0.04f, 0.10f), C_CARD);
    draw_lit_box(v3(px + 0.22f, 0.97f, pz - 0.15f), v3(0.16f, 0.02f, 0.10f), (Color){ 90, 220, 140, 255 });

    draw_plant(-10.3f, 4.45f);
    shadow(-11.3f, 7.25f, 0.4f, 0.4f);
    draw_box(v3(-11.3f, 0.20f, 7.25f), v3(0.30f, 0.40f, 0.30f), C_DARK);            // bin

    // Notice board and a window on the outside wall
    if (!lowWest) {
        draw_box(v3(WEST_X + 0.12f, 1.70f, 6.45f), v3(0.04f, 0.80f, 1.10f), (Color){ 180, 140, 90, 255 });
        Color notes[5] = { C_CARD, C_LEMON, C_PINK, C_BLUE, C_CARD };
        for (int i = 0; i < 5; i++) {
            draw_box(v3(WEST_X + 0.15f, 1.55f + (i % 2) * 0.32f, 6.05f + i * 0.2f),
                     v3(0.01f, 0.20f, 0.16f), notes[i]);
        }
    }
    hanging_sign(ADMIN, -13.1f, 7.15f);
}

// ------------------------------------------------------------------------
// The functions view3d.c calls
// ------------------------------------------------------------------------

static void make_room_signs(void)
{
    for (int r = 1; r <= ROOM_COUNT; r++) {
        int w = MeasureText(ROOMS[r].name, 48) + 60;
        roomSign[r] = make_sign(ROOMS[r].name, w, 92, 48, ROOMS[r].colour, WHITE);
        roomSignW[r] = 0.46f * w / 92.0f;
    }
    viewingSign = make_sign("BABY VIEWING", 400, 70, 40, ROOMS[NURSERY].colour, WHITE);
    xraySign    = make_sign("CAUTION X-RAYS", 384, 128, 40, (Color){ 245, 200, 40, 255 }, C_DARK);
    metalSign   = make_sign("NO METAL", 384, 128, 56, C_EMERGENCY, WHITE);
    scaleScreen = make_sign("72.4 kg", 160, 72, 36, (Color){ 20, 40, 30, 255 }, (Color){ 120, 255, 160, 255 });
    calmPatient.severity = 3;
    unwellPatient.severity = 2;
    ready = true;
}

void rooms3d_prepare(const Sim *sim)
{
    (void)sim;
    if (!ready) make_room_signs();
}

void rooms3d_draw(const Sim *sim, Vector3 camPos, Vector3 camTarget)
{
    night = (sim->shift == SHIFT_NIGHT);
    now = (float)GetTime();

    // Outside walls facing the camera are drawn low, like A&E's
    float lx = camPos.x - camTarget.x, lz = camPos.z - camTarget.z;
    float len = sqrtf(lx * lx + lz * lz);
    if (len < 0.001f) len = 0.001f;
    lowNorth = lz / len < -0.30f;
    lowSouth = lz / len >  0.30f;
    lowWest  = lx / len < -0.35f;
    lowEast  = lx / len >  0.35f;

    draw_shell();
    draw_ward();
    draw_xray();
    draw_mri();
    draw_bloods();
    draw_theatre();
    draw_nursery();
    draw_admin();
}

void rooms3d_draw_glass(void)
{
    if (!ready) return;
    Color g = shade(C_GLASS, 1.0f);

    // Nursery viewing window, above the half wall
    float x = WING_X;
    quad(v3(x, PART_H, 0.25f), v3(x, PART_H, 2.9f), v3(x, 2.34f, 2.9f), v3(x, 2.34f, 0.25f), g);

    // The windows in front of the X-ray and MRI control desks
    quad(v3(-8.0f, 1.0f, -9.1f), v3(-8.0f, 1.0f, -7.7f), v3(-8.0f, 2.0f, -7.7f), v3(-8.0f, 2.0f, -9.1f), g);
    quad(v3(0.95f, 1.0f, -9.1f), v3(0.95f, 1.0f, -7.7f), v3(0.95f, 2.0f, -7.7f), v3(0.95f, 2.0f, -9.1f), g);

    // The incubator's clear hood
    glass_box(v3(-15.4f, 1.11f, 0.75f), v3(0.98f, 0.60f, 0.60f));
}

void rooms3d_lights(const Sim *sim)
{
    bool dark = (sim->shift == SHIFT_NIGHT);
    if (!dark) {
        // Daytime: soft light from the ceiling in every room
        for (int r = 1; r <= ROOM_COUNT; r++) {
            float cx = (ROOMS[r].x0 + ROOMS[r].x1) / 2, cz = (ROOMS[r].z0 + ROOMS[r].z1) / 2;
            glow(cx, cz, 2.6f, (Color){ 255, 255, 255, 20 });
        }
    } else {
        // Night: a lamp over each ward patient, the nursery's night light, desk lamps
        for (int i = 0; i < 3; i++) glow(-15.3f, WARD_BED_Z[i], 1.4f, (Color){ 255, 200, 120, 65 });
        glow(-13.0f, 1.9f, 2.6f, (Color){ 255, 180, 210, 55 });
        glow(-14.7f, 5.6f, 1.3f, (Color){ 255, 225, 160, 50 });
        glow(-12.5f, 5.6f, 1.3f, (Color){ 255, 225, 160, 50 });
        glow(-8.6f, -8.4f, 1.2f, (Color){ 140, 190, 255, 45 });          // X-ray desk screens
        glow(1.5f, -8.4f, 1.2f, (Color){ 140, 190, 255, 45 });           // MRI desk screens
        glow(MRI_X, -9.5f, 1.5f, (Color){ 140, 190, 255, 40 });          // light from the tunnel
        glow(-13.0f, -2.2f, 2.2f, (Color){ 255, 240, 200, 35 });
        glow(-8.9f, 1.3f, 1.4f, (Color){ 255, 180, 210, 40 });           // corridor by the window
    }
    // The theatre lights are always on, brightest on the table
    glow(5.4f, -10.0f, 1.8f, (Color){ 255, 255, 235, dark ? 110 : 60 });
    if (xray_exposing()) glow(-5.6f, -10.2f, 1.2f, (Color){ 255, 240, 170, 90 });
}

void rooms3d_close(void)
{
    if (!ready) return;
    for (int r = 1; r <= ROOM_COUNT; r++) UnloadRenderTexture(roomSign[r]);
    UnloadRenderTexture(viewingSign);
    UnloadRenderTexture(xraySign);
    UnloadRenderTexture(metalSign);
    UnloadRenderTexture(scaleScreen);
    ready = false;
}

float rooms3d_focus(int room, Vector3 *centre)
{
    if (room < 1 || room > ROOM_COUNT) room = AE;
    const Room *r = &ROOMS[room];
    *centre = v3((r->x0 + r->x1) / 2, 0.4f, (r->z0 + r->z1) / 2);
    return r->camDist;
}

const char *rooms3d_name_at(float x, float z)
{
    for (int r = 0; r <= ROOM_COUNT; r++) {
        if (x >= ROOMS[r].x0 && x <= ROOMS[r].x1 && z >= ROOMS[r].z0 && z <= ROOMS[r].z1) {
            return ROOMS[r].name;
        }
    }
    if (x > EAST_X) return "AMBULANCE BAY";
    if (x >= WEST_X && z >= NORTH_Z && z <= SOUTH_Z) return "CORRIDOR";
    return "OUTSIDE";
}