// outside3d.c - the outside of the hospital in 3D.
//
// Two jobs:
//
// 1. The grounds in front of the building: a car park whose spaces fill up at
//    visiting time and empty at night, a lane from the road with a barrier and
//    a pay machine, a path to the main entrance with visitors walking along
//    it, flower beds, three flags, benches, a bus stop and a sign for the
//    hospital.
//
// 2. The building seen from outside. When you look at a floor upstairs, the
//    floors below it are drawn closed up: outside walls with rows of windows
//    (lit at night), a coloured band at each floor, and the columns holding
//    up the floors that overhang the front of A&E. From the "outside" view the
//    whole building is closed up, with its roof: the lift's machine room, air
//    conditioning units with spinning fans, solar panels, and the hospital's
//    name in big letters.
//
// The time of day comes from view3d.c (view3d_darkness, view3d_hour), so the
// windows light up as it gets dark and the car park follows the clock.

#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "rlgl.h"
#include "draw.h"            // colours
#include "view3d_parts.h"
#include "outside3d.h"

// ---- Layout ----
#define AE_DOOR_Z0   2.4f    // A&E's entrance doors in the right-hand wall (view3d.c draws them)
#define AE_DOOR_Z1   4.6f
#define MAIN_DOOR_X -3.0f    // the main entrance, in the front of A&E under the overhang
#define PATH_X      -3.0f    // the path from the car park to the main entrance
#define PARK_X0    -15.5f    // the car park
#define PARK_X1      6.5f
#define PARK_Z0     11.6f
#define PARK_Z1     20.0f
#define ROW_A_Z     13.1f    // the middle of each row of spaces
#define ROW_B_Z     18.3f
#define AISLE_Z     15.7f    // the lane between the rows, and out to the road
#define BAYS_PER_ROW 8
#define ROAD_X0     13.9f    // the road's near edge (as in view3d.c)

// ---- Colours ----
#define C_FACADE    (Color){ 232, 226, 214, 255 }   // pale stone, upstairs
#define C_BASE      (Color){ 206, 210, 216, 255 }   // light grey cladding, the ground floor
#define C_BAND      (Color){  62, 116, 196, 255 }   // blue band at each floor
#define C_ROOF      (Color){ 150, 154, 160, 255 }
#define C_ASPH      (Color){  98, 102, 110, 255 }
#define C_PAVING    (Color){ 205, 200, 190, 255 }

static bool ready = false;
static float mainDoorOpen = 0.0f;       // the main entrance's sliding doors: 0 shut, 1 open
static RenderTexture2D nameSign, mainSign, parkSign, busSign;

// ------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------

// A random-looking number from a whole number: the same input always gives
// the same answer (so a window is always lit or always dark, a space always
// has the same car).
static unsigned int scramble(unsigned int x)
{
    x ^= x >> 16;  x *= 0x7feb352du;
    x ^= x >> 15;  x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A flat rectangle on the ground (slightly above it, so it isn't hidden by the grass)
static void ground(float x0, float z0, float x1, float z1, float y, Color c)
{
    Color s = shade(c, 1.0f);
    quad(v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1), s);
}

// ------------------------------------------------------------------------
// The building from outside
// ------------------------------------------------------------------------

// A window in an outside wall: a white frame and glass that reflects the sky
// by day and, at night, is lit warm yellow in most rooms (not all: some
// rooms are empty, or someone's asleep).
static void window_pane(Vector3 c, bool alongX, float outward, unsigned int id)
{
    float dark = view3d_darkness();
    bool lit = view3d_lamps_on() && (scramble(id) % 100 < 70);
    Vector3 frame = alongX ? v3(1.34f, 1.34f, 0.06f) : v3(0.06f, 1.34f, 1.34f);
    Vector3 glass = alongX ? v3(1.20f, 1.20f, 0.04f) : v3(0.04f, 1.20f, 1.20f);
    Vector3 off = alongX ? v3(0, 0, outward * 0.17f) : v3(outward * 0.17f, 0, 0);
    Vector3 off2 = alongX ? v3(0, 0, outward * 0.20f) : v3(outward * 0.20f, 0, 0);
    draw_box(v3(c.x + off.x, c.y, c.z + off.z), frame, C_CARD);
    if (lit) {
        draw_lit_box(v3(c.x + off2.x, c.y, c.z + off2.z), glass, (Color){ 255, 214, 140, 255 });
    } else {
        Color sky = { (unsigned char)(150 - 120 * dark), (unsigned char)(185 - 150 * dark), (unsigned char)(215 - 160 * dark), 255 };
        draw_lit_box(v3(c.x + off2.x, c.y, c.z + off2.z), glass, sky);
        // a bar across the middle
        Vector3 bar = alongX ? v3(1.2f, 0.05f, 0.05f) : v3(0.05f, 0.05f, 1.2f);
        draw_box(v3(c.x + off2.x, c.y, c.z + off2.z), bar, C_CARD);
    }
}

// An outside wall along x at depth z, from x0 to x1, for one floor (from y0,
// FLOOR_H high), with windows every 2.2 m. `outward` is +1 or -1: which way
// is outside. Windows aren't put where skip0..skip1 is (a doorway).
static void facade_x(float z, float x0, float x1, float y0, float outward, Color c, int floor, float skip0, float skip1)
{
    draw_box(v3((x0 + x1) / 2, y0 + FLOOR_H / 2, z), v3(x1 - x0, FLOOR_H, 0.3f), c);
    for (float x = x0 + 1.4f; x < x1 - 0.8f; x += 2.2f) {
        if (x + 0.7f > skip0 && x - 0.7f < skip1) continue;
        window_pane(v3(x, y0 + 1.6f, z), true, outward, (unsigned int)(floor * 1000 + (int)(x * 10) + (int)(z * 77)));
    }
    draw_box(v3((x0 + x1) / 2, y0 + FLOOR_H - 0.12f, z + outward * 0.17f), v3(x1 - x0 + 0.02f, 0.24f, 0.06f), C_BAND);
}

static void facade_z(float x, float z0, float z1, float y0, float outward, Color c, int floor, float skip0, float skip1)
{
    draw_box(v3(x, y0 + FLOOR_H / 2, (z0 + z1) / 2), v3(0.3f, FLOOR_H, z1 - z0), c);
    for (float z = z0 + 1.4f; z < z1 - 0.8f; z += 2.2f) {
        if (z + 0.7f > skip0 && z - 0.7f < skip1) continue;
        window_pane(v3(x, y0 + 1.6f, z), false, outward, (unsigned int)(floor * 1000 + (int)(z * 10) + (int)(x * 53)));
    }
    draw_box(v3(x + outward * 0.17f, y0 + FLOOR_H - 0.12f, (z0 + z1) / 2), v3(0.06f, 0.24f, z1 - z0 + 0.02f), C_BAND);
}

// The ground floor, closed up. Its outline isn't a plain rectangle: A&E's
// front is further back than the left wing's.
static void ground_storey(void)
{
    float y = 0.0f;
    facade_x(BUILDING_Z0, BUILDING_X0, BUILDING_X1, y, -1, C_BASE, 0, 99, 99);              // back
    facade_z(BUILDING_X0, BUILDING_Z0, BUILDING_Z1, y, -1, C_BASE, 0, 99, 99);              // left
    facade_x(BUILDING_Z1, BUILDING_X0, -8.1f, y, 1, C_BASE, 0, 99, 99);                     // front of the wing
    facade_z(-8.1f, AE_FRONT_Z, BUILDING_Z1, y, 1, C_BASE, 0, 99, 99);                      // the step back
    // A&E's right-hand wall: a gap for the entrance doors (view3d.c draws them)
    draw_box(v3(BUILDING_X1, FLOOR_H / 2, (BUILDING_Z0 + AE_DOOR_Z0) / 2), v3(0.3f, FLOOR_H, AE_DOOR_Z0 - BUILDING_Z0), C_BASE);
    draw_box(v3(BUILDING_X1, FLOOR_H / 2, (AE_DOOR_Z1 + AE_FRONT_Z) / 2), v3(0.3f, FLOOR_H, AE_FRONT_Z - AE_DOOR_Z1), C_BASE);
    draw_box(v3(BUILDING_X1, 2.75f, (AE_DOOR_Z0 + AE_DOOR_Z1) / 2), v3(0.3f, 0.9f, AE_DOOR_Z1 - AE_DOOR_Z0), C_BASE);
    for (float z = BUILDING_Z0 + 1.4f; z < AE_DOOR_Z0 - 0.8f; z += 2.2f) {
        window_pane(v3(BUILDING_X1, 1.6f, z), false, 1, (unsigned int)(z * 31));
    }
    draw_box(v3(BUILDING_X1 + 0.17f, FLOOR_H - 0.12f, (BUILDING_Z0 + AE_FRONT_Z) / 2), v3(0.06f, 0.24f, AE_FRONT_Z - BUILDING_Z0), C_BAND);
    // A&E's front, with the main entrance: glass doors and a sign
    float d0 = MAIN_DOOR_X - 0.9f, d1 = MAIN_DOOR_X + 0.9f;
    facade_x(AE_FRONT_Z, -8.1f, d0, y, 1, C_BASE, 0, 99, 99);
    facade_x(AE_FRONT_Z, d1, BUILDING_X1, y, 1, C_BASE, 0, 99, 99);
    draw_box(v3(MAIN_DOOR_X, 2.85f, AE_FRONT_Z), v3(1.8f, 0.7f, 0.3f), C_BASE);
    outside3d_draw_main_doors();
}

void outside3d_draw_main_doors(void)
{
    if (!ready) outside3d_prepare();
    float h = 2.3f;
    draw_box(v3(MAIN_DOOR_X - 0.95f, h / 2, AE_FRONT_Z), v3(0.14f, h, 0.26f), C_STEEL);    // frame
    draw_box(v3(MAIN_DOOR_X + 0.95f, h / 2, AE_FRONT_Z), v3(0.14f, h, 0.26f), C_STEEL);
    draw_box(v3(MAIN_DOOR_X, h + 0.1f, AE_FRONT_Z), v3(2.04f, 0.2f, 0.26f), C_STEEL);
    draw_box(v3(MAIN_DOOR_X, 2.72f, AE_FRONT_Z + 0.18f), v3(2.4f, 0.4f, 0.05f), C_BRAND);   // the sign
    sign_facing_front(mainSign.texture, MAIN_DOOR_X, 2.72f, AE_FRONT_Z + 0.21f, 2.34f, 0.34f);
    // the two door panels' frames, slid apart as far as they're open
    float slide = mainDoorOpen * 0.8f;
    for (int s = -1; s <= 1; s += 2) {
        float x = MAIN_DOOR_X + s * (0.45f + slide);
        draw_box(v3(x, 0.04f, AE_FRONT_Z), v3(0.88f, 0.08f, 0.06f), C_STEEL);
        draw_box(v3(x, 2.24f, AE_FRONT_Z), v3(0.88f, 0.06f, 0.06f), C_STEEL);
        draw_box(v3(x - s * 0.42f, 1.14f, AE_FRONT_Z), v3(0.05f, 2.2f, 0.06f), C_STEEL);
    }
}

void outside3d_draw_main_door_glass(void)
{
    Color g = shade(C_GLASS, 1.0f);
    float slide = mainDoorOpen * 0.8f;
    for (int s = -1; s <= 1; s += 2) {
        float x0 = MAIN_DOOR_X + s * (0.03f + slide), x1 = MAIN_DOOR_X + s * (0.87f + slide);
        quad(v3(x0, 0.08f, AE_FRONT_Z), v3(x1, 0.08f, AE_FRONT_Z), v3(x1, 2.21f, AE_FRONT_Z), v3(x0, 2.21f, AE_FRONT_Z), g);
    }
}

// One floor upstairs, closed up: a plain rectangle
static void upper_storey(int floor)
{
    float y = floor * FLOOR_H;
    facade_x(BUILDING_Z0, BUILDING_X0, BUILDING_X1, y, -1, C_FACADE, floor, 99, 99);
    facade_x(BUILDING_Z1, BUILDING_X0, BUILDING_X1, y,  1, C_FACADE, floor, 99, 99);
    facade_z(BUILDING_X0, BUILDING_Z0, BUILDING_Z1, y, -1, C_FACADE, floor, 99, 99);
    facade_z(BUILDING_X1, BUILDING_Z0, BUILDING_Z1, y,  1, C_FACADE, floor, 99, 99);
}

// Columns under the floors that overhang the front of A&E, and the underside
// of the overhang
static void overhang(void)
{
    for (int i = 0; i < 5; i++) {
        float x = -6.6f + i * 3.6f;
        draw_box(v3(x, FLOOR_H / 2, BUILDING_Z1 - 0.25f), v3(0.32f, FLOOR_H, 0.32f), C_FACADE);
    }
    draw_box(v3(0, FLOOR_H - 0.1f, (AE_FRONT_Z + BUILDING_Z1) / 2), v3(16.2f, 0.2f, BUILDING_Z1 - AE_FRONT_Z), C_FACADE);
    // lights in the underside of the canopy, lit as it gets dark
    for (int i = 0; i < 4; i++) {
        Vector3 p = v3(-5.0f + i * 3.6f, FLOOR_H - 0.22f, (AE_FRONT_Z + BUILDING_Z1) / 2);
        if (view3d_lamps_on()) draw_lit_box(p, v3(0.5f, 0.04f, 0.3f), (Color){ 255, 235, 190, 255 });
        else                   draw_box(p, v3(0.5f, 0.04f, 0.3f), C_CARD);
    }
}

// An air conditioning unit on the roof, its fan spinning
static void ac_unit(float x, float y, float z)
{
    draw_box(v3(x, y + 0.45f, z), v3(1.2f, 0.9f, 1.0f), (Color){ 200, 204, 210, 255 });
    draw_box(v3(x, y + 0.91f, z), v3(0.9f, 0.02f, 0.9f), C_DARK);
    float a = (float)GetTime() * 8.0f + x;
    for (int k = 0; k < 3; k++) {
        float b = a + k * 2.094f;
        draw_box(v3(x + cosf(b) * 0.2f, y + 0.93f, z + sinf(b) * 0.2f), v3(0.16f, 0.02f, 0.16f), (Color){ 120, 125, 135, 255 });
    }
}

// The roof: a flat roof with a low wall round the edge and things on top
static void roof(void)
{
    float y = FLOORS * FLOOR_H;
    draw_box(v3((BUILDING_X0 + BUILDING_X1) / 2, y + 0.05f, (BUILDING_Z0 + BUILDING_Z1) / 2),
             v3(BUILDING_X1 - BUILDING_X0, 0.1f, BUILDING_Z1 - BUILDING_Z0), C_ROOF);
    // the low wall round the edge
    draw_box(v3((BUILDING_X0 + BUILDING_X1) / 2, y + 0.3f, BUILDING_Z0), v3(BUILDING_X1 - BUILDING_X0 + 0.3f, 0.6f, 0.3f), C_FACADE);
    draw_box(v3((BUILDING_X0 + BUILDING_X1) / 2, y + 0.3f, BUILDING_Z1), v3(BUILDING_X1 - BUILDING_X0 + 0.3f, 0.6f, 0.3f), C_FACADE);
    draw_box(v3(BUILDING_X0, y + 0.3f, (BUILDING_Z0 + BUILDING_Z1) / 2), v3(0.3f, 0.6f, BUILDING_Z1 - BUILDING_Z0), C_FACADE);
    draw_box(v3(BUILDING_X1, y + 0.3f, (BUILDING_Z0 + BUILDING_Z1) / 2), v3(0.3f, 0.6f, BUILDING_Z1 - BUILDING_Z0), C_FACADE);
    // the lift's machine room over the lifts, with a door
    draw_box(v3(LIFT_X, y + 1.1f, 6.6f), v3(2.4f, 2.2f, 2.0f), C_FACADE);
    draw_box(v3(LIFT_X, y + 2.25f, 6.6f), v3(2.6f, 0.1f, 2.2f), C_BAND);
    draw_box(v3(LIFT_X, y + 0.9f, 5.58f), v3(0.8f, 1.8f, 0.04f), C_STEEL);
    // air conditioning units, vents and solar panels
    ac_unit(-1.0f, y, -10.5f);
    ac_unit(1.6f, y, -10.5f);
    ac_unit(4.2f, y, -10.5f);
    for (int i = 0; i < 4; i++) {
        draw_box(v3(-6.0f + i * 1.5f, y + 0.4f, -1.0f), v3(0.25f, 0.8f, 0.25f), C_STEEL);
        draw_box(v3(-6.0f + i * 1.5f, y + 0.84f, -1.0f), v3(0.4f, 0.08f, 0.4f), C_STEEL);
    }
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 5; col++) {
            float px = -15.0f + col * 1.8f, pz = -11.5f + row * 2.0f;
            draw_box(v3(px, y + 0.25f, pz + 0.3f), v3(0.06f, 0.4f, 0.06f), C_STEEL);
            // a panel tilted towards the sun
            Vector3 a = v3(px - 0.8f, y + 0.25f, pz + 0.7f), b = v3(px + 0.8f, y + 0.25f, pz + 0.7f);
            Vector3 c = v3(px + 0.8f, y + 0.75f, pz - 0.5f), d = v3(px - 0.8f, y + 0.75f, pz - 0.5f);
            quad(a, b, c, d, shade((Color){ 40, 60, 110, 255 }, 1.0f));
            DrawLine3D(v3(px, y + 0.26f, pz + 0.69f), v3(px, y + 0.76f, pz - 0.49f), shade(C_STEEL, 1.0f));
        }
    }
    // The hospital's name in big letters on a frame at the front, and a red cross
    float sy = y + 1.5f, sz = BUILDING_Z1 - 0.4f;
    draw_box(v3(-1.0f, y + 0.6f, sz), v3(0.1f, 1.2f, 0.1f), C_STEEL);
    draw_box(v3(5.0f, y + 0.6f, sz), v3(0.1f, 1.2f, 0.1f), C_STEEL);
    draw_box(v3(2.0f, sy, sz), v3(7.4f, 1.1f, 0.1f), C_BRAND);
    sign_facing_front(nameSign.texture, 2.0f, sy, sz + 0.06f, 7.2f, 0.95f);
    Vector3 cross = v3(-3.0f, sy, sz);
    draw_box(cross, v3(1.1f, 1.1f, 0.1f), C_CARD);
    draw_lit_box(v3(cross.x, cross.y, cross.z + 0.06f), v3(0.8f, 0.26f, 0.02f), C_EMERGENCY);
    draw_lit_box(v3(cross.x, cross.y, cross.z + 0.06f), v3(0.26f, 0.8f, 0.02f), C_EMERGENCY);
    // a red light on the top, for aircraft (blinks)
    if (fmodf((float)GetTime(), 1.5f) < 0.4f) draw_lit_box(v3(LIFT_X, y + 2.4f, 6.6f), v3(0.14f, 0.14f, 0.14f), (Color){ 255, 50, 50, 255 });
}

// ------------------------------------------------------------------------
// The grounds
// ------------------------------------------------------------------------

// A small car facing +z (angle 0) or -z (flip).
static void car(float x, float z, bool flip, Color body)
{
    float d = flip ? -1.0f : 1.0f;
    shadow(x, z, 1.5f, 2.5f);
    draw_box(v3(x, 0.38f, z), v3(1.35f, 0.5f, 2.3f), body);                          // body
    draw_box(v3(x, 0.82f, z - d * 0.15f), v3(1.2f, 0.42f, 1.2f), body);               // cabin
    draw_box(v3(x, 0.84f, z - d * 0.15f), v3(1.22f, 0.3f, 1.0f), (Color){ 60, 75, 95, 255 });   // windows
    draw_box(v3(x, 0.84f, z + d * 0.46f), v3(1.1f, 0.3f, 0.03f), (Color){ 140, 170, 200, 255 }); // windscreen
    for (int i = 0; i < 4; i++) {                                                     // wheels
        float wx = x + ((i % 2) ? 0.64f : -0.64f), wz = z + ((i < 2) ? 0.75f : -0.75f);
        draw_box(v3(wx, 0.18f, wz), v3(0.14f, 0.36f, 0.36f), C_DARK);
    }
    bool lights = view3d_lamps_on();
    Color head = lights ? (Color){ 255, 250, 220, 255 } : (Color){ 230, 230, 230, 255 };
    draw_box(v3(x - 0.45f, 0.45f, z + d * 1.16f), v3(0.25f, 0.1f, 0.02f), head);
    draw_box(v3(x + 0.45f, 0.45f, z + d * 1.16f), v3(0.25f, 0.1f, 0.02f), head);
    draw_box(v3(x - 0.45f, 0.45f, z - d * 1.16f), v3(0.25f, 0.1f, 0.02f), (Color){ 200, 40, 40, 255 });
    draw_box(v3(x + 0.45f, 0.45f, z - d * 1.16f), v3(0.25f, 0.1f, 0.02f), (Color){ 200, 40, 40, 255 });
}

static float bay_x(int i) { return PARK_X0 + 1.4f + i * 2.6f; }

// How full the car park is at this hour, 0 to 100: busy at visiting time,
// nearly empty in the small hours.
static int car_park_busy(float hour)
{
    if (hour < 6.0f)  return 15;
    if (hour < 9.0f)  return 45;
    if (hour < 14.0f) return 70;
    if (hour < 20.0f) return 90;          // visiting time
    if (hour < 22.0f) return 55;
    return 25;
}

static void car_park(void)
{
    // Tarmac, white lines and the lane out to the road
    ground(PARK_X0, PARK_Z0, PARK_X1, PARK_Z1, -0.012f, C_ASPH);
    ground(PARK_X1, AISLE_Z - 1.0f, ROAD_X0, AISLE_Z + 1.0f, -0.008f, C_ASPH);
    Color line = (Color){ 235, 235, 235, 255 };
    for (int i = 0; i <= BAYS_PER_ROW; i++) {
        float x = bay_x(i) - 1.3f;
        ground(x - 0.04f, ROW_A_Z - 1.2f, x + 0.04f, ROW_A_Z + 1.2f, 0.0f, line);
        ground(x - 0.04f, ROW_B_Z - 1.2f, x + 0.04f, ROW_B_Z + 1.2f, 0.0f, line);
    }
    // two blue spaces for disabled drivers, by the path
    for (int i = 6; i < 8; i++) ground(bay_x(i) - 1.2f, ROW_A_Z - 1.15f, bay_x(i) + 1.2f, ROW_A_Z + 1.15f, -0.002f, (Color){ 70, 110, 190, 255 });
    // a zebra crossing where the lane crosses the path to A&E
    for (int k = 0; k < 5; k++) {
        float z = AISLE_Z - 0.9f + k * 0.42f;
        ground(8.3f, z, 13.1f, z + 0.22f, 0.004f, line);
    }
    // The cars: each space is full or empty depending on the hour
    Color colours[6] = { (Color){ 200, 50, 50, 255 }, (Color){ 40, 90, 170, 255 }, (Color){ 230, 230, 235, 255 },
                         (Color){ 50, 55, 60, 255 }, (Color){ 160, 165, 175, 255 }, (Color){ 60, 130, 80, 255 } };
    int busy = car_park_busy(view3d_hour());
    int hour = (int)view3d_hour();
    for (int row = 0; row < 2; row++) {
        for (int i = 0; i < BAYS_PER_ROW; i++) {
            unsigned int h = scramble((unsigned int)(row * 31 + i * 7 + hour / 2 * 1013));
            if ((int)(h % 100) >= busy) continue;
            car(bay_x(i), row ? ROW_B_Z : ROW_A_Z, row == 1, colours[(h >> 8) % 6]);
        }
    }
    // The barrier and pay machine
    draw_box(v3(7.2f, 0.5f, AISLE_Z - 1.2f), v3(0.3f, 1.0f, 0.3f), (Color){ 240, 200, 40, 255 });
    draw_box(v3(7.2f, 0.95f, AISLE_Z - 0.2f), v3(0.08f, 0.08f, 2.0f), C_EMERGENCY);
    draw_box(v3(PARK_X1 - 0.4f, 0.75f, ROW_A_Z + 1.5f), v3(0.5f, 1.5f, 0.4f), C_DARK);
    draw_lit_box(v3(PARK_X1 - 0.4f, 1.1f, ROW_A_Z + 1.71f), v3(0.3f, 0.2f, 0.01f), (Color){ 120, 200, 255, 255 });
    // the P sign
    draw_box(v3(PARK_X1 - 0.3f, 1.2f, AISLE_Z - 1.3f), v3(0.08f, 2.4f, 0.08f), C_STEEL);
    draw_box(v3(PARK_X1 - 0.3f, 2.3f, AISLE_Z - 1.3f), v3(0.8f, 0.8f, 0.06f), (Color){ 40, 90, 190, 255 });
    sign_facing_front(parkSign.texture, PARK_X1 - 0.3f, 2.3f, AISLE_Z - 1.26f, 0.7f, 0.7f);
    // car park lamps
    for (int i = 0; i < 3; i++) {
        float x = -11.0f + i * 7.0f;
        draw_box(v3(x, 1.8f, AISLE_Z), v3(0.1f, 3.6f, 0.1f), C_DARK);
        if (view3d_lamps_on()) draw_lit_box(v3(x, 3.6f, AISLE_Z), v3(0.5f, 0.12f, 0.3f), (Color){ 255, 225, 150, 255 });
        else                   draw_box(v3(x, 3.6f, AISLE_Z), v3(0.5f, 0.12f, 0.3f), C_STEEL);
    }
}

// Visitors walking between the car park and the main entrance. Each one
// walks in, spends a while inside, and walks back out again.
static void visitors(void)
{
    float hour = view3d_hour();
    int count = (hour >= 8.0f && hour < 21.0f) ? 6 : 2;       // fewer at night
    Color tops[6] = { (Color){ 190, 90, 110, 255 }, (Color){ 90, 120, 90, 255 }, (Color){ 80, 110, 170, 255 },
                      (Color){ 210, 160, 70, 255 }, (Color){ 140, 80, 150, 255 }, (Color){ 70, 150, 160, 255 } };
    float t0 = (float)GetTime();
    bool nearDoor = false;
    for (int i = 0; i < count; i++) {
        float period = 34.0f + i * 5.0f;
        float t = fmodf(t0 + i * 9.7f, period) / period;      // 0..1 round the loop
        // the route: from their car, along the aisle, up the path, and in
        // through the doors (they open as people come to them)
        float bx = bay_x((i * 3) % BAYS_PER_ROW);
        Vector3 pts[5] = { v3(bx, 0, AISLE_Z), v3(PATH_X + 0.3f, 0, AISLE_Z), v3(PATH_X + 0.3f, 0, BUILDING_Z1 + 0.5f),
                           v3(MAIN_DOOR_X + 0.3f, 0, AE_FRONT_Z + 0.4f), v3(MAIN_DOOR_X + 0.3f, 0, AE_FRONT_Z - 1.0f) };
        float lens[4], total = 0;
        for (int k = 0; k < 4; k++) {
            lens[k] = fabsf(pts[k + 1].x - pts[k].x) + fabsf(pts[k + 1].z - pts[k].z);
            total += lens[k];
        }
        float along;
        bool in;
        if (t < 0.4f)      { along = t / 0.4f; in = true; }
        else if (t < 0.6f) continue;                           // inside, visiting
        else               { along = (t - 0.6f) / 0.4f; in = false; }
        float d = (in ? along : 1.0f - along) * total;
        int k = 0;
        while (k < 3 && d > lens[k]) { d -= lens[k]; k++; }
        float f = lens[k] > 0 ? d / lens[k] : 0;
        Vector3 a = pts[k], b = pts[k + 1];
        Vector3 p = v3(a.x + (b.x - a.x) * f, 0, a.z + (b.z - a.z) * f);
        float dx = (b.x - a.x) * (in ? 1 : -1), dz = (b.z - a.z) * (in ? 1 : -1);
        Facing face = fabsf(dx) > fabsf(dz) ? (dx > 0 ? FACE_EAST : FACE_WEST) : (dz > 0 ? FACE_SOUTH : FACE_NORTH);
        float ddx = p.x - MAIN_DOOR_X, ddz = p.z - AE_FRONT_Z;
        if (ddx * ddx + ddz * ddz < 2.0f * 2.0f) nearDoor = true;
        draw_person(p, face, (Outfit){ tops[i], C_TROUSERS, false, false, false, false }, true, t0 * 9.0f + i);
        if (i % 3 == 0) draw_box(v3(p.x + 0.35f, 0.75f, p.z), v3(0.25f, 0.3f, 0.1f), (Color){ 240, 120, 160, 255 });   // flowers
    }
    mainDoorOpen += ((nearDoor ? 1.0f : 0.0f) - mainDoorOpen) * (1.0f - expf(-8.0f * GetFrameTime()));
}

// Flower beds, flags, benches, the bus stop and the hospital's sign
static void dressing(void)
{
    // the path from the car park to the main entrance
    ground(PATH_X - 0.6f, BUILDING_Z1, PATH_X + 0.6f, PARK_Z0, -0.006f, C_PAVING);
    ground(-8.0f, AE_FRONT_Z, 8.1f, BUILDING_Z1 + 0.4f, -0.007f, C_PAVING);       // paving under the overhang
    // flower beds either side of the path
    Color flowers[4] = { (Color){ 230, 80, 100, 255 }, (Color){ 250, 210, 70, 255 }, (Color){ 160, 110, 220, 255 }, WHITE };
    for (int side = 0; side < 2; side++) {
        float x0 = side ? PATH_X + 1.0f : -14.0f, x1 = side ? 5.0f : PATH_X - 1.0f;
        ground(x0, 8.4f, x1, 9.3f, -0.004f, (Color){ 110, 80, 55, 255 });
        for (float x = x0 + 0.2f; x < x1 - 0.1f; x += 0.35f) {
            int k = (int)(x * 7.3f) & 3;
            draw_box(v3(x, 0.12f, 8.65f + (k % 2) * 0.35f), v3(0.14f, 0.14f, 0.14f), flowers[k]);
            draw_box(v3(x, 0.05f, 8.65f + (k % 2) * 0.35f), v3(0.05f, 0.1f, 0.05f), (Color){ 70, 140, 70, 255 });
        }
    }
    // three flags, fluttering
    Color flagCols[3] = { C_BRAND, (Color){ 60, 160, 90, 255 }, (Color){ 230, 230, 235, 255 } };
    for (int i = 0; i < 3; i++) {
        float x = -7.0f + i * 0.9f, z = 9.8f;
        draw_box(v3(x, 2.5f, z), v3(0.07f, 5.0f, 0.07f), C_STEEL);
        for (int k = 0; k < 5; k++) {
            float wave = 0.08f * sinf((float)GetTime() * 4.0f + k * 0.9f + i);
            draw_box(v3(x + 0.12f + k * 0.18f, 4.6f, z + wave), v3(0.18f, 0.6f, 0.03f), flagCols[i]);
        }
    }
    // benches facing the flower beds
    for (int i = 0; i < 2; i++) {
        float x = (i == 0) ? -10.0f : 2.0f, z = 9.9f;
        draw_box(v3(x, 0.42f, z), v3(1.4f, 0.06f, 0.42f), C_DESK);
        draw_box(v3(x, 0.72f, z + 0.2f), v3(1.4f, 0.4f, 0.06f), C_DESK);
        draw_box(v3(x - 0.6f, 0.2f, z), v3(0.06f, 0.4f, 0.4f), C_DARK);
        draw_box(v3(x + 0.6f, 0.2f, z), v3(0.06f, 0.4f, 0.4f), C_DARK);
    }
    // The bus stop across the road: a shelter with a bench and a sign
    float bx = 18.9f, bz = 15.8f;
    draw_box(v3(bx + 0.4f, 1.2f, bz), v3(0.08f, 2.4f, 1.8f), (Color){ 170, 200, 220, 255 });   // back panel
    draw_box(v3(bx, 2.42f, bz), v3(0.9f, 0.06f, 1.9f), C_DARK);                                 // roof
    draw_box(v3(bx, 1.2f, bz - 0.92f), v3(0.8f, 2.4f, 0.05f), (Color){ 170, 200, 220, 255 });
    draw_box(v3(bx + 0.15f, 0.45f, bz), v3(0.35f, 0.06f, 1.4f), C_DESK);
    draw_box(v3(bx - 0.45f, 1.5f, bz + 1.0f), v3(0.06f, 3.0f, 0.06f), C_STEEL);
    draw_box(v3(bx - 0.45f, 2.8f, bz + 1.0f), v3(0.04f, 0.5f, 0.5f), (Color){ 40, 140, 70, 255 });
    sign_quad(busSign.texture, v3(bx - 0.48f, 3.0f, bz + 1.22f), v3(bx - 0.48f, 3.0f, bz + 0.78f),
                               v3(bx - 0.48f, 2.6f, bz + 0.78f), v3(bx - 0.48f, 2.6f, bz + 1.22f));
    // someone waiting for the bus in the daytime
    float hour = view3d_hour();
    if (hour > 7.0f && hour < 22.0f) person_seated(v3(bx + 0.05f, 0, bz - 0.3f), FACE_WEST, (Color){ 160, 70, 60, 255 }, C_TROUSERS, C_HAIR, false);
    // A bike rack by the entrance
    for (int i = 0; i < 4; i++) {
        float x = 1.2f + i * 0.5f;
        draw_box(v3(x, 0.4f, 7.2f), v3(0.05f, 0.8f, 0.05f), C_STEEL);
        draw_box(v3(x, 0.8f, 7.2f), v3(0.05f, 0.05f, 0.5f), C_STEEL);
    }
    draw_box(v3(1.45f, 0.35f, 7.45f), v3(0.04f, 0.7f, 1.3f), C_EMERGENCY);                     // a red bike
}

// ------------------------------------------------------------------------
// The functions view3d.c calls
// ------------------------------------------------------------------------

void outside3d_prepare(void)
{
    if (ready) return;
    nameSign = make_sign("GENERAL HOSPITAL", 900, 120, 90, C_BRAND, WHITE);
    mainSign = make_sign("MAIN ENTRANCE", 560, 80, 48, C_BRAND, WHITE);
    parkSign = make_sign("P", 128, 128, 110, (Color){ 40, 90, 190, 255 }, WHITE);
    busSign  = make_sign("BUS", 128, 128, 60, (Color){ 40, 140, 70, 255 }, WHITE);
    ready = true;
}

void outside3d_draw(int viewFloor)
{
    if (!ready) outside3d_prepare();
    car_park();
    dressing();
    visitors();

    // The floors below the one being looked at, closed up
    int closed = (viewFloor >= VIEW_OUTSIDE) ? FLOORS : viewFloor;
    if (closed >= 1) {
        ground_storey();
        overhang();
    }
    for (int f = 1; f < closed; f++) upper_storey(f);
    if (viewFloor >= VIEW_OUTSIDE) roof();
}

void outside3d_lights(int viewFloor)
{
    if (!view3d_lamps_on()) return;
    float k = view3d_darkness();
    unsigned char a = (unsigned char)(80 * k);
    for (int i = 0; i < 3; i++) glow(-11.0f + i * 7.0f, AISLE_Z, 3.0f, (Color){ 255, 210, 140, a });
    if (viewFloor >= 1) {
        for (int i = 0; i < 4; i++) glow(-5.0f + i * 3.6f, (AE_FRONT_Z + BUILDING_Z1) / 2, 1.6f, (Color){ 255, 225, 170, a });
    }
    glow(MAIN_DOOR_X, AE_FRONT_Z + 1.0f, 1.8f, (Color){ 255, 230, 180, (unsigned char)(a / 2) });
}

void outside3d_close(void)
{
    if (!ready) return;
    UnloadRenderTexture(nameSign);
    UnloadRenderTexture(mainSign);
    UnloadRenderTexture(parkSign);
    UnloadRenderTexture(busSign);
    ready = false;
}
