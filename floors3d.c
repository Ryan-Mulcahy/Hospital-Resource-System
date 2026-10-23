// floors3d.c - the first and second floors in 3D, and the lifts that join
// every floor.
//
// Both floors upstairs have the same corridors as the ground floor (so the
// building makes sense): a corridor beside where A&E's left wall is below,
// with the lifts at its far end, and a corridor along the back. The rooms fill
// the rest:
//
//   FIRST FLOOR: diagnostics and critical care
//   z -13 +--------+-----------+------------+-----------+
//         |  LAB   |    CT     | ULTRASOUND | CARDIOLOGY|
//         |        +-----------+------------+-----------+
//    -4.2 +--------+ |  back corridor                   |
//         |PHARMACY| +----------------------------------+
//     2.0 +--------+ |                                  |
//         | STAFF  | |      INTENSIVE CARE (ICU)        |
//         |  ROOM  |L|                                  |
//     7.6 +--------+-+----------------------------------+
//      x -16.5   -9.7 -8.1                             8.1
//                   (L: the lifts)
//
//   SECOND FLOOR: wards and therapy (with a waiting area where everyone
//   queuing for a room on this floor sits until they're called)
//         | WAITING| PHYSIOTHERAPY |      DIALYSIS          |
//         |  CAFE  |  MATERNITY    |   CHILDREN'S WARD      |
//
// Everything on a floor is drawn as if it were on the ground, then lifted up
// to the floor's height with rlTranslatef. The walls between rooms are half
// height, like the ground floor, so you can see into every room from above.
//
// The patients in CT, ultrasound, cardiology, ICU, the pharmacy, maternity,
// the children's ward, physio and dialysis are real: they come from the
// simulation's rooms. Everyone else is a dummy that makes the place feel
// lived in. Like the other 3D files, this only reads the simulation.

#include <math.h>
#include <stddef.h>
#include <string.h>
#include "raylib.h"
#include "rlgl.h"            // rlPushMatrix / rlTranslatef: lift a floor to its height
#include "draw.h"            // colours
#include "view3d_parts.h"    // boxes, walls, people, furniture
#include "floors3d.h"

// ------------------------------------------------------------------------
// Layout (1 unit is roughly 1 metre; heights are from the floor's own floor)
// ------------------------------------------------------------------------

#define WING_X     -9.7f    // the left wing's wall onto the side corridor
#define SIDE_X     -8.1f    // the side corridor's other wall (above A&E's left wall)
#define BACK_Z     -6.7f    // the front of the rooms behind the back corridor
#define CORR_Z     -5.2f    // the back corridor's front wall
#define OUTER_H     2.6f    // outside walls
#define PART_H      1.15f   // walls between rooms
#define DOOR_W      1.2f    // a doorway
#define WALK_X     -8.55f   // where people walk along the side corridor

// Which wall a room's door is in
typedef enum {
    DOOR_BACK,      // a room behind the back corridor: door in z = BACK_Z at x = door
    DOOR_FRONT,     // a room in front of the back corridor: door in z = CORR_Z at x = door
    DOOR_WING,      // a room in the left wing: door in x = WING_X at z = door
    DOOR_SIDE       // a room in front of the back corridor: door in x = SIDE_X at z = door
} DoorWall;

typedef struct {
    int floor;               // 1 or 2
    int simRoom;             // the simulation's RoomType, or -1 if nobody is sent here
    const char *name;        // on its sign
    float x0, z0, x1, z1;    // the floor
    Color colour;            // its sign, and a tint for its floor
    DoorWall wall;
    float door;              // the middle of the doorway
    float camDist;           // how far back the camera sits to look at it
} UpperRoom;

enum { CT, ULTRA, CATH, ICU, PHARM, LAB, STAFF,             // first floor
       PHYSIO, DIALYSIS, MATERN, KIDS, WAITING, CAFE,         // second floor
       UPPER_ROOMS };

static const UpperRoom ROOMS[UPPER_ROOMS] = {
    [CT]       = { 1, ROOM_CT,         "CT SCANNER",      WING_X, -13.0f, -3.3f, BACK_Z, {  95, 105, 190, 255 }, DOOR_BACK,  -6.0f,  13.0f },
    [ULTRA]    = { 1, ROOM_ULTRASOUND, "ULTRASOUND",       -3.3f, -13.0f,  2.6f, BACK_Z, {  60, 150, 190, 255 }, DOOR_BACK,  -2.3f,  13.0f },
    [CATH]     = { 1, ROOM_CARDIO,     "CARDIOLOGY",        2.6f, -13.0f,  8.1f, BACK_Z, { 200,  60,  85, 255 }, DOOR_BACK,   3.6f,  13.0f },
    [ICU]      = { 1, ROOM_ICU,        "INTENSIVE CARE",  SIDE_X, CORR_Z,  8.1f,   7.6f, { 215,  95,  55, 255 }, DOOR_SIDE,  -0.9f,  19.0f },
    [PHARM]    = { 1, ROOM_PHARMACY,   "PHARMACY",       -16.5f,  -4.2f, WING_X,   2.0f, {  50, 160, 100, 255 }, DOOR_WING,  -2.45f, 12.0f },
    [LAB]      = { 1, -1,              "LABORATORY",     -16.5f, -13.0f, WING_X,  -4.2f, { 110, 120, 205, 255 }, DOOR_WING,  -5.95f, 14.0f },
    [STAFF]    = { 1, -1,              "STAFF ROOM",     -16.5f,   2.0f, WING_X,   7.6f, { 150, 115,  85, 255 }, DOOR_WING,   5.1f,  12.0f },
    [PHYSIO]   = { 2, ROOM_PHYSIO,     "PHYSIOTHERAPY",   WING_X, -13.0f, -3.3f, BACK_Z, {  70, 165,  80, 255 }, DOOR_BACK,  -6.0f,  13.0f },
    [DIALYSIS] = { 2, ROOM_DIALYSIS,   "DIALYSIS",         -3.3f, -13.0f,  8.1f, BACK_Z, {  55, 120, 175, 255 }, DOOR_BACK,  -2.3f,  15.0f },
    [MATERN]   = { 2, ROOM_MATERNITY,  "MATERNITY",       SIDE_X, CORR_Z,  0.0f,   7.6f, { 205, 105, 170, 255 }, DOOR_SIDE,  -0.9f,  16.0f },
    [KIDS]     = { 2, ROOM_CHILDREN,   "CHILDREN'S WARD",   0.0f, CORR_Z,  8.1f,   7.6f, { 240, 150,  35, 255 }, DOOR_FRONT,  4.0f,  16.0f },
    [WAITING]  = { 2, -1,              "WAITING AREA",   -16.5f, -13.0f, WING_X,  -4.2f, {  40, 150, 160, 255 }, DOOR_WING,  -5.95f, 13.0f },
    [CAFE]     = { 2, -1,              "CAFE",           -16.5f,  -4.2f, WING_X,   7.6f, { 185, 110,  55, 255 }, DOOR_WING,  -2.45f, 15.0f },
};

// The short names for the number keys, e.g. "1 CT  2 Ultrasound"
static const char *KEY_NAME[UPPER_ROOMS] = {
    [CT] = "CT", [ULTRA] = "Ultrasound", [CATH] = "Cardiology", [ICU] = "ICU", [PHARM] = "Pharmacy",
    [LAB] = "Lab", [STAFF] = "Staff room", [PHYSIO] = "Physio", [DIALYSIS] = "Dialysis",
    [MATERN] = "Maternity", [KIDS] = "Children", [WAITING] = "Waiting area", [CAFE] = "Cafe",
};

// ---- Colours for this file ----
#define C_MACHINE   (Color){ 228, 231, 237, 255 }   // white hospital equipment
#define C_SCREEN    (Color){ 110, 170, 235, 255 }
#define C_WOOD      (Color){ 196, 160, 120, 255 }
#define C_DARKWOOD  (Color){ 120,  85,  60, 255 }
#define C_SCRUBS    (Color){  60, 150, 130, 255 }
#define C_RADIOG    (Color){  45,  75, 125, 255 }
#define C_LEAD      (Color){  70,  90, 110, 255 }   // lead apron
#define C_MIDWIFE   (Color){ 150, 110, 190, 255 }
#define C_PHYSIO    (Color){  40, 120,  70, 255 }   // physio's polo shirt
#define C_PHARMA    (Color){ 245, 245, 248, 255 }
#define C_SHIRT     (Color){ 236, 238, 242, 255 }
#define C_NAVY      (Color){  50,  60, 100, 255 }
#define C_GREY_HAIR (Color){ 175, 175, 180, 255 }
#define C_FAIR_HAIR (Color){ 205, 170, 105, 255 }
#define C_RED_HAIR  (Color){ 170,  80,  40, 255 }
#define C_PINK      (Color){ 245, 180, 200, 255 }
#define C_BLUE      (Color){ 160, 200, 240, 255 }
#define C_LEMON     (Color){ 250, 225, 140, 255 }
#define C_MINT      (Color){ 170, 225, 195, 255 }
#define C_RUG       (Color){ 120, 180, 220, 255 }
#define C_SOFA      (Color){ 110, 130, 160, 255 }

// ------------------------------------------------------------------------
// State
// ------------------------------------------------------------------------

static bool ready = false;
static RenderTexture2D roomSign[UPPER_ROOMS];
static float roomSignW[UPPER_ROOMS];
static RenderTexture2D floorSign[FLOORS];             // over the lifts: "FIRST FLOOR ..."
static RenderTexture2D ultrasoundScreen, menuBoard, radiationSign, callingScreen;

static const Sim *simNow;
static float now;
static int   curFloor;                                 // the floor being drawn
static bool  lowNorth, lowSouth, lowWest, lowEast;     // outside walls facing the camera

// ------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------

static Color gown_colour(const Patient *p)
{
    if (p->severity == 1) return C_EMERGENCY;
    if (p->severity == 2) return C_URGENT;
    return C_ROUTINE;
}

// The patient in place `slot` of a simulation room, or NULL if it's empty or
// they're still walking there (view3d.c draws them while they walk).
static const Patient *in_room(int r, int slot)
{
    if (simNow == NULL || r < 0 || slot >= simNow->rooms[r].capacity) return NULL;
    const Patient *p = simNow->rooms[r].in[slot];
    if (p == NULL || view3d_patient_walking(p->id)) return NULL;
    return p;
}

// How far through their time in the room a patient is: 0 just arrived, 1 done.
static float progress(const Patient *p, int r)
{
    float d = simNow->rooms[r].duration;
    if (d <= 0) return 1;
    float k = 1.0f - p->roomLeft / d;
    return k < 0 ? 0 : (k > 1 ? 1 : k);
}

static Outfit outfit(Color top, Color legs, bool cap)
{
    return (Outfit){ top, legs, false, false, cap, false };
}

static Outfit doctor(void)
{
    return (Outfit){ C_COAT, C_TROUSERS, true, true, false, false };
}

static Outfit nurse(void)
{
    return (Outfit){ C_BRAND, C_BRAND, false, false, true, true };
}

// A wall along x (at depth z) from x0 to x1, with doorways DOOR_W wide
// centred on the x values in gaps[].
static void wall_x(float z, float x0, float x1, float h, bool low, const float *gaps, int nGaps)
{
    float from = x0;
    for (int i = 0; i <= nGaps; i++) {
        float to = (i < nGaps) ? gaps[i] - DOOR_W / 2 : x1;
        if (to - from > 0.01f) draw_wall(v3((from + to) / 2, 0, z), to - from, 0.2f, h, low);
        if (i < nGaps) from = gaps[i] + DOOR_W / 2;
    }
}

// The same for a wall along z (at position x).
static void wall_z(float x, float z0, float z1, float h, bool low, const float *gaps, int nGaps)
{
    float from = z0;
    for (int i = 0; i <= nGaps; i++) {
        float to = (i < nGaps) ? gaps[i] - DOOR_W / 2 : z1;
        if (to - from > 0.01f) draw_wall(v3(x, 0, (from + to) / 2), 0.2f, to - from, h, low);
        if (i < nGaps) from = gaps[i] + DOOR_W / 2;
    }
}

// A floor colour: the room's colour, very pale.
static Color pale(Color c, float k)
{
    return (Color){ (unsigned char)(c.r + (255 - c.r) * k), (unsigned char)(c.g + (255 - c.g) * k),
                    (unsigned char)(c.b + (255 - c.b) * k), 255 };
}

// Windows in an outside wall, showing the sky: along x at depth z, or along z at x.
// `inward` is +1 or -1: which side of the wall is inside.
static void windows_x(float z, float x0, float x1, float inward)
{
    float dark = view3d_darkness();
    Color sky = (Color){ (unsigned char)(178 - 150 * dark), (unsigned char)(214 - 174 * dark), (unsigned char)(244 - 172 * dark), 255 };
    for (float x = x0 + 1.0f; x + 0.6f < x1; x += 2.2f) {
        draw_box(v3(x + 0.6f, 1.75f, z + inward * 0.11f), v3(1.32f, 1.02f, 0.04f), C_CARD);            // frame
        draw_lit_box(v3(x + 0.6f, 1.75f, z + inward * 0.135f), v3(1.2f, 0.90f, 0.01f), sky);
    }
}

static void windows_z(float x, float z0, float z1, float inward)
{
    float dark = view3d_darkness();
    Color sky = (Color){ (unsigned char)(178 - 150 * dark), (unsigned char)(214 - 174 * dark), (unsigned char)(244 - 172 * dark), 255 };
    for (float z = z0 + 1.0f; z + 0.6f < z1; z += 2.2f) {
        draw_box(v3(x + inward * 0.11f, 1.75f, z + 0.6f), v3(0.04f, 1.02f, 1.32f), C_CARD);
        draw_lit_box(v3(x + inward * 0.135f, 1.75f, z + 0.6f), v3(0.01f, 0.90f, 1.2f), sky);
    }
}

// A room's name on a sign hanging from the ceiling, facing the front.
static void hanging_sign(int room, float x, float z)
{
    float w = roomSignW[room], h = 0.46f, y = 2.32f;
    draw_box(v3(x - w / 2 + 0.15f, y + 0.40f, z), v3(0.03f, 0.56f, 0.03f), C_STEEL);   // wires
    draw_box(v3(x + w / 2 - 0.15f, y + 0.40f, z), v3(0.03f, 0.56f, 0.03f), C_STEEL);
    draw_box(v3(x, y, z), v3(w, h, 0.05f), ROOMS[room].colour);
    sign_facing_front(roomSign[room].texture, x, y, z + 0.03f, w - 0.06f, h - 0.06f);
}

// A sign hung near the room's door, over the room itself.
static void room_sign(int room)
{
    const UpperRoom *r = &ROOMS[room];
    float cx = (r->x0 + r->x1) / 2;
    switch (r->wall) {
        case DOOR_BACK:  hanging_sign(room, cx, r->z0 + 0.7f); break;
        case DOOR_FRONT: hanging_sign(room, cx, r->z1 - 0.8f); break;
        case DOOR_SIDE:  hanging_sign(room, cx, r->z1 - 0.8f); break;
        default:         hanging_sign(room, cx, r->z1 - 0.45f); break;
    }
}

// Someone small lying in bed (the children's ward), head to the back of the piece.
static void child_lying(Vector3 p, Facing f, float y, Color cover, Color hair)
{
    part_box(p, f, 0, y + 0.10f, 0.15f, 0.44f, 0.20f, 0.95f, cover);          // body, covered
    part_box(p, f, 0, y + 0.13f, -0.48f, 0.27f, 0.25f, 0.25f, C_SKIN);        // head
    part_box(p, f, 0, y + 0.14f, -0.61f, 0.29f, 0.27f, 0.05f, hair);
}

// A child standing (a small person).
static void child_standing(Vector3 p, Facing f, Color top, Color legs, Color hair)
{
    part_shadow(p, f, 0.4f, 0.4f);
    part_box(p, f, -0.07f, 0.17f, 0, 0.11f, 0.34f, 0.14f, legs);
    part_box(p, f,  0.07f, 0.17f, 0, 0.11f, 0.34f, 0.14f, legs);
    part_box(p, f, 0, 0.52f, 0, 0.32f, 0.38f, 0.20f, top);
    part_box(p, f, -0.21f, 0.52f, 0, 0.08f, 0.32f, 0.10f, top);
    part_box(p, f,  0.21f, 0.52f, 0, 0.08f, 0.32f, 0.10f, top);
    part_box(p, f, 0, 0.85f, 0, 0.26f, 0.26f, 0.26f, C_SKIN);
    part_box(p, f, -0.05f, 0.87f, 0.135f, 0.04f, 0.04f, 0.01f, C_DARK);
    part_box(p, f,  0.05f, 0.87f, 0.135f, 0.04f, 0.04f, 0.01f, C_DARK);
    part_box(p, f, 0, 0.99f, -0.02f, 0.28f, 0.06f, 0.28f, hair);
}

// A ball: a stack of boxes, wide in the middle.
static void ball(float x, float y, float z, float r, Color c)
{
    draw_box(v3(x, y, z), v3(r * 2.0f, r * 1.2f, r * 2.0f), c);
    draw_box(v3(x, y, z), v3(r * 1.2f, r * 2.0f, r * 1.2f), c);
    draw_box(v3(x, y, z), v3(r * 1.7f, r * 1.7f, r * 1.7f), c);
}

// A screen showing a heart trace, e.g. on a monitor bank.
static void ecg_screen(float x, float y, float z, float w, float h, float rate, Color line)
{
    draw_box(v3(x, y, z), v3(w + 0.06f, h + 0.06f, 0.06f), C_DARK);
    draw_lit_box(v3(x, y, z + 0.035f), v3(w, h, 0.01f), (Color){ 10, 22, 16, 255 });
    const int steps = 24;
    for (int i = 0; i < steps; i++) {
        float u = (float)i / steps;
        float b = fmodf(u * 2.0f + now * rate, 1.0f);
        float v = (b > 0.40f && b < 0.45f) ? 1.0f : (b > 0.45f && b < 0.5f) ? -0.4f : 0.0f;
        draw_lit_box(v3(x - w / 2 + 0.03f + u * (w - 0.06f), y + v * h * 0.3f, z + 0.045f), v3(w / steps + 0.005f, 0.015f, 0.005f), line);
    }
}

// ------------------------------------------------------------------------
// Where patients go, and the walk there
// ------------------------------------------------------------------------

// Which room in ROOMS[] a simulation room is (or -1).
static int upper_of(int simRoom)
{
    for (int i = 0; i < UPPER_ROOMS; i++) if (ROOMS[i].simRoom == simRoom) return i;
    return -1;
}

// The ICU and maternity beds, the children's beds and the dialysis chairs
static const float ICU_BED_X[4]    = { -5.6f, -2.0f, 1.6f, 5.2f };
static const float MATERN_BED_X[2] = { -6.2f, -2.6f };
static const float KIDS_BED_Z[3]   = { -2.6f, 0.3f, 3.2f };
static const float DIALYSIS_X[3]   = { -1.2f, 2.5f, 6.2f };
#define BED_ROW_Z  -3.4f     // the ICU and maternity beds, heads to the back corridor wall
#define KIDS_BED_X  7.0f     // the children's beds, heads to the outside wall

// Where a patient walks to for place `slot` in a room upstairs (they're then
// drawn lying on the table, in the bed, or in the chair).
static Vector3 slot_stand(int simRoom, int slot)
{
    switch (simRoom) {
        case ROOM_CT:         return v3(-5.4f, 0, -8.8f);
        case ROOM_ULTRASOUND: return v3( 0.0f, 0, -9.0f);
        case ROOM_CARDIO:     return v3( 5.0f, 0, -8.9f);
        case ROOM_ICU:        return v3(ICU_BED_X[slot] + 0.95f, 0, BED_ROW_Z + 1.0f);
        case ROOM_PHARMACY:   return v3(-13.3f, 0, slot == 0 ? -2.4f : -0.6f);
        case ROOM_MATERNITY:  return v3(MATERN_BED_X[slot] + 0.95f, 0, BED_ROW_Z + 1.0f);
        case ROOM_CHILDREN:   return v3(KIDS_BED_X - 1.2f, 0, KIDS_BED_Z[slot] + 0.6f);
        case ROOM_PHYSIO:     return slot == 0 ? v3(-7.9f, 0, -10.0f) : v3(-4.8f, 0, -8.4f);
        default:              return v3(DIALYSIS_X[slot], 0, -10.3f);   // dialysis
    }
}

// Where the k-th patient queuing for a room waits, which way they face, and
// whether they sit (the pharmacy's chairs).
// The second floor's waiting area: a row of five chairs for each room on the
// floor, facing a screen that calls people in. Row 0 is physio, then
// dialysis, maternity and the children's ward.
#define WAIT_ROWS      4
#define WAIT_SEATS     5
static const int WAIT_ROOM[WAIT_ROWS] = { ROOM_PHYSIO, ROOM_DIALYSIS, ROOM_MATERNITY, ROOM_CHILDREN };
static float wait_row_x(int row)  { return -11.6f - row * 1.25f; }
static float wait_seat_z(int k)   { return -11.9f + k * 0.75f; }
#define WAIT_AISLE_Z  -7.7f      // the walkway along the front of the rows

static int wait_row_of(int simRoom)
{
    for (int r = 0; r < WAIT_ROWS; r++) if (WAIT_ROOM[r] == simRoom) return r;
    return -1;
}

static Vector3 queue_spot(int simRoom, int k, Facing *f, bool *seated)
{
    const UpperRoom *r = &ROOMS[upper_of(simRoom)];
    *seated = false;
    int row = wait_row_of(simRoom);
    if (row >= 0) {                                   // second floor: a seat in the waiting area
        *f = FACE_WEST;
        *seated = true;
        return v3(wait_row_x(row), 0, wait_seat_z(k < WAIT_SEATS ? k : WAIT_SEATS - 1));
    }
    switch (r->wall) {
        case DOOR_BACK:  *f = FACE_NORTH; return v3(r->door + 0.95f + 0.65f * k, 0, -6.3f);   // in the back corridor
        case DOOR_FRONT: *f = FACE_SOUTH; return v3(r->door + 0.95f + 0.65f * k, 0, -5.6f);
        case DOOR_SIDE:  *f = FACE_EAST;  return v3(-9.4f, 0, r->door - 0.9f - 0.6f * k);    // along the side corridor
        default:                                                                          // the pharmacy's chairs
            *f = FACE_WEST;
            if (k < 3) { *seated = true; return v3(-10.4f, 0, 0.1f + 0.75f * k); }
            return v3(-11.3f, 0, 1.6f - 0.6f * (k - 3));
    }
}

int floors3d_path(int room, int place, Vector3 *pts, int max)
{
    int u = upper_of(room);
    if (u < 0) return 0;
    const UpperRoom *r = &ROOMS[u];
    float y = r->floor * FLOOR_H;
    int n = 0;
    #define ADD(p) do { if (n < max) pts[n++] = (p); } while (0)
    bool queue = place < 0;
    Facing f; bool seated;
    Vector3 end = queue ? queue_spot(room, -1 - place, &f, &seated) : slot_stand(room, place);
    end.y = y;

    // Down the side corridor to the lifts, in, up, and out
    ADD(v3(WALK_X, 0, 0.0f));
    ADD(v3(WALK_X, 0, 5.9f));
    ADD(v3(LIFT_X, 0, LIFT_Z));
    ADD(v3(LIFT_X, y, LIFT_Z));
    ADD(v3(WALK_X, y, 5.9f));
    ADD(v3(WALK_X, y, -0.9f));            // (on the way up the corridor: every route here shares it)
    int row = wait_row_of(room);
    if (queue && row >= 0) {              // to a seat in the second floor's waiting area
        ADD(v3(WALK_X, y, -5.95f));
        ADD(v3(-10.6f, y, -5.95f));
        ADD(v3(-10.6f, y, WAIT_AISLE_Z));
        ADD(v3(end.x + 0.65f, y, WAIT_AISLE_Z));
        ADD(v3(end.x + 0.65f, y, end.z));
        ADD(end);
        return n;
    }
    switch (r->wall) {
        case DOOR_BACK:
        case DOOR_FRONT:
            ADD(v3(WALK_X, y, -5.95f));                         // up the corridor, round the corner
            ADD(v3(r->door, y, -5.95f));                        // outside the door
            if (!queue) ADD(v3(r->door, y, r->wall == DOOR_BACK ? -7.4f : -4.5f));   // just inside
            break;
        case DOOR_SIDE:
            if (!queue) ADD(v3(-7.4f, y, r->door));   // (the door is at z -0.9, where they are now)
            break;
        default:                                                // the left wing
            ADD(v3(WALK_X, y, r->door));
            ADD(v3(-10.6f, y, r->door));
            break;
    }
    ADD(end);
    #undef ADD
    return n;
}

float floors3d_focus(int room, Vector3 *centre)
{
    int u = upper_of(room);
    if (u < 0) { *centre = v3(0, 0.4f, 0); return 20.0f; }
    const UpperRoom *r = &ROOMS[u];
    *centre = v3((r->x0 + r->x1) / 2, r->floor * FLOOR_H + 0.4f, (r->z0 + r->z1) / 2);
    return r->camDist;
}

const char *floors3d_name_at(int floor, float x, float z)
{
    for (int i = 0; i < UPPER_ROOMS; i++) {
        const UpperRoom *r = &ROOMS[i];
        if (r->floor == floor && x >= r->x0 && x <= r->x1 && z >= r->z0 && z <= r->z1) return r->name;
    }
    if (x > WING_X && x < SIDE_X && z > LIFT_DOOR_Z) return "LIFTS";
    if (x >= BUILDING_X0 && x <= BUILDING_X1 && z >= BUILDING_Z0 && z <= BUILDING_Z1) return "CORRIDOR";
    return "OUTSIDE";
}

// The k-th room on a floor (k from 1, in the order of ROOMS[]), or -1.
static int room_by_key(int floor, int k)
{
    for (int i = 0; i < UPPER_ROOMS; i++) {
        if (ROOMS[i].floor != floor) continue;
        if (--k == 0) return i;
    }
    return -1;
}

int floors3d_key_count(int floor)
{
    int n = 0;
    for (int i = 0; i < UPPER_ROOMS; i++) if (ROOMS[i].floor == floor) n++;
    return n;
}

const char *floors3d_key_name(int floor, int k)
{
    int i = room_by_key(floor, k);
    return i < 0 ? "" : KEY_NAME[i];
}

float floors3d_key_focus(int floor, int k, Vector3 *centre)
{
    int i = room_by_key(floor, k);
    if (i < 0) { *centre = v3(-1.0f, floor * FLOOR_H + 0.4f, -1.5f); return 30.0f; }
    const UpperRoom *r = &ROOMS[i];
    *centre = v3((r->x0 + r->x1) / 2, floor * FLOOR_H + 0.4f, (r->z0 + r->z1) / 2);
    return r->camDist;
}

const char *floors3d_floor_name(int floor)
{
    switch (floor) {
        case 0:  return "GROUND FLOOR";
        case 1:  return "FIRST FLOOR";
        case 2:  return "SECOND FLOOR";
        default: return "OUTSIDE";
    }
}

// The patients queuing for the rooms on this floor.
static void draw_queues(void)
{
    for (int r = 0; r < ROOM_TYPES; r++) {
        if (sim_room_floor(r) != curFloor) continue;
        int k = 0;
        for (const Patient *p = simNow->rooms[r].queue; p != NULL && k < 5; p = p->next, k++) {
            if (view3d_patient_walking(p->id)) continue;
            Facing f; bool seated;
            Vector3 s = queue_spot(r, k, &f, &seated);
            // Someone who can't walk far is given a chair while they wait
            if (!seated && (p->equipNeeds & EQUIP_BIT(EQUIP_WHEELCHAIR))) { furniture_chair(s, f, C_CHAIR); seated = true; }
            if (seated) person_seated(s, f, gown_colour(p), C_TROUSERS, C_HAIR, false);
            else draw_person(s, f, (Outfit){ gown_colour(p), C_TROUSERS, false, false, false, false }, false, 0);
        }
    }
}

// ------------------------------------------------------------------------
// The floor, the walls and the corridors (both floors are built the same way)
// ------------------------------------------------------------------------

static void draw_shell(void)
{
    // Floors: the corridors, then each room in a pale version of its colour
    Color corridorA = { 226, 222, 214, 255 }, corridorB = { 232, 228, 220, 255 };
    floor_tiles(WING_X, BACK_Z, BUILDING_X1, CORR_Z, corridorA, corridorB);       // back corridor
    floor_tiles(WING_X, CORR_Z, SIDE_X, LIFT_DOOR_Z, corridorA, corridorB);       // side corridor
    for (int i = 0; i < UPPER_ROOMS; i++) {
        const UpperRoom *r = &ROOMS[i];
        if (r->floor != curFloor) continue;
        floor_tiles(r->x0, r->z0, r->x1, r->z1, pale(r->colour, 0.82f), pale(r->colour, 0.87f));
    }

    // Outside walls, with windows; the ones facing the camera are cut down
    wall_x(BUILDING_Z0, BUILDING_X0 - 0.1f, BUILDING_X1 + 0.1f, OUTER_H, lowNorth, NULL, 0);
    wall_x(BUILDING_Z1, BUILDING_X0 - 0.1f, BUILDING_X1 + 0.1f, OUTER_H, lowSouth, NULL, 0);
    wall_z(BUILDING_X0, BUILDING_Z0, BUILDING_Z1, OUTER_H, lowWest, NULL, 0);
    wall_z(BUILDING_X1, BUILDING_Z0, BUILDING_Z1, OUTER_H, lowEast, NULL, 0);
    if (!lowNorth) windows_x(BUILDING_Z0, BUILDING_X0, BUILDING_X1, 1);
    if (!lowWest)  windows_z(BUILDING_X0, BUILDING_Z0, BUILDING_Z1, 1);
    if (!lowEast)  windows_z(BUILDING_X1, BUILDING_Z0, BUILDING_Z1, -1);

    // The corridor walls, with a doorway into each room
    float back[4], front[4], wing[4], side[4];
    int nb = 0, nf = 0, nw = 0, ns = 0;
    for (int i = 0; i < UPPER_ROOMS; i++) {
        const UpperRoom *r = &ROOMS[i];
        if (r->floor != curFloor) continue;
        switch (r->wall) {
            case DOOR_BACK:  back[nb++] = r->door; break;
            case DOOR_FRONT: front[nf++] = r->door; break;
            case DOOR_WING:  wing[nw++] = r->door; break;
            default:         side[ns++] = r->door; break;
        }
    }
    wall_x(BACK_Z, WING_X, BUILDING_X1, PART_H, false, back, nb);
    wall_x(CORR_Z, SIDE_X, BUILDING_X1, PART_H, false, front, nf);
    wall_z(WING_X, BUILDING_Z0, BUILDING_Z1, PART_H, false, wing, nw);
    wall_z(SIDE_X, CORR_Z, LIFT_DOOR_Z, PART_H, false, side, ns);

    // The walls between rooms
    for (int i = 0; i < UPPER_ROOMS; i++) {
        const UpperRoom *r = &ROOMS[i];
        if (r->floor != curFloor) continue;
        if (r->wall == DOOR_WING) {
            if (r->z1 < BUILDING_Z1 - 0.05f) wall_x(r->z1, BUILDING_X0, WING_X, PART_H, false, NULL, 0);
        } else if (r->x1 < BUILDING_X1 - 0.05f) {
            wall_z(r->x1, r->z0, r->z1, PART_H, false, NULL, 0);
        }
    }

    // A bench and a plant in the corridors
    draw_box(v3(-9.35f, 0.45f, 2.6f), v3(0.42f, 0.08f, 1.30f), C_CHAIR);
    draw_box(v3(-9.35f, 0.22f, 2.1f), v3(0.36f, 0.42f, 0.08f), C_STEEL);
    draw_box(v3(-9.35f, 0.22f, 3.1f), v3(0.36f, 0.42f, 0.08f), C_STEEL);
    draw_plant(7.5f, -6.0f);
    draw_plant(-9.35f, -4.6f);
}

// ------------------------------------------------------------------------
// First floor
// ------------------------------------------------------------------------

// CT: the scanner's ring, the table sliding through it, and the control desk
static void draw_ct(void)
{
    float gx = -7.4f, gz = -10.2f;                  // the gantry: a ring standing across the table
    shadow(gx, gz, 1.0f, 2.6f);
    draw_box(v3(gx, 0.30f, gz), v3(0.9f, 0.6f, 2.2f), C_MACHINE);               // its base
    const int n = 24;
    for (int i = 0; i < n; i++) {                                                // the ring
        float a = i * 2 * PI / n;
        draw_box(v3(gx, 1.15f + sinf(a) * 0.82f, gz + cosf(a) * 0.82f), v3(0.8f, 0.36f, 0.36f), C_MACHINE);
    }
    for (int i = 0; i < n; i++) {                                                // the coloured trim
        float a = i * 2 * PI / n;
        draw_box(v3(gx + 0.41f, 1.15f + sinf(a) * 0.62f, gz + cosf(a) * 0.62f), v3(0.02f, 0.12f, 0.12f), ROOMS[CT].colour);
    }
    // the spinning tube inside the ring, while someone is being scanned
    const Patient *p = in_room(ROOM_CT, 0);
    if (p) {
        float a = now * 6.0f;
        draw_lit_box(v3(gx, 1.15f + sinf(a) * 0.55f, gz + cosf(a) * 0.55f), v3(0.5f, 0.12f, 0.12f), (Color){ 255, 230, 140, 255 });
    }

    // The table, sliding the patient through the ring
    float slide = p ? 0.5f - 0.5f * cosf(now * 0.6f) : 0.0f;
    float tx = -5.4f - slide * 1.6f;
    draw_box(v3(-5.2f, 0.32f, gz), v3(1.6f, 0.64f, 0.6f), C_MACHINE);
    draw_box(v3(tx, 0.74f, gz), v3(2.3f, 0.10f, 0.62f), (Color){ 238, 232, 214, 255 });
    if (p) person_lying(v3(tx, 0, gz), FACE_EAST, 0.79f, 0.0f, gown_colour(p), C_HAIR);

    // The radiographer at the control desk, with a scan on the screen
    Vector3 d = v3(-4.6f, 0, -12.2f);
    draw_box(v3(d.x, 0.40f, d.z), v3(1.6f, 0.80f, 0.6f), C_DESK);
    furniture_computer(v3(d.x - 0.4f, 0, d.z - 0.1f), FACE_SOUTH, 0.80f);
    furniture_computer(v3(d.x + 0.4f, 0, d.z - 0.1f), FACE_SOUTH, 0.80f);
    draw_lit_box(v3(d.x + 0.4f, 1.18f, d.z - 0.06f), v3(0.18f, 0.18f, 0.01f), (Color){ 200, 200, 205, 255 });   // a slice
    draw_lit_box(v3(d.x + 0.4f, 1.18f, d.z - 0.05f), v3(0.10f, 0.12f, 0.01f), (Color){ 110, 110, 118, 255 });
    furniture_chair(v3(d.x, 0, d.z + 0.6f), FACE_NORTH, C_DARK);
    person_seated(v3(d.x, 0, d.z + 0.6f), FACE_NORTH, C_RADIOG, C_SHIRT, C_HAIR, true);
    // the lead-lined screen beside the desk
    draw_box(v3(d.x - 1.1f, 0.95f, d.z + 0.2f), v3(0.08f, 1.9f, 1.0f), C_LEAD);
    draw_lit_box(v3(d.x - 1.1f, 1.40f, d.z + 0.2f), v3(0.09f, 0.40f, 0.6f), (Color){ 180, 210, 230, 255 });

    // RADIATION sign on the back wall
    if (!lowNorth) {
        float wz = BUILDING_Z0 + 0.13f;
        draw_box(v3(-8.2f, 1.85f, wz), v3(1.10f, 0.40f, 0.03f), (Color){ 245, 200, 40, 255 });
        sign_facing_front(radiationSign.texture, -8.2f, 1.85f, wz + 0.02f, 1.04f, 0.34f);
    }
    room_sign(CT);
}

// Ultrasound: a couch, the scanner with its fan-shaped picture, the sonographer
static void draw_ultrasound(void)
{
    Vector3 c = v3(0.0f, 0, -10.6f);
    shadow(c.x, c.z, 2.2f, 0.9f);
    draw_box(v3(c.x, 0.30f, c.z), v3(1.8f, 0.60f, 0.7f), C_MACHINE);
    draw_box(v3(c.x, 0.65f, c.z), v3(2.0f, 0.12f, 0.75f), (Color){ 120, 160, 190, 255 });   // padded top
    draw_box(v3(c.x - 0.75f, 0.78f, c.z), v3(0.45f, 0.12f, 0.6f), C_CARD);                 // pillow, and paper roll
    const Patient *p = in_room(ROOM_ULTRASOUND, 0);
    if (p) person_lying(c, FACE_EAST, 0.71f, 0.0f, gown_colour(p), C_FAIR_HAIR);

    // The scanner on its trolley, its screen turned to the couch
    Vector3 m = v3(1.6f, 0, -11.4f);
    shadow(m.x, m.z, 0.8f, 0.7f);
    draw_box(v3(m.x, 0.45f, m.z), v3(0.6f, 0.9f, 0.55f), C_MACHINE);
    draw_box(v3(m.x, 0.93f, m.z + 0.05f), v3(0.62f, 0.05f, 0.5f), C_DARK);                // keyboard
    draw_box(v3(m.x, 1.40f, m.z - 0.15f), v3(0.62f, 0.48f, 0.06f), C_DARK);
    sign_quad(ultrasoundScreen.texture, v3(m.x - 0.28f, 1.62f, m.z - 0.115f), v3(m.x + 0.28f, 1.62f, m.z - 0.115f),
                                        v3(m.x + 0.28f, 1.18f, m.z - 0.115f), v3(m.x - 0.28f, 1.18f, m.z - 0.115f));
    // The sonographer, sitting at the couch with the probe
    Vector3 s = v3(1.1f, 0, -10.0f);
    furniture_chair(s, FACE_WEST, C_DARK);
    person_seated(s, FACE_WEST, C_RADIOG, C_NAVY, C_RED_HAIR, false);
    if (p) draw_box(v3(0.45f + 0.08f * sinf(now * 1.5f), 0.86f, c.z + 0.1f), v3(0.08f, 0.06f, 0.14f), C_DARK);   // the probe

    // The room is kept dim: a soft lamp, a curtain, and a box of gel and towels
    draw_box(v3(2.2f, 0.90f, -7.4f), v3(0.04f, 1.80f, 0.8f), C_CURTAIN);
    furniture_trolley(v3(-2.4f, 0, -11.9f), FACE_SOUTH, 0.6f, 0.45f);
    draw_box(v3(-2.4f, 0.98f, -11.9f), v3(0.14f, 0.12f, 0.14f), (Color){ 120, 200, 230, 255 });
    room_sign(ULTRA);
}

// Cardiology: the cath lab, with its C-shaped X-ray arm round the table,
// a bank of screens and the team in lead aprons
static void draw_cath_lab(void)
{
    float tx = 5.4f, tz = -10.3f;
    const Patient *p = in_room(ROOM_CARDIO, 0);
    shadow(tx, tz, 2.6f, 1.0f);
    draw_box(v3(tx + 0.6f, 0.36f, tz), v3(0.8f, 0.72f, 0.6f), C_MACHINE);                 // pedestal
    draw_box(v3(tx, 0.78f, tz), v3(2.4f, 0.10f, 0.62f), (Color){ 238, 232, 214, 255 });
    if (p) person_lying(v3(tx, 0, tz), FACE_EAST, 0.83f, 0.0f, gown_colour(p), C_GREY_HAIR);

    // The C-arm: a big arc over the patient's chest, swinging slowly
    float swing = p ? 0.35f * sinf(now * 0.4f) : 0.0f;
    float cx = tx - 0.2f;
    draw_box(v3(cx, 0.05f, tz - 1.15f), v3(0.6f, 0.1f, 0.6f), C_STEEL);
    draw_box(v3(cx, 0.55f, tz - 1.15f), v3(0.3f, 1.0f, 0.3f), C_MACHINE);
    // (a half circle across the table: the detector over the chest, round the
    // side, and the X-ray tube under the table)
    const int n = 14;
    for (int i = 0; i <= n; i++) {
        float a = -PI / 2 + PI * i / n + swing;
        draw_box(v3(cx, 1.05f + sinf(a) * 0.95f, tz - cosf(a) * 0.95f), v3(0.22f, 0.22f, 0.22f), shade(C_MACHINE, 0.95f));
    }
    draw_box(v3(cx, 1.05f + 0.95f * cosf(swing), tz - 0.95f * sinf(swing)), v3(0.5f, 0.12f, 0.5f), C_DARK);   // detector
    draw_box(v3(cx, 1.05f - 0.95f * cosf(swing), tz + 0.95f * sinf(swing)), v3(0.36f, 0.16f, 0.36f), C_STEEL); // tube

    // Screens hanging over the table: the heart's arteries and its trace
    for (int i = 0; i < 3; i++) {
        ecg_screen(3.6f + i * 0.85f, 2.05f, tz - 1.5f, 0.75f, 0.45f, 1.1f + i * 0.2f,
                   i == 1 ? (Color){ 230, 230, 230, 255 } : (Color){ 80, 255, 140, 255 });
    }
    draw_box(v3(4.45f, 2.45f, tz - 1.5f), v3(2.6f, 0.06f, 0.06f), C_STEEL);

    // The team, in lead aprons over their scrubs
    Outfit lead = { C_LEAD, C_SCRUBS, false, false, true, false };
    person_masked(v3(tx - 0.3f, 0, tz + 0.95f), FACE_NORTH, lead);
    person_masked(v3(tx + 0.9f, 0, tz + 0.9f), FACE_NORTH, lead);
    // Control room behind glass in the corner
    draw_box(v3(7.4f, 0.40f, -7.6f), v3(1.2f, 0.8f, 0.55f), C_DESK);
    furniture_computer(v3(7.4f, 0, -7.8f), FACE_SOUTH, 0.80f);
    furniture_trolley(v3(3.2f, 0, -12.3f), FACE_SOUTH, 0.8f, 0.5f);
    room_sign(CATH);
}

// One ICU bay: a bed with its ventilator, monitor and drip, curtains either side
static void icu_bay(int slot)
{
    float x = ICU_BED_X[slot];
    Vector3 b = v3(x, 0, BED_ROW_Z);
    furniture_bed(b, FACE_SOUTH);
    const Patient *p = in_room(ROOM_ICU, slot);
    if (p) {
        person_lying(b, FACE_SOUTH, 0.61f, 0.0f, gown_colour(p), C_HAIR);
        draw_box(v3(x, 0.84f, BED_ROW_Z - 0.62f), v3(0.14f, 0.06f, 0.12f), C_BLUE);   // breathing mask
    }
    // Every ICU patient has a ventilator from the hospital's equipment (it's
    // taken as they're given the bed, so it's there while they walk in)
    if (simNow->rooms[ROOM_ICU].in[slot] != NULL) {
        draw_equipment_item(EQUIP_VENTILATOR, v3(x - 0.95f, 0, BED_ROW_Z - 0.75f), FACE_SOUTH);
    }
    draw_monitor(x + 0.85f, BED_ROW_Z - 0.85f, p);
    furniture_drip(x + 0.75f, BED_ROW_Z + 0.2f);
    // curtain rails between the bays
    if (slot < 3) {
        float cx = x + 1.8f;
        draw_box(v3(cx, 2.15f, BED_ROW_Z), v3(0.04f, 0.04f, 2.6f), C_STEEL);
        for (int k = 0; k < 4; k++) {
            draw_box(v3(cx + ((k % 2) ? 0.03f : -0.03f), 1.22f, BED_ROW_Z - 1.1f + k * 0.1f), v3(0.03f, 1.80f, 0.10f),
                     shade(C_CURTAIN, (k % 2) ? 0.9f : 1.0f));
        }
    }
}

static void draw_icu(void)
{
    for (int i = 0; i < 4; i++) icu_bay(i);

    // The nurses' station in the middle, with a screen for every bed
    Vector3 st = v3(0.0f, 0, 1.3f);
    shadow(st.x, st.z, 4.4f, 1.6f);
    draw_box(v3(st.x, 0.45f, st.z), v3(4.0f, 0.9f, 1.0f), C_DESK);
    draw_box(v3(st.x, 0.93f, st.z), v3(4.2f, 0.06f, 1.2f), shade(C_DESK, 1.15f));
    for (int i = 0; i < 4; i++) {
        const Patient *p = in_room(ROOM_ICU, i);
        ecg_screen(-1.5f + i * 1.0f, 1.28f, st.z - 0.3f, 0.6f, 0.36f, p ? 1.2f : 0.0f,
                   p ? (Color){ 80, 255, 140, 255 } : (Color){ 60, 70, 80, 255 });
    }
    furniture_chair(v3(-0.8f, 0, st.z + 0.9f), FACE_NORTH, C_DARK);
    person_seated(v3(-0.8f, 0, st.z + 0.9f), FACE_NORTH, C_BRAND, C_BRAND, C_FAIR_HAIR, true);
    // A nurse going from bay to bay, and a consultant on the ward round
    float t = fmodf(now * 0.12f, 1.0f);
    float nx = -5.0f + 10.0f * (t < 0.5f ? t * 2 : 2 - t * 2);
    draw_person(v3(nx, 0, -1.4f), t < 0.5f ? FACE_EAST : FACE_WEST, nurse(), true, now * 10.0f);
    draw_person(v3(3.3f, 0, -1.9f), FACE_NORTH, doctor(), false, 0);

    // The relatives' corner: a sofa, a plant and a box of tissues
    Vector3 so = v3(5.6f, 0, 6.6f);
    draw_box(v3(so.x, 0.25f, so.z), v3(2.0f, 0.5f, 0.8f), C_SOFA);
    draw_box(v3(so.x, 0.65f, so.z + 0.32f), v3(2.0f, 0.6f, 0.18f), C_SOFA);
    person_seated(v3(so.x - 0.5f, 0, so.z - 0.1f), FACE_NORTH, (Color){ 160, 80, 90, 255 }, C_NAVY, C_GREY_HAIR, false);
    draw_box(v3(3.9f, 0.25f, 6.6f), v3(0.6f, 0.5f, 0.6f), C_WOOD);
    draw_box(v3(3.9f, 0.56f, 6.6f), v3(0.18f, 0.12f, 0.12f), C_CARD);
    draw_plant(7.5f, 5.0f);
    furniture_bin(-7.5f, 6.8f, 0.5f);
    room_sign(ICU);
}

// Pharmacy: the counter with its glass screen, shelves of medicine behind,
// the pharmacist, and chairs for people waiting
static void draw_pharmacy(void)
{
    // Shelves along the outside wall, full of colourful boxes
    Color boxes[5] = { (Color){ 230, 90, 90, 255 }, (Color){ 90, 160, 230, 255 }, (Color){ 250, 210, 80, 255 },
                       (Color){ 110, 200, 140, 255 }, (Color){ 240, 240, 245, 255 } };
    for (int s = 0; s < 3; s++) {
        float z = -3.6f + s * 1.6f;
        draw_box(v3(-16.2f, 1.0f, z + 0.65f), v3(0.4f, 2.0f, 1.4f), C_WOOD);
        for (int shelf = 0; shelf < 4; shelf++) {
            for (int k = 0; k < 5; k++) {
                draw_box(v3(-16.05f, 0.35f + shelf * 0.45f, z + 0.12f + k * 0.26f), v3(0.18f, 0.22f, 0.2f),
                         boxes[(s + shelf + k) % 5]);
            }
        }
    }
    // The counter, with a glass screen and a till
    draw_box(v3(-14.2f, 0.50f, -1.5f), v3(0.7f, 1.0f, 4.6f), C_WOOD);
    draw_box(v3(-14.2f, 1.02f, -1.5f), v3(0.8f, 0.05f, 4.7f), C_DARKWOOD);
    draw_box(v3(-14.2f, 1.08f, -0.2f), v3(0.4f, 0.10f, 0.4f), C_DARK);
    furniture_computer(v3(-14.3f, 0, -2.4f), FACE_EAST, 1.05f);
    // The pharmacist, and an assistant counting tablets
    draw_person(v3(-15.0f, 0, -2.4f), FACE_EAST, outfit(C_PHARMA, C_NAVY, false), false, 0);
    draw_person(v3(-15.2f, 0, 0.6f), FACE_WEST, outfit((Color){ 90, 170, 130, 255 }, C_NAVY, false), false, 0);
    // The patients collecting their medicine, and a paper bag for each
    for (int s = 0; s < 2; s++) {
        const Patient *p = in_room(ROOM_PHARMACY, s);
        if (p == NULL) continue;
        Vector3 at = slot_stand(ROOM_PHARMACY, s);
        draw_person(at, FACE_WEST, (Outfit){ gown_colour(p), C_TROUSERS, false, false, false, false }, false, 0);
        draw_box(v3(-14.0f, 1.14f, at.z), v3(0.2f, 0.18f, 0.12f), (Color){ 220, 200, 160, 255 });
    }
    // Chairs for waiting
    for (int k = 0; k < 3; k++) furniture_chair(v3(-10.4f, 0, 0.1f + 0.75f * k), FACE_WEST, C_CHAIR);
    // A green cross on the outside wall: the sign of a pharmacy
    if (!lowWest) {
        float x = BUILDING_X0 + 0.13f;
        draw_lit_box(v3(x, 2.25f, -1.0f), v3(0.04f, 0.18f, 0.55f), (Color){ 40, 200, 90, 255 });
        draw_lit_box(v3(x, 2.25f, -1.0f), v3(0.04f, 0.55f, 0.18f), (Color){ 40, 200, 90, 255 });
    }
    room_sign(PHARM);
}

// Laboratory: benches with microscopes, racks of samples, a centrifuge and a fridge
static void draw_lab(void)
{
    for (int b = 0; b < 2; b++) {
        float z = -11.6f + b * 3.6f;
        shadow(-13.0f, z, 5.2f, 1.0f);
        draw_box(v3(-13.0f, 0.45f, z), v3(5.0f, 0.90f, 0.8f), C_MACHINE);
        draw_box(v3(-13.0f, 0.92f, z), v3(5.1f, 0.05f, 0.85f), (Color){ 60, 70, 80, 255 });
        for (int k = 0; k < 3; k++) {
            float x = -14.8f + k * 1.8f;
            // a microscope
            draw_box(v3(x, 0.98f, z), v3(0.22f, 0.06f, 0.28f), C_DARK);
            draw_box(v3(x, 1.15f, z - 0.08f), v3(0.06f, 0.30f, 0.06f), C_DARK);
            draw_box(v3(x, 1.30f, z + 0.02f), v3(0.07f, 0.07f, 0.20f), C_DARK);
            // and a rack of sample tubes with coloured tops
            for (int t = 0; t < 5; t++) {
                Color top = (t % 3 == 0) ? (Color){ 200, 50, 60, 255 } : (t % 3 == 1) ? (Color){ 150, 80, 200, 255 } : (Color){ 250, 200, 50, 255 };
                draw_box(v3(x + 0.55f + t * 0.06f, 1.02f, z + 0.15f), v3(0.04f, 0.14f, 0.04f), (Color){ 230, 235, 240, 255 });
                draw_box(v3(x + 0.55f + t * 0.06f, 1.10f, z + 0.15f), v3(0.045f, 0.03f, 0.045f), top);
            }
        }
    }
    // The scientists: one at a microscope, one with a clipboard
    furniture_chair(v3(-13.0f, 0, -10.8f), FACE_NORTH, C_DARK);
    person_seated(v3(-13.0f, 0, -10.8f), FACE_NORTH, C_COAT, C_NAVY, C_HAIR, false);
    draw_person(v3(-11.5f, 0, -7.2f), FACE_NORTH, (Outfit){ C_COAT, C_NAVY, true, false, false, true }, false, 0);
    // A centrifuge spinning, and the sample fridge
    draw_box(v3(-10.6f, 0.45f, -12.3f), v3(0.6f, 0.9f, 0.6f), C_MACHINE);
    float a = now * 20.0f;
    draw_box(v3(-10.6f + cosf(a) * 0.12f, 0.93f, -12.3f + sinf(a) * 0.12f), v3(0.08f, 0.04f, 0.08f), C_DARK);
    draw_box(v3(-16.05f, 0.95f, -6.4f), v3(0.6f, 1.9f, 0.8f), C_MACHINE);
    draw_lit_box(v3(-15.74f, 1.5f, -6.4f), v3(0.01f, 0.6f, 0.5f), (Color){ 170, 220, 255, 255 });
    room_sign(LAB);
}

// Staff room: a sofa with a doctor napping, a kitchen, and a table
static void draw_staff_room(void)
{
    // The sofa, and a doctor fast asleep on it
    draw_box(v3(-16.0f, 0.25f, 5.0f), v3(0.8f, 0.5f, 2.4f), C_SOFA);
    draw_box(v3(-16.3f, 0.65f, 5.0f), v3(0.18f, 0.6f, 2.4f), C_SOFA);
    person_lying(v3(-15.95f, 0, 5.0f), FACE_SOUTH, 0.45f, 0.0f, C_COAT, C_HAIR);
    float zz = fmodf(now * 0.5f, 1.0f);                                    // Z z z
    draw_box(v3(-15.9f, 1.2f + zz * 0.5f, 3.9f), v3(0.08f, 0.08f, 0.02f), (Color){ 255, 255, 255, (unsigned char)(255 * (1 - zz)) });
    // The kitchen along the outside wall
    draw_box(v3(-13.0f, 0.45f, 7.15f), v3(4.6f, 0.9f, 0.6f), C_WOOD);
    draw_box(v3(-13.0f, 0.93f, 7.15f), v3(4.7f, 0.06f, 0.65f), (Color){ 200, 200, 205, 255 });
    draw_box(v3(-14.6f, 1.06f, 7.1f), v3(0.2f, 0.22f, 0.2f), C_DARK);       // kettle
    draw_box(v3(-12.6f, 1.16f, 7.15f), v3(0.40f, 0.42f, 0.35f), C_DARK);    // coffee machine
    draw_lit_box(v3(-12.6f, 1.25f, 6.97f), v3(0.12f, 0.05f, 0.01f), (Color){ 90, 220, 140, 255 });
    draw_box(v3(-10.7f, 0.95f, 7.15f), v3(0.6f, 1.9f, 0.6f), C_MACHINE);    // fridge
    // A round table, a nurse on her break with a mug, and a doctor reading
    draw_box(v3(-12.6f, 0.72f, 4.4f), v3(1.1f, 0.05f, 1.1f), C_WOOD);
    draw_box(v3(-12.6f, 0.36f, 4.4f), v3(0.1f, 0.72f, 0.1f), C_STEEL);
    furniture_chair(v3(-12.6f, 0, 3.6f), FACE_SOUTH, C_CHAIR);
    person_seated(v3(-12.6f, 0, 3.6f), FACE_SOUTH, C_BRAND, C_BRAND, C_RED_HAIR, false);
    draw_box(v3(-12.5f, 0.80f, 4.1f), v3(0.08f, 0.1f, 0.08f), C_EMERGENCY);
    furniture_chair(v3(-11.8f, 0, 4.4f), FACE_WEST, C_CHAIR);
    person_seated(v3(-11.8f, 0, 4.4f), FACE_WEST, C_COAT, C_TROUSERS, C_GREY_HAIR, false);
    draw_box(v3(-12.15f, 0.82f, 4.4f), v3(0.24f, 0.03f, 0.32f), C_CARD);    // a newspaper
    draw_plant(-10.3f, 2.5f);
    room_sign(STAFF);
}

// ------------------------------------------------------------------------
// Second floor
// ------------------------------------------------------------------------

// Physiotherapy: parallel bars, an exercise mat, balls and a treadmill
static void draw_physio(void)
{
    // Parallel bars: the patient practises walking between them
    float bz = -10.0f;
    for (int s = -1; s <= 1; s += 2) {
        draw_box(v3(-6.6f, 0.95f, bz + s * 0.4f), v3(3.0f, 0.05f, 0.05f), C_STEEL);
        for (int k = 0; k < 3; k++) draw_box(v3(-7.9f + k * 1.3f, 0.48f, bz + s * 0.4f), v3(0.05f, 0.95f, 0.05f), C_STEEL);
    }
    draw_box(v3(-6.6f, 0.01f, bz), v3(3.2f, 0.02f, 1.0f), (Color){ 90, 140, 200, 255 });
    const Patient *p = in_room(ROOM_PHYSIO, 0);
    if (p) {
        float t = 0.5f - 0.5f * cosf(now * 0.5f);
        float x = -7.6f + t * 2.0f;
        bool forward = sinf(now * 0.5f) > 0;
        draw_person(v3(x, 0, bz), forward ? FACE_EAST : FACE_WEST,
                    (Outfit){ gown_colour(p), C_TROUSERS, false, false, false, false }, true, now * 4.0f);
        draw_person(v3(x + 0.3f, 0, bz + 0.85f), FACE_NORTH, outfit(C_PHYSIO, C_NAVY, false), false, 0);
    }
    // The exercise mat: a patient doing leg raises
    draw_box(v3(-4.6f, 0.02f, -8.6f), v3(2.0f, 0.04f, 0.9f), (Color){ 70, 160, 120, 255 });
    const Patient *q = in_room(ROOM_PHYSIO, 1);
    if (q) {
        Vector3 m = v3(-4.6f, 0, -8.6f);
        part_box(m, FACE_EAST, 0, 0.16f, -0.3f, 0.5f, 0.24f, 0.7f, gown_colour(q));          // body
        part_box(m, FACE_EAST, 0, 0.18f, -0.8f, 0.3f, 0.3f, 0.3f, C_SKIN);                    // head
        float lift = 0.25f + 0.25f * sinf(now * 2.0f);
        part_box(m, FACE_EAST, 0, 0.12f + lift * 0.5f, 0.45f, 0.4f, 0.16f + lift, 0.6f, C_TROUSERS);   // legs, rising
    }
    // Exercise balls, a treadmill, and bars on the wall
    ball(-3.9f, 0.32f, -12.2f, 0.32f, (Color){ 230, 90, 90, 255 });
    ball(-4.7f, 0.28f, -12.3f, 0.28f, (Color){ 90, 160, 230, 255 });
    draw_box(v3(-8.6f, 0.15f, -12.2f), v3(1.6f, 0.3f, 0.7f), C_DARK);       // treadmill
    draw_box(v3(-8.0f, 0.75f, -12.2f), v3(0.08f, 1.2f, 0.6f), C_DARK);
    draw_lit_box(v3(-7.95f, 1.35f, -12.2f), v3(0.04f, 0.18f, 0.3f), (Color){ 90, 220, 140, 255 });
    if (!lowNorth) {
        for (int k = 0; k < 6; k++) draw_box(v3(-6.0f, 0.3f + k * 0.35f, BUILDING_Z0 + 0.2f), v3(1.6f, 0.04f, 0.04f), C_WOOD);
    }
    room_sign(PHYSIO);
}

// Dialysis: reclining chairs, each with its machine cleaning the blood
static void draw_dialysis(void)
{
    for (int s = 0; s < 3; s++) {
        float x = DIALYSIS_X[s], z = -11.3f;
        // the recliner
        shadow(x, z, 1.0f, 1.4f);
        draw_box(v3(x, 0.3f, z), v3(0.8f, 0.3f, 0.9f), (Color){ 90, 130, 170, 255 });
        draw_box(v3(x, 0.75f, z - 0.45f), v3(0.8f, 0.9f, 0.2f), (Color){ 90, 130, 170, 255 });
        draw_box(v3(x - 0.45f, 0.5f, z), v3(0.12f, 0.2f, 0.8f), (Color){ 80, 115, 150, 255 });
        draw_box(v3(x + 0.45f, 0.5f, z), v3(0.12f, 0.2f, 0.8f), (Color){ 80, 115, 150, 255 });
        const Patient *p = in_room(ROOM_DIALYSIS, s);
        if (p) person_seated(v3(x, 0, z + 0.1f), FACE_SOUTH, gown_colour(p), C_TROUSERS, s == 1 ? C_GREY_HAIR : C_HAIR, false);
        // the machine, with its red and blue lines going to the patient's arm
        float mx = x + 0.95f;
        draw_box(v3(mx, 0.70f, z - 0.2f), v3(0.5f, 1.4f, 0.5f), C_MACHINE);
        draw_lit_box(v3(mx, 1.20f, z + 0.06f), v3(0.36f, 0.26f, 0.01f), p ? C_SCREEN : (Color){ 40, 50, 60, 255 });
        if (p) {
            float pump = 0.5f + 0.5f * sinf(now * 4.0f + s);
            draw_box(v3(mx - 0.1f, 0.85f, z + 0.07f), v3(0.08f, 0.08f, 0.02f), (Color){ (unsigned char)(150 + 100 * pump), 30, 40, 255 });
            DrawLine3D(v3(mx - 0.2f, 0.8f, z + 0.05f), v3(x + 0.35f, 0.62f, z + 0.25f), shade((Color){ 200, 40, 50, 255 }, 1.0f));
            DrawLine3D(v3(mx - 0.2f, 0.7f, z + 0.05f), v3(x + 0.35f, 0.60f, z + 0.30f), shade((Color){ 60, 90, 220, 255 }, 1.0f));
        }
        draw_box(v3(x - 0.75f, 0.35f, z + 0.4f), v3(0.4f, 0.7f, 0.4f), C_WOOD);    // side table
    }
    // The dialysis nurse, checking the machines in turn
    float t = fmodf(now * 0.1f, 1.0f);
    float nx = DIALYSIS_X[0] + 0.9f + (DIALYSIS_X[2] - DIALYSIS_X[0]) * (t < 0.5f ? t * 2 : 2 - t * 2);
    draw_person(v3(nx, 0, -9.9f), t < 0.5f ? FACE_EAST : FACE_WEST, nurse(), true, now * 10.0f);
    // A water treatment unit along the wall, and a TV for the long sessions
    draw_box(v3(-2.8f, 0.8f, -9.0f), v3(0.6f, 1.6f, 1.6f), C_MACHINE);
    if (!lowNorth) {
        draw_box(v3(2.5f, 2.0f, BUILDING_Z0 + 0.14f), v3(1.2f, 0.7f, 0.06f), C_DARK);
        draw_lit_box(v3(2.5f, 2.0f, BUILDING_Z0 + 0.18f), v3(1.1f, 0.6f, 0.01f), (Color){ 90, 150, 110, 255 });
    }
    room_sign(DIALYSIS);
}

// A baby cot on a trolley, with the baby once they're born
static void cot(float x, float z, bool baby, Color blanket)
{
    shadow(x, z, 0.6f, 0.8f);
    draw_box(v3(x, 0.35f, z), v3(0.08f, 0.7f, 0.08f), C_STEEL);
    draw_box(v3(x, 0.78f, z), v3(0.5f, 0.3f, 0.75f), (Color){ 214, 232, 244, 255 });
    draw_box(v3(x, 0.84f, z), v3(0.42f, 0.04f, 0.66f), C_MATTRESS);
    if (baby) {
        draw_box(v3(x, 0.90f, z + 0.05f), v3(0.26f, 0.09f, 0.34f), blanket);
        draw_box(v3(x, 0.92f, z - 0.17f), v3(0.14f, 0.12f, 0.12f), C_SKIN);
    }
}

// Maternity: two delivery beds, each with a cot; once a patient is halfway
// through their stay, the baby has arrived (and there's a balloon)
static void draw_maternity(void)
{
    Color blankets[2] = { C_PINK, C_BLUE };
    for (int s = 0; s < 2; s++) {
        float x = MATERN_BED_X[s];
        Vector3 b = v3(x, 0, BED_ROW_Z);
        furniture_bed(b, FACE_SOUTH);
        const Patient *p = in_room(ROOM_MATERNITY, s);
        bool born = p && progress(p, ROOM_MATERNITY) > 0.5f;
        if (p) person_lying(b, FACE_SOUTH, 0.61f, 0.0f, gown_colour(p), s ? C_RED_HAIR : C_HAIR);
        cot(x + 1.0f, BED_ROW_Z + 0.3f, born, blankets[s]);
        draw_monitor(x - 0.85f, BED_ROW_Z - 0.85f, p);
        if (born) {                                               // a balloon for the new arrival
            float bob = 0.05f * sinf(now * 1.5f + s);
            draw_box(v3(x + 1.0f, 2.2f + bob, BED_ROW_Z + 0.3f), v3(0.34f, 0.42f, 0.34f), blankets[s]);
            DrawLine3D(v3(x + 1.0f, 0.95f, BED_ROW_Z + 0.3f), v3(x + 1.0f, 2.0f + bob, BED_ROW_Z + 0.3f), shade(C_DARK, 1.0f));
        }
        // the partner, sitting with them
        if (p) {
            furniture_chair(v3(x - 0.9f, 0, BED_ROW_Z + 0.6f), FACE_EAST, C_CHAIR);
            person_seated(v3(x - 0.9f, 0, BED_ROW_Z + 0.6f), FACE_EAST, (Color){ 90, 120, 90, 255 }, C_TROUSERS, C_HAIR, false);
        }
    }
    // The midwife, a birthing ball, a rocking chair and bunting
    draw_person(v3(-4.4f, 0, -1.2f), FACE_NORTH, outfit(C_MIDWIFE, C_MIDWIFE, true), false, 0);
    ball(-6.6f, 0.36f, 2.2f, 0.36f, (Color){ 150, 110, 190, 255 });
    draw_box(v3(-2.0f, 0.25f, 4.5f), v3(0.8f, 0.5f, 0.8f), C_PINK);              // armchair for feeding
    draw_box(v3(-2.0f, 0.75f, 4.85f), v3(0.8f, 0.6f, 0.15f), C_PINK);
    person_seated(v3(-2.0f, 0, 4.4f), FACE_NORTH, C_MIDWIFE, C_MIDWIFE, C_FAIR_HAIR, false);
    draw_box(v3(-2.0f, 0.9f, 4.25f), v3(0.3f, 0.14f, 0.18f), C_LEMON);           // a baby in her arms
    cot(-5.5f, 4.8f, true, C_MINT);
    cot(-4.6f, 4.8f, true, C_LEMON);
    // Bunting along the outside wall
    if (!lowSouth) {
        Color flags[4] = { C_PINK, C_BLUE, C_LEMON, C_MINT };
        for (int i = 0; i < 18; i++) {
            draw_box(v3(-7.7f + i * 0.42f, 2.22f, BUILDING_Z1 - 0.13f), v3(0.18f, 0.2f, 0.01f), flags[i % 4]);
        }
    }
    room_sign(MATERN);
}

// The children's ward: small beds with bright covers, a play corner with
// a rug, blocks, a toy box and a rocking horse, and a rainbow on the wall
static void draw_childrens_ward(void)
{
    Color pillows[3] = { C_LEMON, C_MINT, C_PINK };
    for (int s = 0; s < 3; s++) {
        float z = KIDS_BED_Z[s];
        Vector3 b = v3(KIDS_BED_X, 0, z);
        part_shadow(b, FACE_WEST, 1.1f, 1.8f);
        part_box(b, FACE_WEST, 0, 0.22f, 0, 0.9f, 0.34f, 1.6f, C_BEDFRAME);
        part_box(b, FACE_WEST, 0, 0.44f, 0.05f, 0.82f, 0.12f, 1.45f, C_MATTRESS);
        part_box(b, FACE_WEST, 0, 0.54f, -0.55f, 0.6f, 0.1f, 0.3f, pillows[s]);
        part_box(b, FACE_WEST, 0, 0.55f, -0.82f, 0.9f, 0.8f, 0.08f, (Color){ 120, 190, 230, 255 });   // headboard
        part_box(b, FACE_WEST, -0.48f, 0.55f, 0.1f, 0.05f, 0.25f, 1.2f, C_STEEL);                        // cot sides
        part_box(b, FACE_WEST,  0.48f, 0.55f, 0.1f, 0.05f, 0.25f, 1.2f, C_STEEL);
        const Patient *p = in_room(ROOM_CHILDREN, s);
        if (p) {
            child_lying(b, FACE_WEST, 0.50f, gown_colour(p), s == 1 ? C_FAIR_HAIR : C_HAIR);
            // a teddy bear to keep them company
            draw_box(v3(KIDS_BED_X - 0.25f, 0.62f, z + 0.35f), v3(0.14f, 0.18f, 0.12f), (Color){ 170, 120, 80, 255 });
            draw_box(v3(KIDS_BED_X - 0.25f, 0.76f, z + 0.35f), v3(0.11f, 0.11f, 0.11f), (Color){ 170, 120, 80, 255 });
            // a parent beside the bed
            furniture_chair(v3(KIDS_BED_X - 1.2f, 0, z - 0.5f), FACE_EAST, C_CHAIR);
            person_seated(v3(KIDS_BED_X - 1.2f, 0, z - 0.5f), FACE_EAST, (Color){ 170, 90, 120, 255 }, C_NAVY, C_RED_HAIR, false);
        }
    }
    // The play corner
    draw_box(v3(2.6f, 0.01f, 4.6f), v3(3.6f, 0.02f, 3.0f), C_RUG);
    Color toys[4] = { (Color){ 230, 80, 80, 255 }, (Color){ 250, 210, 60, 255 }, (Color){ 80, 170, 230, 255 }, (Color){ 100, 200, 110, 255 } };
    for (int i = 0; i < 6; i++) {
        draw_box(v3(1.6f + (i % 3) * 0.35f, 0.08f + (i / 3) * 0.15f, 3.9f + (i % 2) * 0.2f), v3(0.15f, 0.15f, 0.15f), toys[i % 4]);
    }
    draw_box(v3(4.0f, 0.3f, 5.8f), v3(1.0f, 0.6f, 0.6f), toys[2]);                // toy box
    draw_box(v3(4.0f, 0.62f, 5.8f), v3(1.05f, 0.06f, 0.65f), toys[1]);
    // a rocking horse, rocking
    float rock = 0.08f * sinf(now * 2.0f);
    draw_box(v3(1.5f, 0.08f, 5.6f), v3(1.1f, 0.06f, 0.08f), C_DARKWOOD);
    draw_box(v3(1.5f, 0.45f + rock, 5.6f), v3(0.8f, 0.3f, 0.25f), (Color){ 240, 230, 210, 255 });
    draw_box(v3(1.95f, 0.75f + rock, 5.6f), v3(0.22f, 0.4f, 0.2f), (Color){ 240, 230, 210, 255 });
    draw_box(v3(2.1f, 0.85f + rock, 5.6f), v3(0.2f, 0.16f, 0.16f), (Color){ 240, 230, 210, 255 });
    // a child playing with the blocks, and the play specialist
    child_standing(v3(2.6f, 0, 4.3f), FACE_WEST, toys[3], C_NAVY, C_FAIR_HAIR);
    person_seated(v3(3.3f, 0, 3.8f), FACE_WEST, (Color){ 250, 160, 60, 255 }, C_NAVY, C_HAIR, false);
    // A rainbow painted on the outside wall
    if (!lowSouth) {
        Color bands[6] = { (Color){ 230, 70, 70, 255 }, (Color){ 245, 150, 50, 255 }, (Color){ 250, 220, 70, 255 },
                           (Color){ 90, 190, 100, 255 }, (Color){ 70, 140, 230, 255 }, (Color){ 140, 90, 200, 255 } };
        for (int b = 0; b < 6; b++) {
            float r = 1.6f - b * 0.15f;
            for (int i = 0; i <= 16; i++) {
                float a = PI * i / 16.0f;
                draw_box(v3(3.5f + cosf(a) * r, 0.6f + sinf(a) * r, BUILDING_Z1 - 0.12f), v3(0.16f, 0.16f, 0.01f), bands[b]);
            }
        }
    }
    draw_person(v3(5.4f, 0, -1.2f), FACE_EAST, nurse(), false, 0);
    room_sign(KIDS);
}

// The waiting area: everyone queuing for physio, dialysis, maternity or the
// children's ward sits here, in their room's row, until a screen calls them.
// There's a water cooler, a magazine table and toys.
static void draw_waiting_area(void)
{
    for (int row = 0; row < WAIT_ROWS; row++) {
        const UpperRoom *r = &ROOMS[upper_of(WAIT_ROOM[row])];
        float x = wait_row_x(row);
        // a strip of the room's colour on the floor, and its name on a little sign at the end of the row
        draw_box(v3(x + 0.05f, 0.012f, (wait_seat_z(0) + wait_seat_z(WAIT_SEATS - 1)) / 2), v3(0.9f, 0.02f, WAIT_SEATS * 0.75f + 0.2f), pale(r->colour, 0.5f));
        draw_box(v3(x, 0.5f, wait_seat_z(0) - 0.55f), v3(0.06f, 1.0f, 0.06f), C_STEEL);
        draw_box(v3(x, 1.05f, wait_seat_z(0) - 0.55f), v3(0.5f, 0.24f, 0.05f), r->colour);
        for (int k = 0; k < WAIT_SEATS; k++) furniture_chair(v3(x, 0, wait_seat_z(k)), FACE_WEST, C_CHAIR);
    }
    // The screen calling people in, on the outside wall
    if (!lowWest) {
        float x = BUILDING_X0 + 0.14f;
        draw_box(v3(x, 1.9f, -9.4f), v3(0.08f, 1.1f, 2.6f), C_DARK);
        sign_quad(callingScreen.texture, v3(x + 0.05f, 2.4f, -8.2f), v3(x + 0.05f, 2.4f, -10.6f),
                                         v3(x + 0.05f, 1.4f, -10.6f), v3(x + 0.05f, 1.4f, -8.2f));
    }
    // A water cooler, a table of magazines, and a box of toys for children waiting
    draw_box(v3(-16.1f, 0.55f, -5.0f), v3(0.4f, 1.1f, 0.4f), C_MACHINE);
    draw_box(v3(-16.1f, 1.3f, -5.0f), v3(0.3f, 0.4f, 0.3f), (Color){ 150, 200, 240, 255 });
    draw_box(v3(-13.5f, 0.35f, -5.3f), v3(1.2f, 0.06f, 0.6f), C_WOOD);
    draw_box(v3(-13.5f, 0.17f, -5.3f), v3(0.08f, 0.34f, 0.08f), C_STEEL);
    Color mags[3] = { C_PINK, C_BLUE, C_LEMON };
    for (int i = 0; i < 3; i++) draw_box(v3(-13.9f + i * 0.35f, 0.39f, -5.3f), v3(0.24f, 0.02f, 0.32f), mags[i]);
    draw_box(v3(-15.6f, 0.25f, -6.4f), v3(0.8f, 0.5f, 0.5f), (Color){ 240, 150, 35, 255 });
    child_standing(v3(-14.8f, 0, -6.6f), FACE_WEST, (Color){ 90, 170, 230, 255 }, C_NAVY, C_FAIR_HAIR);
    draw_plant(-16.0f, -12.4f);
    draw_plant(-11.0f, -5.0f);
    room_sign(WAITING);
}

// The waiting area's screen: who's next for each room on the second floor.
static void update_calling_screen(const Sim *sim)
{
    BeginTextureMode(callingScreen);
        ClearBackground((Color){ 14, 24, 40, 255 });
        DrawRectangle(0, 0, 320, 34, (Color){ 40, 150, 160, 255 });
        DrawText("NOW CALLING", 12, 7, 22, WHITE);
        for (int row = 0; row < WAIT_ROWS; row++) {
            int r = WAIT_ROOM[row];
            const Patient *next = sim->rooms[r].queue;
            int y = 44 + row * 23;
            DrawText(sim->rooms[r].name, 12, y, 18, (Color){ 170, 185, 210, 255 });
            const char *who = next ? TextFormat("Patient %d", next->id) : "-";
            DrawText(who, 308 - MeasureText(who, 18), y, 18, next ? (Color){ 120, 230, 160, 255 } : (Color){ 90, 100, 120, 255 });
        }
    EndTextureMode();
}

void floors3d_update(const Sim *sim)
{
    if (!ready) floors3d_prepare();
    update_calling_screen(sim);
}

// Cafe: the counter with a coffee machine and cakes, tables of visitors
static void draw_cafe(void)
{
    // The counter
    draw_box(v3(-15.4f, 0.5f, 1.6f), v3(0.8f, 1.0f, 5.0f), C_DARKWOOD);
    draw_box(v3(-15.4f, 1.02f, 1.6f), v3(0.9f, 0.05f, 5.1f), C_WOOD);
    draw_box(v3(-15.5f, 1.25f, 0.2f), v3(0.5f, 0.42f, 0.5f), C_DARK);                     // coffee machine
    draw_lit_box(v3(-15.24f, 1.32f, 0.2f), v3(0.01f, 0.06f, 0.16f), (Color){ 90, 220, 140, 255 });
    draw_box(v3(-15.3f, 1.2f, 2.4f), v3(0.6f, 0.32f, 1.2f), (Color){ 214, 232, 244, 255 });    // cake cabinet
    Color cakes[3] = { (Color){ 120, 70, 40, 255 }, C_PINK, C_LEMON };
    for (int i = 0; i < 6; i++) {
        draw_box(v3(-15.3f, 1.12f + (i / 3) * 0.12f, 1.95f + (i % 3) * 0.4f), v3(0.18f, 0.08f, 0.18f), cakes[i % 3]);
    }
    draw_person(v3(-16.1f, 0, 1.0f), FACE_EAST, outfit((Color){ 40, 40, 45, 255 }, C_NAVY, false), false, 0);
    // The menu on the outside wall
    if (!lowWest) {
        float x = BUILDING_X0 + 0.13f;
        draw_box(v3(x, 2.0f, 1.6f), v3(0.04f, 0.7f, 1.8f), C_DARK);
        sign_quad(menuBoard.texture, v3(x + 0.03f, 2.3f, 2.45f), v3(x + 0.03f, 2.3f, 0.75f),
                                     v3(x + 0.03f, 1.7f, 0.75f), v3(x + 0.03f, 1.7f, 2.45f));
    }
    // Tables, and visitors having tea
    Color tops[4] = { (Color){ 90, 120, 90, 255 }, (Color){ 190, 90, 110, 255 }, (Color){ 80, 110, 170, 255 }, (Color){ 210, 160, 70, 255 } };
    for (int t = 0; t < 4; t++) {
        float x = -13.0f + (t % 2) * 2.2f, z = -2.4f + (t / 2) * 3.2f + ((t % 2) ? 0.8f : 0.0f);
        draw_box(v3(x, 0.72f, z), v3(0.9f, 0.05f, 0.9f), C_WOOD);
        draw_box(v3(x, 0.36f, z), v3(0.1f, 0.72f, 0.1f), C_STEEL);
        furniture_chair(v3(x, 0, z - 0.7f), FACE_SOUTH, C_CHAIR);
        furniture_chair(v3(x, 0, z + 0.7f), FACE_NORTH, C_CHAIR);
        if (t != 3) person_seated(v3(x, 0, z - 0.7f), FACE_SOUTH, tops[t], C_TROUSERS, (t % 2) ? C_FAIR_HAIR : C_HAIR, false);
        if (t % 2 == 0) person_seated(v3(x, 0, z + 0.7f), FACE_NORTH, tops[(t + 2) % 4], C_NAVY, C_GREY_HAIR, false);
        draw_box(v3(x - 0.15f, 0.80f, z), v3(0.08f, 0.1f, 0.08f), C_CARD);         // cups
        draw_box(v3(x + 0.18f, 0.78f, z + 0.1f), v3(0.2f, 0.03f, 0.2f), C_CARD);    // a plate
    }
    draw_plant(-10.4f, 6.8f);
    draw_plant(-16.0f, 6.8f);
    room_sign(CAFE);
}

// ------------------------------------------------------------------------
// The lifts
// ------------------------------------------------------------------------

void floors3d_draw_lift(int floor, float carY, float doorsOpen)
{
    if (!ready) floors3d_prepare();
    float y = floor * FLOOR_H;
    rlPushMatrix();
    rlTranslatef(0, y, 0);

    float x0 = WING_X + 0.1f, x1 = SIDE_X - 0.1f, zf = LIFT_DOOR_Z, zb = BUILDING_Z1 - 0.1f;
    // The car, when it's on this floor (or passing through it)
    float local = carY - y;
    if (local > -FLOOR_H * 0.9f && local < FLOOR_H * 0.9f) {
        draw_box(v3(LIFT_X, local + 0.03f, (zf + zb) / 2), v3(x1 - x0 - 0.1f, 0.06f, zb - zf - 0.1f), (Color){ 150, 150, 160, 255 });
        draw_box(v3(x0 + 0.08f, local + 1.15f, (zf + zb) / 2), v3(0.05f, 2.2f, zb - zf - 0.1f), (Color){ 200, 205, 212, 255 });
        draw_box(v3(x1 - 0.08f, local + 1.15f, (zf + zb) / 2), v3(0.05f, 2.2f, zb - zf - 0.1f), (Color){ 200, 205, 212, 255 });
        draw_box(v3(LIFT_X, local + 1.0f, zb - 0.08f), v3(x1 - x0 - 0.1f, 0.06f, 0.05f), C_STEEL);    // handrail
        draw_lit_box(v3(LIFT_X, local + 2.28f, (zf + zb) / 2), v3(0.6f, 0.03f, 0.4f), (Color){ 255, 250, 230, 255 });
    }
    // The shaft: steel doors in a frame, sliding apart as they open
    draw_box(v3(LIFT_X, 2.45f, zf), v3(x1 - x0, 0.3f, 0.16f), C_STEEL);                // over the doors
    draw_box(v3(x0 + 0.08f, 1.15f, zf), v3(0.16f, 2.3f, 0.16f), C_STEEL);
    draw_box(v3(x1 - 0.08f, 1.15f, zf), v3(0.16f, 2.3f, 0.16f), C_STEEL);
    float half = (x1 - x0 - 0.32f) / 2, slide = doorsOpen * (half - 0.05f);
    Color door = { 185, 192, 202, 255 };
    draw_box(v3(LIFT_X - half / 2 - slide, 1.15f, zf - 0.02f), v3(half, 2.3f, 0.05f), door);
    draw_box(v3(LIFT_X + half / 2 + slide, 1.15f, zf - 0.02f), v3(half, 2.3f, 0.05f), door);
    // the floor indicator: G, 1, 2, with the lift's floor lit
    int at = (int)floorf(carY / FLOOR_H + 0.5f);
    for (int f = 0; f < FLOORS; f++) {
        Vector3 p = v3(LIFT_X - 0.25f + f * 0.25f, 2.45f, zf - 0.09f);
        if (f == at) draw_lit_box(p, v3(0.14f, 0.14f, 0.02f), (Color){ 255, 190, 60, 255 });
        else         draw_box(p, v3(0.14f, 0.14f, 0.02f), C_DARK);
    }
    // the call button
    draw_lit_box(v3(x1 + 0.12f, 1.1f, zf - 0.25f), v3(0.02f, 0.1f, 0.1f), (Color){ 120, 200, 255, 255 });
    // What's on this floor: a sign hanging in front of the lifts
    float sw = 1.5f, sh = 0.36f, sy = 2.2f, sz = zf - 1.2f;
    draw_box(v3(LIFT_X - 0.6f, sy + 0.4f, sz), v3(0.03f, 0.5f, 0.03f), C_STEEL);
    draw_box(v3(LIFT_X + 0.6f, sy + 0.4f, sz), v3(0.03f, 0.5f, 0.03f), C_STEEL);
    draw_box(v3(LIFT_X, sy, sz), v3(sw, sh, 0.05f), C_DARK);
    sign_facing_front(floorSign[floor].texture, LIFT_X, sy, sz + 0.03f, sw - 0.06f, sh - 0.06f);
    // The back and sides of the shaft (cut down when they face the camera)
    draw_wall(v3(WING_X, 0, (zf + BUILDING_Z1) / 2), 0.2f, BUILDING_Z1 - zf, FLOOR_H - 0.2f, lowWest);
    draw_wall(v3(SIDE_X, 0, (zf + BUILDING_Z1) / 2), 0.2f, BUILDING_Z1 - zf, FLOOR_H - 0.2f, lowEast);
    rlPopMatrix();
}

// ------------------------------------------------------------------------
// The functions view3d.c calls
// ------------------------------------------------------------------------

void floors3d_prepare(void)
{
    if (ready) return;
    for (int r = 0; r < UPPER_ROOMS; r++) {
        int w = MeasureText(ROOMS[r].name, 48) + 60;
        roomSign[r] = make_sign(ROOMS[r].name, w, 92, 48, ROOMS[r].colour, WHITE);
        roomSignW[r] = 0.46f * w / 92.0f;
    }
    floorSign[0] = make_sign("G   A&E, SURGERY, WARD",      600, 140, 44, C_DARK, WHITE);
    floorSign[1] = make_sign("1   SCANS, HEART, ICU",       600, 140, 44, C_DARK, WHITE);
    floorSign[2] = make_sign("2   BABIES, CHILDREN, THERAPY", 600, 140, 40, C_DARK, WHITE);
    radiationSign = make_sign("RADIATION", 384, 128, 56, (Color){ 245, 200, 40, 255 }, C_DARK);
    menuBoard = make_sign("COFFEE  TEA  CAKE", 480, 160, 44, C_DARK, (Color){ 250, 230, 180, 255 });
    callingScreen = LoadRenderTexture(320, 140);
    SetTextureFilter(callingScreen.texture, TEXTURE_FILTER_BILINEAR);
    // The ultrasound's picture: a grey fan on black
    ultrasoundScreen = LoadRenderTexture(160, 128);
    BeginTextureMode(ultrasoundScreen);
        ClearBackground((Color){ 5, 5, 8, 255 });
        DrawCircleSector((Vector2){ 80, 8 }, 112, 55, 125, 24, (Color){ 70, 70, 75, 255 });
        for (int i = 0; i < 60; i++) {
            float a = (55 + (i * 37) % 70) * DEG2RAD, r = 20 + (i * 53) % 90;
            DrawCircle((int)(80 + cosf(a) * r), (int)(8 + sinf(a) * r), 2 + i % 3, (Color){ 150, 150, 155, 255 });
        }
        DrawEllipse(80, 70, 22, 14, (Color){ 20, 20, 24, 255 });     // something round, like a baby's head
    EndTextureMode();
    SetTextureFilter(ultrasoundScreen.texture, TEXTURE_FILTER_BILINEAR);
    ready = true;
}

void floors3d_draw(const Sim *sim, int floor, Vector3 camPos, Vector3 camTarget)
{
    if (!ready) floors3d_prepare();
    simNow = sim;
    now = (float)GetTime();
    curFloor = floor;

    // Outside walls facing the camera are drawn low
    float lx = camPos.x - camTarget.x, lz = camPos.z - camTarget.z;
    float len = sqrtf(lx * lx + lz * lz);
    if (len < 0.001f) len = 0.001f;
    lowNorth = lz / len < -0.30f;
    lowSouth = lz / len >  0.30f;
    lowWest  = lx / len < -0.35f;
    lowEast  = lx / len >  0.35f;

    rlPushMatrix();
    rlTranslatef(0, floor * FLOOR_H, 0);
    draw_shell();
    if (floor == 1) {
        draw_ct();
        draw_ultrasound();
        draw_cath_lab();
        draw_icu();
        draw_pharmacy();
        draw_lab();
        draw_staff_room();
    } else {
        draw_physio();
        draw_dialysis();
        draw_maternity();
        draw_childrens_ward();
        draw_waiting_area();
        draw_cafe();
    }
    draw_queues();
    rlPopMatrix();
}

void floors3d_draw_glass(int floor)
{
    Color g = shade(C_GLASS, 1.0f);
    rlPushMatrix();
    rlTranslatef(0, floor * FLOOR_H, 0);
    if (floor == 1) {
        // The window into the cath lab's control corner
        quad(v3(6.7f, 1.0f, -8.2f), v3(8.0f, 1.0f, -8.2f), v3(8.0f, 2.1f, -8.2f), v3(6.7f, 2.1f, -8.2f), g);
        // The pharmacy's glass screen on the counter
        quad(v3(-14.2f, 1.05f, -3.8f), v3(-14.2f, 1.05f, 0.8f), v3(-14.2f, 1.6f, 0.8f), v3(-14.2f, 1.6f, -3.8f), g);
    } else {
        // The cafe's cake cabinet
        quad(v3(-15.0f, 1.04f, 1.8f), v3(-15.0f, 1.04f, 3.0f), v3(-15.0f, 1.36f, 3.0f), v3(-15.0f, 1.36f, 1.8f), g);
    }
    rlPopMatrix();
}

void floors3d_lights(const Sim *sim, int floor)
{
    float dark = view3d_darkness();
    bool lamps = view3d_lamps_on() || sim->shift == SHIFT_NIGHT;
    rlPushMatrix();
    rlTranslatef(0, floor * FLOOR_H, 0);
    // Daylight from the ceiling in every room, fading at dusk
    unsigned char day = (unsigned char)(20 * (1.0f - dark));
    for (int i = 0; i < UPPER_ROOMS; i++) {
        const UpperRoom *r = &ROOMS[i];
        if (r->floor != floor || day < 2) continue;
        glow((r->x0 + r->x1) / 2, (r->z0 + r->z1) / 2, 2.6f, (Color){ 255, 255, 255, day });
    }
    if (floor == 1) {
        if (lamps) {
            for (int i = 0; i < 4; i++) glow(ICU_BED_X[i], BED_ROW_Z, 1.5f, (Color){ 255, 200, 120, 60 });
            glow(0.0f, 1.3f, 2.4f, (Color){ 255, 220, 160, 55 });       // ICU station
            glow(-12.6f, 4.4f, 1.6f, (Color){ 255, 220, 160, 50 });     // staff room
        }
        glow(5.4f, -10.3f, 1.6f, (Color){ 255, 255, 235, 50 });         // cath lab lamp
        if (in_room(ROOM_CT, 0)) glow(-7.4f, -10.2f, 1.2f, (Color){ 255, 230, 160, 50 });
        glow(1.6f, -11.0f, 1.0f, (Color){ 140, 190, 255, 40 });          // the ultrasound screen
    } else {
        glow(-15.9f, -9.4f, 1.2f, (Color){ 140, 190, 255, 45 });          // the waiting area's screen
        if (lamps) {
            for (int i = 0; i < 2; i++) glow(MATERN_BED_X[i], BED_ROW_Z, 1.5f, (Color){ 255, 190, 210, 55 });
            for (int i = 0; i < 3; i++) glow(KIDS_BED_X, KIDS_BED_Z[i], 1.2f, (Color){ 255, 220, 150, 50 });
            glow(-13.0f, 0.0f, 2.6f, (Color){ 255, 220, 160, 45 });     // cafe
        }
        for (int i = 0; i < 3; i++) glow(DIALYSIS_X[i] + 0.95f, -11.0f, 0.8f, (Color){ 140, 190, 255, 40 });
    }
    glow(LIFT_X, 6.0f, 1.2f, (Color){ 255, 250, 230, 35 });             // by the lifts
    rlPopMatrix();
}

void floors3d_close(void)
{
    if (!ready) return;
    for (int r = 0; r < UPPER_ROOMS; r++) UnloadRenderTexture(roomSign[r]);
    for (int f = 0; f < FLOORS; f++) UnloadRenderTexture(floorSign[f]);
    UnloadRenderTexture(ultrasoundScreen);
    UnloadRenderTexture(menuBoard);
    UnloadRenderTexture(radiationSign);
    UnloadRenderTexture(callingScreen);
    ready = false;
}
