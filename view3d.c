// view3d.c - the department in 3D, drawn in a blocky "voxel" style.
//
// Everything is built from boxes. raylib's own cubes are one flat colour, so
// draw_box() draws each face with its own brightness instead (top brightest,
// sides darker). That fakes a light shining from above and gives depth
// without needing any shaders.
//
// Layout (1 unit is roughly 1 metre, looking from the waiting room):
//   back:   6 beds along the back wall, each with a heart monitor, a drip
//           stand and curtains between them
//   middle: the nurses' station, where free staff wait
//   front:  the waiting room, with a chair for each waiting patient,
//           reception, a vending machine, a water cooler and a TV
//   right:  the entrance doors, and the ambulance bay outside
//   left:   a doorway into the corridor to the rest of the hospital, which
//           rooms3d.c draws (ward, X-ray, MRI, bloods, theatre, nursery, admin)
//   outside: two ambulance bays with a road out, and a helipad
//
// Every patient is followed from frame to frame: when the simulation moves
// them (into a chair, a bed, a room, or home) they walk there. Emergencies
// arrive by ambulance, which drives in, reverses into a bay and leaves again;
// helicopter rescues land on the helipad.
//
// Shared colours and helpers are in view3d_parts.h, so rooms3d.c can use them.
//
// Patients walk in through the doors to a chair, and walk out again when
// they are discharged. The ambulance lights flash when an emergency arrives.

#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "rlgl.h"            // lower-level drawing: glows and signs with text on them
#include "draw.h"            // colours and screen size
#include "view3d.h"
#include "view3d_parts.h"   // colours and helpers shared with rooms3d.c
#include "rooms3d.h"        // the rest of the hospital

// ---- Layout ----
#define BED_Z        -3.2f   // the row of beds
#define BED_SPACING   2.5f
#define DESK_Z       -0.2f   // nurses' station
#define CHAIRS_PER_ROW 10
#define CHAIR_ROWS     2
#define CHAIR_SPACING  1.2f
#define WALL_BACK_H    3.0f  // height of the back wall
#define WALL_SIDE_H    2.4f  // height of the left wall
#define WALL_RIGHT_H   1.0f  // the right wall is low, so you can see people at the doors
#define DOOR_Z0        2.4f  // the entrance doors, in the right-hand wall
#define DOOR_Z1        4.6f
#define DOOR_X         8.1f
#define AMBULANCE_X   11.4f  // middle of a parked ambulance
#define AMB_BAYS       2
static const float BAY_Z[AMB_BAYS] = { 1.9f, 5.1f };   // the two ambulance bays
#define LANE_X        14.2f  // the road the ambulances drive along
#define ROAD_END      20.0f  // where the road leaves the picture
#define HELI_X        11.4f  // middle of the helipad
#define HELI_Z        -8.7f


// ---- Camera, controlled by the mouse ----
static float camYaw   = 0.0f;    // degrees around the department
static float camPitch = 42.0f;   // degrees above the floor
static float camDist  = 21.0f;   // distance from the centre
static Vector3 camTarget = { 3.0f, 0.4f, 0.9f };   // the point the camera looks at
                                                 // (a little right of A&E, to fit the ambulance in)

// Number keys glide the camera to a room. These are where it's gliding to.
static bool    gliding = false;
static Vector3 goalTarget;
static float   goalYaw, goalPitch, goalDist;

// ---- Where each member of staff is standing; they walk between spots ----
static Vector3 nursePos[NURSE_COUNT];
static Vector3 doctorPos[DOCTOR_COUNT];
static bool    staffPlaced = false;

static bool night = false;       // set each frame; darkens every colour

// ---- Every patient, followed from frame to frame ----
// Where the simulation has a patient. When it changes, they walk to the new place.
typedef enum { AT_CHAIR, AT_OVERFLOW, AT_BED, AT_ROOM, AT_QUEUE } PlaceKind;

typedef struct {
    PlaceKind kind;
    int a, b;            // chair: a = seat; bed: a = bed; room/queue: a = room, b = place / position
} Place;

#define MAX_TRACKED 256
#define MAX_POINTS   20

typedef struct {
    bool used;
    int id;
    Place place;                 // where the simulation has them now
    Color gown;
    bool rescue;
    bool seen;                   // found in the simulation this frame
    bool leaving;                // gone from the simulation: walking out, then forgotten
    bool walking;                // on their way somewhere
    int vehicle;                 // waiting to get out of: 0, 1 an ambulance, 2 the helicopter, -1 none
    Vector3 pos;
    Vector3 points[MAX_POINTS];
    int count;                   // points in use
    int next;                    // the point they are walking to
    float phase;                 // for swinging arms and legs
} Tracked;

static Tracked tracked[MAX_TRACKED];
static int    lastSeenId = 0;            // highest patient id already seen arriving
static double lastDrawTime = -100.0;     // to notice when the 3D view is opened again
static float  doorOpen = 0.0f;           // 0 = closed, 1 = fully open

// ---- Ambulances and the helicopter ----
typedef enum { V_IDLE, V_ARRIVING, V_UNLOADING, V_LEAVING } VehicleState;

typedef struct {
    VehicleState state;
    float t;                     // seconds into the current state
} Vehicle;

#define HELI 2                   // vehicle number of the helicopter
static Vehicle vehicles[AMB_BAYS + 1];   // ambulances 0 and 1 (one per bay), then the helicopter
static float rotor = 0.0f;               // the helicopter's rotor angle, in degrees

// How long each part takes, in seconds at normal speed
#define AMB_DRIVE_IN   4.0f
#define AMB_REVERSE    2.5f
#define AMB_UNLOAD     4.0f
#define AMB_TURN_OUT   2.0f
#define AMB_DRIVE_OUT  3.5f
#define HELI_APPROACH  4.5f
#define HELI_DESCEND   2.5f
#define HELI_UNLOAD    6.0f
#define HELI_CLIMB     2.0f
#define HELI_DEPART    3.5f

// ---- Screens on the wall behind each A&E bed ----
static RenderTexture2D bedScreen[BED_COUNT];

// ---- Signs with text on them, made once ----
static bool signsReady = false;
static RenderTexture2D signWard, signWaiting, signEmergency, signStation,
                       signReception, signAmbulance, tvScreen;

// ---- Walls that face the camera are drawn low, so they never block the view ----
static bool lowBack, lowLeft, lowRight;

// ------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------

Vector3 v3(float x, float y, float z) { return (Vector3){ x, y, z }; }

static float bed_x(int i) { return -6.25f + i * BED_SPACING; }

// Colour with a brightness factor, darker and bluer at night.
Color shade(Color c, float f)
{
    float r = c.r * f, g = c.g * f, b = c.b * f;
    if (night) { r *= 0.50f; g *= 0.56f; b *= 0.78f; }
    return (Color){ (unsigned char)fminf(r, 255), (unsigned char)fminf(g, 255),
                    (unsigned char)fminf(b, 255), c.a };
}

// One flat four-sided face. Back-face culling is switched off while the 3D
// view draws, so it shows from either side whichever way the camera looks.
void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col)
{
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}

// A box centred on p, with each face shaded differently to fake lighting.
void draw_box(Vector3 p, Vector3 size, Color c)
{
    float x0 = p.x - size.x / 2, x1 = p.x + size.x / 2;
    float y0 = p.y - size.y / 2, y1 = p.y + size.y / 2;
    float z0 = p.z - size.z / 2, z1 = p.z + size.z / 2;

    quad(v3(x0,y1,z0), v3(x1,y1,z0), v3(x1,y1,z1), v3(x0,y1,z1), shade(c, 1.00f)); // top
    quad(v3(x0,y0,z1), v3(x1,y0,z1), v3(x1,y1,z1), v3(x0,y1,z1), shade(c, 0.86f)); // front
    quad(v3(x0,y0,z0), v3(x1,y0,z0), v3(x1,y1,z0), v3(x0,y1,z0), shade(c, 0.70f)); // back
    quad(v3(x1,y0,z0), v3(x1,y0,z1), v3(x1,y1,z1), v3(x1,y1,z0), shade(c, 0.78f)); // right
    quad(v3(x0,y0,z0), v3(x0,y0,z1), v3(x0,y1,z1), v3(x0,y1,z0), shade(c, 0.64f)); // left
    quad(v3(x0,y0,z0), v3(x1,y0,z0), v3(x1,y0,z1), v3(x0,y0,z1), shade(c, 0.50f)); // bottom
}

// A box that gives off its own light (lamps, screens): not darkened at night.
void draw_lit_box(Vector3 p, Vector3 size, Color c)
{
    bool wasNight = night;
    night = false;
    draw_box(p, size, c);
    night = wasNight;
}

// A soft dark patch on the floor under people and furniture.
void shadow(float x, float z, float w, float d)
{
    Color c = { 0, 0, 0, night ? 70 : 38 };
    quad(v3(x - w/2, 0.01f, z - d/2), v3(x + w/2, 0.01f, z - d/2),
         v3(x + w/2, 0.01f, z + d/2), v3(x - w/2, 0.01f, z + d/2), c);
}

// A round pool of light on the floor: bright in the middle, fading to nothing
// at the edge. Must be drawn inside BeginBlendMode(BLEND_ADDITIVE).
void glow(float x, float z, float radius, Color c)
{
    const int slices = 20;
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < slices; i++) {
        float a0 = i * 2 * PI / slices, a1 = (i + 1) * 2 * PI / slices;
        rlColor4ub(c.r, c.g, c.b, c.a);  rlVertex3f(x, 0.03f, z);
        rlColor4ub(c.r, c.g, c.b, 0);    rlVertex3f(x + cosf(a0) * radius, 0.03f, z + sinf(a0) * radius);
        rlColor4ub(c.r, c.g, c.b, 0);    rlVertex3f(x + cosf(a1) * radius, 0.03f, z + sinf(a1) * radius);
    }
    rlEnd();
}

// A sign: a texture stretched over four corners (top-left, top-right,
// bottom-right, bottom-left). Render textures are stored upside down, so the
// top of the picture is at v = 1.
void sign_quad(Texture2D t, Vector3 tl, Vector3 tr, Vector3 br, Vector3 bl)
{
    Color tint = shade(WHITE, 1.0f);
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
        rlColor4ub(tint.r, tint.g, tint.b, 255);
        rlTexCoord2f(0, 1); rlVertex3f(tl.x, tl.y, tl.z);
        rlTexCoord2f(0, 0); rlVertex3f(bl.x, bl.y, bl.z);
        rlTexCoord2f(1, 0); rlVertex3f(br.x, br.y, br.z);
        rlTexCoord2f(1, 1); rlVertex3f(tr.x, tr.y, tr.z);
    rlEnd();
    rlSetTexture(0);
}

// A sign on a flat plate facing the camera side (+z), centred on (x, y) at depth z.
void sign_facing_front(Texture2D t, float x, float y, float z, float w, float h)
{
    sign_quad(t, v3(x - w/2, y + h/2, z), v3(x + w/2, y + h/2, z),
                 v3(x + w/2, y - h/2, z), v3(x - w/2, y - h/2, z));
}

static Color severity_color(int severity)
{
    if (severity == 1) return C_EMERGENCY;
    if (severity == 2) return C_URGENT;
    return C_ROUTINE;
}

// Move `from` part of the way towards `to`. Uses real time, so staff walk at
// the same pace whatever the simulation speed.
static Vector3 walk_towards(Vector3 from, Vector3 to)
{
    float t = 1.0f - expf(-6.0f * GetFrameTime());
    return v3(from.x + (to.x - from.x) * t, from.y, from.z + (to.z - from.z) * t);
}

static bool is_walking(Vector3 from, Vector3 to)
{
    float dx = to.x - from.x, dz = to.z - from.z;
    return dx * dx + dz * dz > 0.01f;
}

// ------------------------------------------------------------------------
// People
// ------------------------------------------------------------------------

// The way to face when moving from `from` to `to`.
static Facing facing_for(Vector3 from, Vector3 to)
{
    float dx = to.x - from.x, dz = to.z - from.z;
    if (fabsf(dx) > fabsf(dz)) return (dx > 0) ? FACE_EAST : FACE_WEST;
    return (dz > 0) ? FACE_SOUTH : FACE_NORTH;
}

// Body parts are placed relative to the person: lx to the side, lz forwards.
// part() turns that into a world position for whichever way they face.
static Vector3 partBase;
static Facing  partFace;

static void part(float lx, float y, float lz, float sx, float sy, float sz, Color c)
{
    float wx, wz, wsx = sx, wsz = sz;
    switch (partFace) {
        case FACE_SOUTH: wx =  lx; wz =  lz;                     break;
        case FACE_NORTH: wx = -lx; wz = -lz;                     break;
        case FACE_EAST:  wx =  lz; wz = -lx; wsx = sz; wsz = sx; break;
        default:         wx = -lz; wz =  lx; wsx = sz; wsz = sx; break;
    }
    draw_box(v3(partBase.x + wx, y, partBase.z + wz), v3(wsx, sy, wsz), c);
}

// A standing person. `phase` swings the arms and legs while walking.
void draw_person(Vector3 p, Facing face, Outfit o, bool walking, float phase)
{
    float swing = walking ? sinf(phase) : 0.0f;
    partBase = p;
    partFace = face;

    shadow(p.x, p.z, 0.6f, 0.6f);

    // Legs step forwards and back, and lift a little
    part(-0.11f, 0.25f + fmaxf(swing, 0) * 0.05f,  swing * 0.08f, 0.17f, 0.50f, 0.22f, o.legs);
    part( 0.11f, 0.25f + fmaxf(-swing, 0) * 0.05f, -swing * 0.08f, 0.17f, 0.50f, 0.22f, o.legs);

    // Body, and the coat that hangs below it
    part(0, 0.80f, 0, 0.48f, 0.60f, 0.30f, o.top);
    if (o.coat) {
        part(0, 0.50f, 0, 0.50f, 0.22f, 0.32f, o.top);                        // coat tails
        part(0, 0.86f, 0.16f, 0.14f, 0.40f, 0.02f, C_TROUSERS);               // shirt under the coat
    }
    part(-0.12f, 0.98f, 0.16f, 0.10f, 0.07f, 0.02f, WHITE);                   // name badge

    // Arms swing the other way to the legs; the clipboard arm stays still
    float armSwing = o.clipboard ? 0.0f : -swing * 0.10f;
    part(-0.31f, 0.82f,  swing * 0.10f, 0.13f, 0.50f, 0.16f, o.top);           // left arm
    part(-0.31f, 0.54f,  swing * 0.10f, 0.12f, 0.10f, 0.14f, C_SKIN);          // left hand
    part( 0.31f, 0.82f, armSwing, 0.13f, 0.50f, 0.16f, o.top);                 // right arm
    part( 0.31f, 0.54f, armSwing, 0.12f, 0.10f, 0.14f, C_SKIN);                // right hand
    if (o.clipboard) {
        part(0.31f, 0.66f, 0.15f, 0.05f, 0.36f, 0.26f, C_DESK);               // board
        part(0.34f, 0.67f, 0.15f, 0.02f, 0.30f, 0.20f, C_CARD);               // paper
    }

    if (o.stethoscope) {
        part(-0.08f, 0.98f, 0.17f, 0.03f, 0.22f, 0.02f, C_DARK);              // tubes round the neck
        part( 0.08f, 0.98f, 0.17f, 0.03f, 0.22f, 0.02f, C_DARK);
        part( 0.08f, 0.84f, 0.18f, 0.08f, 0.08f, 0.03f, C_STEEL);             // the round end
    }

    // Head, eyes, and hair (or a scrub cap)
    part(0, 1.27f, 0, 0.32f, 0.32f, 0.32f, C_SKIN);
    part(-0.07f, 1.30f, 0.165f, 0.05f, 0.05f, 0.01f, C_DARK);
    part( 0.07f, 1.30f, 0.165f, 0.05f, 0.05f, 0.01f, C_DARK);
    part(0, 1.30f, -0.165f, 0.34f, 0.26f, 0.03f, o.cap ? o.top : C_HAIR);     // back of the head
    part(0, 1.46f, -0.02f, 0.35f, 0.09f, 0.35f, o.cap ? C_SCRUBCAP : C_HAIR);
}

static Outfit nurse_outfit(void)
{
    return (Outfit){ C_BRAND, C_BRAND, false, false, true, true };
}

static Outfit doctor_outfit(void)
{
    return (Outfit){ C_COAT, C_TROUSERS, true, true, false, false };
}

// A patient walking in or out, in a gown of their severity colour.
static Outfit patient_outfit(Color gown)
{
    return (Outfit){ gown, C_TROUSERS, false, false, false, false };
}

// A patient lying in a bed, in a gown of their severity colour.
static void draw_lying_patient(float x, const Patient *p)
{
    draw_box(v3(x, 0.74f, BED_Z + 0.15f), v3(0.56f, 0.26f, 1.30f), severity_color(p->severity));
    draw_box(v3(x, 0.78f, BED_Z - 0.70f), v3(0.34f, 0.32f, 0.32f), C_SKIN);
    draw_box(v3(x, 0.78f, BED_Z - 0.86f), v3(0.36f, 0.34f, 0.06f), C_HAIR);
}

// ------------------------------------------------------------------------
// The ward: beds, monitors, drips and curtains
// ------------------------------------------------------------------------

static void draw_bed(float x)
{
    shadow(x, BED_Z, 1.4f, 2.4f);
    draw_box(v3(x, 0.25f, BED_Z), v3(1.2f, 0.40f, 2.2f), C_BEDFRAME);                  // frame
    draw_box(v3(x, 0.53f, BED_Z + 0.05f), v3(1.1f, 0.16f, 2.0f), C_MATTRESS);         // mattress
    draw_box(v3(x, 0.66f, BED_Z - 0.75f), v3(0.8f, 0.12f, 0.35f), C_CARD);            // pillow
    draw_box(v3(x, 0.60f, BED_Z - 1.10f), v3(1.2f, 1.00f, 0.10f), C_BEDFRAME);        // headboard
    draw_box(v3(x, 0.55f, BED_Z + 1.08f), v3(1.2f, 0.50f, 0.06f), C_BEDFRAME);        // footboard
}

// One beat of a heart trace, for a position 0..1 along the beat.
// A small bump, the tall spike, then a wider bump - the shape on a real monitor.
static float heartbeat(float t)
{
    if (t > 0.20f && t < 0.30f) return 0.15f * sinf((t - 0.20f) / 0.10f * PI);   // P wave
    if (t > 0.38f && t < 0.42f) return (t - 0.38f) / 0.04f;                       // up
    if (t > 0.42f && t < 0.46f) return 1.0f - (t - 0.42f) / 0.04f * 1.4f;         // down, past zero
    if (t > 0.46f && t < 0.50f) return -0.4f + (t - 0.46f) / 0.04f * 0.4f;        // back to zero
    if (t > 0.62f && t < 0.80f) return 0.25f * sinf((t - 0.62f) / 0.18f * PI);   // T wave
    return 0.0f;
}

// A heart monitor on a stand. The trace scrolls across the screen,
// faster for more serious patients, and goes red for an emergency.
void draw_monitor(float x, float z, const Patient *p)
{
    shadow(x, z, 0.4f, 0.4f);
    draw_box(v3(x, 0.03f, z), v3(0.40f, 0.05f, 0.40f), C_STEEL);                     // base
    draw_box(v3(x, 0.60f, z), v3(0.06f, 1.15f, 0.06f), C_STEEL);                     // pole
    draw_box(v3(x, 1.33f, z), v3(0.48f, 0.36f, 0.10f), C_DARK);                      // case

    float sx = 0.20f, y0 = 1.19f, y1 = 1.47f, fz = z + 0.052f;
    Color screen = p ? (Color){ 12, 24, 18, 255 } : (Color){ 30, 34, 40, 255 };
    bool wasNight = night;
    night = false;                                                                // screens glow
    quad(v3(x - sx, y0, fz), v3(x + sx, y0, fz), v3(x + sx, y1, fz), v3(x - sx, y1, fz), screen);
    night = wasNight;
    if (p == NULL) return;

    Color line = (p->severity == 1) ? (Color){ 255, 80, 90, 255 } : (Color){ 80, 255, 140, 255 };
    float rate = (p->severity == 1) ? 1.8f : (p->severity == 2) ? 1.3f : 1.0f;   // beats per second
    float mid = (y0 + y1) / 2 - 0.04f, amp = 0.11f, thick = 0.018f;
    const int steps = 40;
    for (int i = 0; i < steps; i++) {
        float u0 = (float)i / steps, u1 = (float)(i + 1) / steps;
        // two beats fit across the screen; the pattern moves left as time passes
        float b0 = fmodf(u0 * 2.0f + (float)GetTime() * rate, 1.0f);
        float b1 = fmodf(u1 * 2.0f + (float)GetTime() * rate, 1.0f);
        Vector3 a = v3(x - sx + 0.02f + u0 * (2 * sx - 0.04f), mid + heartbeat(b0) * amp, fz + 0.003f);
        Vector3 b = v3(x - sx + 0.02f + u1 * (2 * sx - 0.04f), mid + heartbeat(b1) * amp, fz + 0.003f);
        // a thin strip from a to b, so the line has some thickness
        float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
        float nx = -dy / len * thick, ny = dx / len * thick;
        quad(v3(a.x + nx, a.y + ny, a.z), v3(b.x + nx, b.y + ny, b.z),
             v3(b.x - nx, b.y - ny, b.z), v3(a.x - nx, a.y - ny, a.z), line);
    }
}

// A drip stand. The bag and tube only appear when someone is in the bed.
static void draw_drip(float x, float z, float patientX, bool inUse)
{
    draw_box(v3(x, 0.03f, z), v3(0.45f, 0.04f, 0.07f), C_STEEL);                     // feet
    draw_box(v3(x, 0.03f, z), v3(0.07f, 0.04f, 0.45f), C_STEEL);
    draw_box(v3(x, 0.98f, z), v3(0.04f, 1.95f, 0.04f), C_STEEL);                     // pole
    draw_box(v3(x, 1.94f, z), v3(0.32f, 0.03f, 0.03f), C_STEEL);                     // hook
    if (!inUse) return;
    draw_box(v3(x + 0.12f, 1.74f, z), v3(0.16f, 0.26f, 0.06f), (Color){ 205, 230, 245, 255 });  // bag
    DrawLine3D(v3(x + 0.12f, 1.60f, z), v3(patientX - 0.26f, 0.80f, BED_Z - 0.1f), shade(C_CARD, 1.0f));
}

// A curtain between two beds, in folds, hanging from a rail.
static void draw_curtain(float x)
{
    const float zBack = -5.0f, zFront = -3.4f;
    draw_box(v3(x, 2.15f, (zBack - 2.2f) / 2), v3(0.04f, 0.04f, -2.2f - zBack), C_STEEL);  // rail
    int folds = 8;
    float w = (zFront - zBack) / folds;
    for (int i = 0; i < folds; i++) {
        float off = (i % 2) ? 0.03f : -0.03f;                 // in and out, like real folds
        draw_box(v3(x + off, 1.20f, zBack + w * (i + 0.5f)), v3(0.03f, 1.80f, w),
                 shade(C_CURTAIN, (i % 2) ? 0.90f : 1.0f));
    }
}

// ------------------------------------------------------------------------
// The waiting room
// ------------------------------------------------------------------------

// A chair in the waiting room, with a seated patient if there is one.
static void draw_chair(float x, float z, const Patient *p)
{
    shadow(x, z, 0.75f, 0.75f);
    draw_box(v3(x, 0.22f, z), v3(0.62f, 0.44f, 0.60f), C_CHAIR);                      // seat
    draw_box(v3(x, 0.70f, z - 0.27f), v3(0.62f, 0.55f, 0.08f), C_CHAIR);              // back
    if (p == NULL) return;

    Color gown = severity_color(p->severity);
    draw_box(v3(x, 0.52f, z + 0.12f), v3(0.40f, 0.16f, 0.50f), gown);                 // thighs
    draw_box(v3(x, 0.26f, z + 0.38f), v3(0.40f, 0.42f, 0.16f), C_TROUSERS);           // shins
    draw_box(v3(x, 0.86f, z - 0.08f), v3(0.46f, 0.55f, 0.28f), gown);                 // body
    draw_box(v3(x, 1.30f, z - 0.08f), v3(0.32f, 0.32f, 0.32f), C_SKIN);               // head
    draw_box(v3(x - 0.07f, 1.33f, z + 0.085f), v3(0.05f, 0.05f, 0.01f), C_DARK);      // eyes
    draw_box(v3(x + 0.07f, 1.33f, z + 0.085f), v3(0.05f, 0.05f, 0.01f), C_DARK);
    draw_box(v3(x, 1.48f, z - 0.10f), v3(0.34f, 0.08f, 0.34f), C_HAIR);
}

static float chair_x(int seat) { return -5.4f + (seat % CHAIRS_PER_ROW) * CHAIR_SPACING; }
static float chair_z(int seat) { return 2.6f + (seat / CHAIRS_PER_ROW) * 1.7f; }

void draw_plant(float x, float z)
{
    shadow(x, z, 0.6f, 0.6f);
    draw_box(v3(x, 0.22f, z), v3(0.45f, 0.44f, 0.45f), (Color){ 185, 110, 70, 255 });  // pot
    Color leaf = { 70, 160, 80, 255 };
    draw_box(v3(x, 0.70f, z), v3(0.50f, 0.50f, 0.50f), leaf);
    draw_box(v3(x - 0.15f, 1.05f, z + 0.05f), v3(0.28f, 0.35f, 0.28f), shade(leaf, 1.1f));
    draw_box(v3(x + 0.14f, 1.10f, z - 0.08f), v3(0.24f, 0.42f, 0.24f), shade(leaf, 0.9f));
}

static void draw_vending_machine(float x, float z)
{
    shadow(x, z, 1.0f, 0.9f);
    draw_box(v3(x, 0.95f, z), v3(0.95f, 1.90f, 0.75f), (Color){ 200, 60, 60, 255 });
    float fz = z + 0.38f;
    quad(v3(x - 0.38f, 0.75f, fz), v3(x + 0.20f, 0.75f, fz),
         v3(x + 0.20f, 1.75f, fz), v3(x - 0.38f, 1.75f, fz), shade((Color){ 40, 50, 65, 255 }, 1.0f));
    // rows of snacks behind the glass
    Color snack[4] = { { 250, 200, 60, 255 }, { 80, 170, 230, 255 }, { 120, 200, 90, 255 }, { 240, 120, 60, 255 } };
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            draw_box(v3(x - 0.32f + c * 0.14f, 0.88f + r * 0.23f, fz), v3(0.09f, 0.12f, 0.02f),
                     snack[(r + c) % 4]);
        }
    }
    draw_box(v3(x + 0.31f, 1.30f, fz), v3(0.12f, 0.30f, 0.02f), C_DARK);             // buttons
    draw_box(v3(x - 0.09f, 0.40f, fz), v3(0.55f, 0.16f, 0.02f), C_DARK);             // the slot
}

static void draw_water_cooler(float x, float z)
{
    shadow(x, z, 0.5f, 0.5f);
    draw_box(v3(x, 0.50f, z), v3(0.40f, 1.00f, 0.40f), C_CARD);
    draw_box(v3(x, 0.80f, z + 0.21f), v3(0.08f, 0.06f, 0.04f), C_BRAND);              // tap
    draw_box(v3(x, 1.22f, z), v3(0.30f, 0.44f, 0.30f), (Color){ 130, 190, 240, 255 }); // bottle
}

// Reception desk by the doors, with someone working at it.
static void draw_reception(void)
{
    float x = 6.6f, z = 1.75f;
    shadow(x, z, 1.0f, 1.6f);
    draw_box(v3(x, 0.50f, z), v3(0.80f, 1.00f, 1.40f), C_DESK);
    draw_box(v3(x, 1.03f, z), v3(0.95f, 0.06f, 1.55f), shade(C_DESK, 1.15f));
    draw_box(v3(x + 0.15f, 1.25f, z - 0.3f), v3(0.05f, 0.34f, 0.46f), C_DARK);        // computer
    draw_person(v3(7.45f, 0, z), FACE_WEST, (Outfit){ C_RECEPTION, C_TROUSERS, false, false, false, false },
                false, 0);
    // "RECEPTION" on a little stand on the desk, facing the waiting room
    draw_box(v3(x, 1.30f, z + 0.70f), v3(0.90f, 0.48f, 0.04f), C_DARK);
    sign_facing_front(signReception.texture, x, 1.30f, z + 0.73f, 0.84f, 0.42f);
}

// The waiting time board on the left wall, updated every frame.
static void update_tv(const Sim *sim)
{
    float avg = (sim->stats.admitted > 0) ? sim->stats.totalWait / sim->stats.admitted : 0.0f;
    BeginTextureMode(tvScreen);
        ClearBackground((Color){ 20, 40, 80, 255 });
        DrawRectangle(0, 0, 256, 34, (Color){ 214, 69, 80, 255 });
        DrawText("A&E WAITING TIME", 128 - MeasureText("A&E WAITING TIME", 20) / 2, 8, 20, WHITE);
        const char *wait = (sim->stats.admitted > 0) ? TextFormat("%.0f min", sim_minutes(avg)) : "-- min";
        DrawText(wait, 128 - MeasureText(wait, 44) / 2, 48, 44, WHITE);
        const char *queue = TextFormat("%d waiting", sim->waitingCount);
        DrawText(queue, 128 - MeasureText(queue, 18) / 2, 104, 18, (Color){ 170, 200, 240, 255 });
    EndTextureMode();
}

// Text cut short with "..." so it fits in maxWidth.
static const char *fit(const char *text, int size, int maxWidth)
{
    if (MeasureText(text, size) <= maxWidth) return text;
    static char out[64];
    int n = (int)TextLength(text);
    if (n > 60) n = 60;
    while (n > 1) {
        n--;
        TextCopy(out, TextFormat("%.*s...", n, text));
        if (MeasureText(out, size) <= maxWidth) break;
    }
    return out;
}

// The screens on the wall behind each bed: patient, condition, who's with
// them, how far through treatment, and where they go next. Updated every frame.
static void update_bed_screens(const Sim *sim)
{
    const int W = 256;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        BeginTextureMode(bedScreen[i]);
            ClearBackground((Color){ 14, 24, 40, 255 });
            Color bar = p ? severity_color(p->severity) : (Color){ 70, 80, 98, 255 };
            DrawRectangle(0, 0, W, 32, bar);
            DrawText(TextFormat("BED %d", i + 1), 10, 7, 20, WHITE);
            if (p == NULL) {
                DrawText("FREE", W / 2 - MeasureText("FREE", 30) / 2, 70, 30, (Color){ 110, 125, 150, 255 });
                EndTextureMode();
                continue;
            }
            const char *id = TextFormat("%sPatient %d", p->rescue ? "RESCUE  " : "", p->id);
            DrawText(id, W - 10 - MeasureText(id, 16), 9, 16, WHITE);

            DrawText(fit(p->condition->name, 18, W - 20), 10, 40, 18, WHITE);
            DrawText(p->condition->icd10, 10, 60, 14, (Color){ 140, 160, 190, 255 });

            int n = sim_nurse_at(sim, i), d = sim_doctor_at(sim, i);
            const char *who; Color whoCol = (Color){ 120, 200, 255, 255 };
            if (d != -1)                          who = TextFormat("Dr %s", sim->doctors[d].name);
            else if (p->needsDoctor)            { who = "Waiting for a doctor"; whoCol = (Color){ 255, 120, 120, 255 }; }
            else if (n != -1 && p->assessLeft > 0) who = TextFormat("Assessing: %s", sim->nurses[n].name);
            else if (n != -1)                     who = TextFormat("Nurse %s", sim->nurses[n].name);
            else                                { who = "Waiting for a nurse"; whoCol = (Color){ 255, 190, 110, 255 }; }
            DrawText(fit(who, 16, W - 20), 10, 80, 16, whoCol);

            float done = 1.0f - p->treatmentLeft / p->treatmentTotal;
            if (done < 0) done = 0;
            DrawRectangle(10, 104, W - 20, 8, (Color){ 40, 52, 72, 255 });
            DrawRectangle(10, 104, (int)((W - 20) * done), 8, (Color){ 90, 220, 140, 255 });

            char next[PATHWAY_LEN];
            sim_route_text(p, next, (int)sizeof next);
            DrawText(TextFormat("Next: %s", next), 10, 120, 14, (Color){ 170, 185, 210, 255 });
        EndTextureMode();
    }
}

// ------------------------------------------------------------------------
// The building
// ------------------------------------------------------------------------

// A wall: mint lower half, green stripe, off-white upper half. A low wall
// (one facing the camera) is only the lower half, cut down.
void draw_wall(Vector3 centre, float lengthX, float lengthZ, float height, bool low)
{
    float lowH = low ? 0.35f : fminf(1.0f, height);
    draw_box(v3(centre.x, lowH / 2, centre.z), v3(lengthX, lowH, lengthZ), C_WALL_LOW);
    if (low || height <= 1.0f) return;
    // the stripe sits just proud of the wall
    draw_box(v3(centre.x, 1.06f, centre.z), v3(lengthX + 0.02f, 0.12f, lengthZ + 0.02f), C_STRIPE);
    draw_box(v3(centre.x, (1.12f + height) / 2, centre.z), v3(lengthX, height - 1.12f, lengthZ), C_WALL);
}

static void draw_floor(void)
{
    // Hospital vinyl: soft blue-grey in the ward, warm grey in the waiting room,
    // with faint tile joins.
    for (int ix = -8; ix < 8; ix++) {
        for (int iz = -5; iz < 6; iz++) {
            bool ward = iz < 1;
            bool alt = ((ix + iz) & 1) != 0;
            Color c = ward ? (alt ? (Color){ 210, 218, 228, 255 } : (Color){ 216, 223, 233, 255 })
                           : (alt ? (Color){ 214, 210, 202, 255 } : (Color){ 220, 216, 208, 255 });
            c = shade(c, 1.0f);
            quad(v3(ix, 0, iz), v3(ix + 1, 0, iz), v3(ix + 1, 0, iz + 1), v3(ix, 0, iz + 1), c);
        }
    }

    // Coloured guide lines on the floor, from the doors:
    // yellow to the waiting room, green to the ward.
    Vector3 yellow[] = { v3(7.9f, 0, 3.75f), v3(6.95f, 0, 3.75f), v3(6.95f, 0, 5.75f), v3(-6.0f, 0, 5.75f) };
    Vector3 green[]  = { v3(7.9f, 0, 3.25f), v3(6.05f, 0, 3.25f), v3(6.05f, 0, 1.55f),
                         v3(2.15f, 0, 1.55f), v3(2.15f, 0, -1.25f), v3(-6.6f, 0, -1.25f) };
    Color yc = shade((Color){ 240, 200, 40, 255 }, 1.0f), gc = shade((Color){ 60, 170, 110, 255 }, 1.0f);
    for (int pass = 0; pass < 2; pass++) {
        Vector3 *pts = pass ? green : yellow;
        int n = pass ? 6 : 4;
        Color c = pass ? gc : yc;
        for (int i = 0; i + 1 < n; i++) {
            float x0 = fminf(pts[i].x, pts[i + 1].x) - 0.06f, x1 = fmaxf(pts[i].x, pts[i + 1].x) + 0.06f;
            float z0 = fminf(pts[i].z, pts[i + 1].z) - 0.06f, z1 = fmaxf(pts[i].z, pts[i + 1].z) + 0.06f;
            quad(v3(x0, 0.012f, z0), v3(x1, 0.012f, z0), v3(x1, 0.012f, z1), v3(x0, 0.012f, z1), c);
        }
    }

    // Outside, on the right: tarmac with two ambulance bays, the road the
    // ambulances come and go on, and the helipad further back.
    Color tar = shade(C_ASPHALT, 1.0f);
    quad(v3(8.2f, -0.005f, -13.0f), v3(15.4f, -0.005f, -13.0f), v3(15.4f, -0.005f, 8.4f), v3(8.2f, -0.005f, 8.4f), tar);
    quad(v3(13.2f, -0.005f, 8.4f), v3(15.4f, -0.005f, 8.4f), v3(15.4f, -0.005f, ROAD_END), v3(13.2f, -0.005f, ROAD_END), tar);
    Color paint = shade(C_HIVIS, 1.0f);
    float t = 0.07f;
    for (int b = 0; b < AMB_BAYS; b++) {                 // a yellow box round each bay
        float bx0 = 9.4f, bx1 = 13.4f, bz0 = BAY_Z[b] - 1.15f, bz1 = BAY_Z[b] + 1.15f;
        quad(v3(bx0, 0.01f, bz0 - t), v3(bx1, 0.01f, bz0 - t), v3(bx1, 0.01f, bz0 + t), v3(bx0, 0.01f, bz0 + t), paint);
        quad(v3(bx0, 0.01f, bz1 - t), v3(bx1, 0.01f, bz1 - t), v3(bx1, 0.01f, bz1 + t), v3(bx0, 0.01f, bz1 + t), paint);
        quad(v3(bx1 - t, 0.01f, bz0), v3(bx1 + t, 0.01f, bz0), v3(bx1 + t, 0.01f, bz1), v3(bx1 - t, 0.01f, bz1), paint);
    }
    // White dashes down the middle of the road
    Color white = shade(C_CARD, 1.0f);
    for (float z = -1.0f; z < ROAD_END; z += 1.6f) {
        quad(v3(LANE_X - 0.05f, 0.01f, z), v3(LANE_X + 0.05f, 0.01f, z), v3(LANE_X + 0.05f, 0.01f, z + 0.8f), v3(LANE_X - 0.05f, 0.01f, z + 0.8f), white);
    }
    sign_quad(signAmbulance.texture, v3(9.4f, 0.012f, 6.9f), v3(13.0f, 0.012f, 6.9f),
                                     v3(13.0f, 0.012f, 7.8f), v3(9.4f, 0.012f, 7.8f));

    // The helipad: a pale square, a yellow ring, and a big white H
    Color pad = shade((Color){ 150, 156, 164, 255 }, 1.0f);
    quad(v3(HELI_X - 2.4f, 0.004f, HELI_Z - 2.4f), v3(HELI_X + 2.4f, 0.004f, HELI_Z - 2.4f),
         v3(HELI_X + 2.4f, 0.004f, HELI_Z + 2.4f), v3(HELI_X - 2.4f, 0.004f, HELI_Z + 2.4f), pad);
    const int seg = 32;
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg, r0 = 1.85f, r1 = 2.05f;
        quad(v3(HELI_X + cosf(a0) * r0, 0.01f, HELI_Z + sinf(a0) * r0), v3(HELI_X + cosf(a1) * r0, 0.01f, HELI_Z + sinf(a1) * r0),
             v3(HELI_X + cosf(a1) * r1, 0.01f, HELI_Z + sinf(a1) * r1), v3(HELI_X + cosf(a0) * r1, 0.01f, HELI_Z + sinf(a0) * r1), paint);
    }
    float hx = HELI_X, hz = HELI_Z, hw = 0.16f;
    quad(v3(hx - 0.65f - hw, 0.012f, hz - 0.9f), v3(hx - 0.65f + hw, 0.012f, hz - 0.9f),
         v3(hx - 0.65f + hw, 0.012f, hz + 0.9f), v3(hx - 0.65f - hw, 0.012f, hz + 0.9f), white);   // left of the H
    quad(v3(hx + 0.65f - hw, 0.012f, hz - 0.9f), v3(hx + 0.65f + hw, 0.012f, hz - 0.9f),
         v3(hx + 0.65f + hw, 0.012f, hz + 0.9f), v3(hx + 0.65f - hw, 0.012f, hz + 0.9f), white);   // right
    quad(v3(hx - 0.65f, 0.012f, hz - hw), v3(hx + 0.65f, 0.012f, hz - hw),
         v3(hx + 0.65f, 0.012f, hz + hw), v3(hx - 0.65f, 0.012f, hz + hw), white);                 // middle
}

// Green lights round the helipad and a windsock (they stand up, so not in draw_floor)
static void draw_helipad_kit(void)
{
    for (int i = 0; i < 4; i++) {
        float x = HELI_X + ((i % 2) ? 2.3f : -2.3f), z = HELI_Z + ((i < 2) ? 2.3f : -2.3f);
        draw_lit_box(v3(x, 0.08f, z), v3(0.16f, 0.16f, 0.16f), (Color){ 90, 230, 120, 255 });
    }
    float wx = HELI_X + 3.2f, wz = HELI_Z - 2.6f;
    draw_box(v3(wx, 1.2f, wz), v3(0.06f, 2.4f, 0.06f), C_STEEL);                    // pole
    float sway = 0.15f * sinf((float)GetTime() * 1.3f);
    for (int i = 0; i < 4; i++) {                                                   // the sock, in stripes
        Color c = (i % 2) ? C_CARD : (Color){ 240, 120, 40, 255 };
        draw_box(v3(wx - 0.18f - i * 0.22f, 2.25f - i * 0.04f + sway * i * 0.3f, wz), v3(0.22f, 0.26f - i * 0.03f, 0.26f - i * 0.03f), c);
    }
}

// A wall clock showing the shift time.
void draw_clock(float x, float y, float z, int hour, int minute)
{
    const int slices = 24;
    float r = 0.34f;
    for (int i = 0; i < slices; i++) {
        float a0 = i * 2 * PI / slices, a1 = (i + 1) * 2 * PI / slices;
        DrawTriangle3D(v3(x, y, z), v3(x + cosf(a0) * (r + 0.05f), y + sinf(a0) * (r + 0.05f), z),
                       v3(x + cosf(a1) * (r + 0.05f), y + sinf(a1) * (r + 0.05f), z), shade(C_DARK, 1.0f));
        DrawTriangle3D(v3(x, y, z + 0.01f), v3(x + cosf(a0) * r, y + sinf(a0) * r, z + 0.01f),
                       v3(x + cosf(a1) * r, y + sinf(a1) * r, z + 0.01f), shade(C_CARD, 1.0f));
    }
    for (int h = 0; h < 12; h++) {                                     // hour marks
        float a = h * PI / 6;
        draw_box(v3(x + sinf(a) * 0.27f, y + cosf(a) * 0.27f, z + 0.02f), v3(0.04f, 0.04f, 0.01f), C_DARK);
    }
    // hands: angles measured clockwise from 12
    float minA = minute / 60.0f * 2 * PI;
    float hourA = ((hour % 12) + minute / 60.0f) / 12.0f * 2 * PI;
    float lens[2] = { 0.17f, 0.26f }, angles[2] = { hourA, minA }, widths[2] = { 0.035f, 0.022f };
    for (int i = 0; i < 2; i++) {
        float sx = sinf(angles[i]), sy = cosf(angles[i]);
        float px = -sy * widths[i], py = sx * widths[i];               // sideways, for the hand's width
        float ex = x + sx * lens[i], ey = y + sy * lens[i], hz = z + 0.03f + i * 0.005f;
        quad(v3(x + px, y + py, hz), v3(ex + px, ey + py, hz), v3(ex - px, ey - py, hz), v3(x - px, y - py, hz),
             shade(C_DARK, 1.0f));
    }
}

// Sliding glass doors in the right-hand wall, with an EMERGENCY sign above.
static void draw_entrance(void)
{
    float h = 2.2f;
    draw_box(v3(DOOR_X, h / 2, DOOR_Z0), v3(0.26f, h, 0.14f), C_STEEL);             // frame
    draw_box(v3(DOOR_X, h / 2, DOOR_Z1), v3(0.26f, h, 0.14f), C_STEEL);
    draw_box(v3(DOOR_X, h + 0.1f, (DOOR_Z0 + DOOR_Z1) / 2), v3(0.26f, 0.2f, DOOR_Z1 - DOOR_Z0 + 0.14f), C_STEEL);

    // A blade sign sticking out over the bay, so it can be read from the front
    float sx = DOOR_X + 1.3f, sy = 2.85f, sz = DOOR_Z0 - 0.05f;
    draw_box(v3(DOOR_X + 0.2f, sy, sz), v3(0.4f, 0.06f, 0.06f), C_STEEL);            // bracket
    draw_box(v3(sx, sy, sz), v3(2.1f, 0.62f, 0.06f), (Color){ 200, 40, 50, 255 });
    sign_facing_front(signEmergency.texture, sx, sy, sz + 0.035f, 2.02f, 0.56f);
}

// The glass panels are see-through, so they're drawn after everything else.
static void draw_door_glass(void)
{
    float mid = (DOOR_Z0 + DOOR_Z1) / 2, half = (DOOR_Z1 - DOOR_Z0) / 2;
    float slide = doorOpen * (half - 0.1f);
    Color g = shade(C_GLASS, 1.0f);
    float x = DOOR_X - 0.02f;
    // left panel slides towards DOOR_Z0, right panel towards DOOR_Z1
    quad(v3(x, 0.05f, DOOR_Z0 + 0.07f - slide), v3(x, 0.05f, mid - slide),
         v3(x, 2.15f, mid - slide), v3(x, 2.15f, DOOR_Z0 + 0.07f - slide), g);
    quad(v3(x, 0.05f, mid + slide), v3(x, 0.05f, DOOR_Z1 - 0.07f + slide),
         v3(x, 2.15f, DOOR_Z1 - 0.07f + slide), v3(x, 2.15f, mid + slide), g);
}

// An ambulance with its middle at `pos`, turned `angle` degrees: at 0 the cab
// points along +x (as when parked, rear doors to the hospital), at 90 along -z.
// It's drawn round (0, 0) and rlgl's matrix moves and turns it into place.
static void draw_ambulance(Vector3 pos, float angle, bool flashing)
{
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(angle, 0, 1, 0);
    float x = 0, z = 0;
    shadow(x + 0.4f, z, 4.3f, 2.0f);
    draw_box(v3(x - 0.25f, 1.25f, z), v3(2.7f, 1.9f, 1.7f), C_HIVIS);               // box body
    draw_box(v3(x + 1.65f, 0.95f, z), v3(1.1f, 1.3f, 1.65f), C_HIVIS);              // cab
    draw_box(v3(x + 2.21f, 1.25f, z), v3(0.02f, 0.45f, 1.40f), (Color){ 50, 70, 100, 255 }); // windscreen
    draw_box(v3(x - 1.61f, 1.45f, z), v3(0.02f, 0.55f, 0.03f), C_DARK);             // gap between rear doors
    draw_box(v3(x - 1.61f, 1.55f, z - 0.4f), v3(0.02f, 0.40f, 0.45f), (Color){ 50, 70, 100, 255 }); // rear windows
    draw_box(v3(x - 1.61f, 1.55f, z + 0.4f), v3(0.02f, 0.40f, 0.45f), (Color){ 50, 70, 100, 255 });

    // The check pattern along both sides
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < 13; i++) {
            float cx = x - 1.5f + i * 0.30f + 0.15f;
            for (int row = 0; row < 2; row++) {
                Color c = ((i + row) % 2) ? C_AMB_GREEN : C_HIVIS;
                draw_box(v3(cx, 0.72f + row * 0.28f, z + side * 0.86f), v3(0.30f, 0.28f, 0.02f), c);
            }
        }
    }

    // Wheels
    for (int side = -1; side <= 1; side += 2) {
        draw_box(v3(x - 0.9f, 0.28f, z + side * 0.80f), v3(0.6f, 0.56f, 0.22f), C_DARK);
        draw_box(v3(x + 1.5f, 0.28f, z + side * 0.80f), v3(0.6f, 0.56f, 0.22f), C_DARK);
    }

    // Lights on the roof: red and blue, taking turns when flashing
    bool redOn  = flashing && fmodf((float)GetTime() * 6.0f, 2.0f) < 1.0f;
    bool blueOn = flashing && !redOn;
    Color red  = redOn  ? (Color){ 255, 60, 60, 255 } : (Color){ 130, 40, 45, 255 };
    Color blue = blueOn ? (Color){ 70, 140, 255, 255 } : (Color){ 40, 60, 120, 255 };
    if (redOn)  draw_lit_box(v3(x + 1.65f, 1.66f, z - 0.45f), v3(0.36f, 0.14f, 0.36f), red);
    else        draw_box    (v3(x + 1.65f, 1.66f, z - 0.45f), v3(0.36f, 0.14f, 0.36f), red);
    if (blueOn) draw_lit_box(v3(x + 1.65f, 1.66f, z + 0.45f), v3(0.36f, 0.14f, 0.36f), blue);
    else        draw_box    (v3(x + 1.65f, 1.66f, z + 0.45f), v3(0.36f, 0.14f, 0.36f), blue);
    rlPopMatrix();
}

// The coast guard helicopter: red and white, with spinning rotors. `pos` is
// the bottom of the skids; at angle 0 the nose points along +x.
static void draw_helicopter(Vector3 pos, float angle)
{
    Color red = { 210, 45, 45, 255 }, glass = { 50, 70, 100, 255 };
    shadow(pos.x, pos.z, 3.2f - fminf(pos.y, 8.0f) * 0.2f, 2.0f - fminf(pos.y, 8.0f) * 0.12f);

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(angle, 0, 1, 0);
    // Skids and struts
    for (int side = -1; side <= 1; side += 2) {
        draw_box(v3(0.1f, 0.05f, side * 0.75f), v3(2.6f, 0.08f, 0.08f), C_DARK);
        draw_box(v3(-0.5f, 0.28f, side * 0.65f), v3(0.07f, 0.45f, 0.07f), C_DARK);
        draw_box(v3( 0.7f, 0.28f, side * 0.65f), v3(0.07f, 0.45f, 0.07f), C_DARK);
    }
    // Body: red with a white stripe, a glass nose, and the tail boom
    draw_box(v3(0.1f, 1.05f, 0), v3(2.6f, 1.15f, 1.35f), red);
    draw_box(v3(0.1f, 0.95f, 0), v3(2.62f, 0.20f, 1.37f), C_CARD);
    draw_box(v3(1.45f, 1.05f, 0), v3(0.3f, 0.75f, 1.15f), glass);                    // nose window
    draw_box(v3(0.75f, 1.30f, 0), v3(0.7f, 0.45f, 1.37f), glass);                    // side windows
    draw_box(v3(-0.3f, 1.70f, 0), v3(1.4f, 0.35f, 0.9f), C_CARD);                    // engine on top
    draw_box(v3(-2.2f, 1.30f, 0), v3(2.6f, 0.30f, 0.30f), red);                      // tail boom
    draw_box(v3(-3.4f, 1.75f, 0), v3(0.45f, 0.75f, 0.10f), red);                     // tail fin
    draw_box(v3(-2.6f, 1.30f, 0), v3(0.25f, 0.30f, 0.32f), C_CARD);                  // white band
    // Tail rotor, spinning on the side of the fin
    rlPushMatrix();
    rlTranslatef(-3.4f, 1.75f, 0.12f);
    rlRotatef(rotor * 2.0f, 0, 0, 1);
    draw_box(v3(0, 0, 0), v3(0.9f, 0.08f, 0.03f), C_DARK);
    rlPopMatrix();
    // Main rotor: a mast and two long blades
    draw_box(v3(-0.1f, 2.0f, 0), v3(0.14f, 0.35f, 0.14f), C_DARK);
    rlPushMatrix();
    rlTranslatef(-0.1f, 2.18f, 0);
    rlRotatef(rotor, 0, 1, 0);
    draw_box(v3(0, 0, 0), v3(5.6f, 0.04f, 0.22f), C_DARK);
    draw_box(v3(0, 0, 0), v3(0.22f, 0.04f, 5.6f), C_DARK);
    rlPopMatrix();
    rlPopMatrix();
}

static void draw_room(const Sim *sim)
{
    draw_floor();

    // Walls: back, left, and right (split by the doors). Walls between the
    // camera and the room are cut down low so they never block the view.
    draw_wall(v3(0, 0, -5.1f), 16.4f, 0.2f, WALL_BACK_H, lowBack);
    draw_wall(v3(-8.1f, 0, -3.4f), 0.2f, 3.6f, WALL_SIDE_H, lowLeft);              // left, with a doorway
    draw_wall(v3(-8.1f, 0,  3.0f), 0.2f, 6.4f, WALL_SIDE_H, lowLeft);              // to the corridor
    draw_wall(v3(DOOR_X, 0, (-5.2f + DOOR_Z0) / 2), 0.2f, DOOR_Z0 + 5.2f, WALL_RIGHT_H, lowRight);
    draw_wall(v3(DOOR_X, 0, (DOOR_Z1 + 6.2f) / 2), 0.2f, 6.2f - DOOR_Z1, WALL_RIGHT_H, lowRight);
    draw_box(v3(-5.0f, 0.35f, 1.0f), v3(6.0f, 0.7f, 0.15f), C_WALL_LOW);              // ward / waiting divider,
    draw_box(v3( 5.0f, 0.35f, 1.0f), v3(6.0f, 0.7f, 0.15f), C_WALL_LOW);              // with a gap in the middle
    draw_box(v3(-5.0f, 0.72f, 1.0f), v3(6.02f, 0.06f, 0.17f), C_STRIPE);
    draw_box(v3( 5.0f, 0.72f, 1.0f), v3(6.02f, 0.06f, 0.17f), C_STRIPE);

    // On the back wall: the ward sign, a red cross, and over each bed a screen
    // showing who is in it, with a reading lamp underneath. (The clock is on
    // the ward's wall now, in rooms3d.c, so there's only one.)
    if (!lowBack) {
        float wz = -4.99f;
        draw_box(v3(0, 2.55f, wz), v3(2.7f, 0.62f, 0.04f), C_BRAND);
        sign_facing_front(signWard.texture, 0, 2.55f, wz + 0.025f, 2.6f, 0.54f);

        draw_box(v3(-3.75f, 2.5f, wz), v3(0.66f, 0.66f, 0.04f), C_CARD);             // red cross
        draw_box(v3(-3.75f, 2.5f, wz + 0.03f), v3(0.50f, 0.16f, 0.03f), C_EMERGENCY);
        draw_box(v3(-3.75f, 2.5f, wz + 0.03f), v3(0.16f, 0.50f, 0.03f), C_EMERGENCY);

        for (int i = 0; i < BED_COUNT; i++) {
            // The bed's screen: a dark case with the picture from update_bed_screens()
            draw_box(v3(bed_x(i), 1.95f, wz + 0.03f), v3(1.12f, 0.66f, 0.06f), C_DARK);
            bool wasNight = night;
            night = false;                                                        // screens glow
            sign_facing_front(bedScreen[i].texture, bed_x(i), 1.95f, wz + 0.065f, 1.04f, 0.585f);
            night = wasNight;

            bool on = night && sim->beds[i].occupant != NULL;
            Color lamp = on ? (Color){ 255, 225, 160, 255 } : (Color){ 225, 228, 235, 255 };
            if (on) draw_lit_box(v3(bed_x(i), 1.55f, wz + 0.04f), v3(0.6f, 0.07f, 0.08f), lamp);
            else    draw_box    (v3(bed_x(i), 1.55f, wz + 0.04f), v3(0.6f, 0.07f, 0.08f), lamp);
        }
    }

    // On the left wall: the WAITING ROOM sign and the TV with the waiting time.
    // Both face into the room (+x); the text runs towards -z.
    if (!lowLeft) {
        float wx = -7.99f;
        draw_box(v3(wx, 1.90f, 2.7f), v3(0.04f, 0.50f, 2.30f), C_BRAND);
        sign_quad(signWaiting.texture, v3(wx + 0.03f, 2.12f, 3.80f), v3(wx + 0.03f, 2.12f, 1.60f),
                                       v3(wx + 0.03f, 1.68f, 1.60f), v3(wx + 0.03f, 1.68f, 3.80f));
        draw_box(v3(wx, 1.75f, 4.9f), v3(0.08f, 0.72f, 1.22f), C_DARK);
        bool wasNight = night;
        night = false;                                                            // the TV glows
        sign_quad(tvScreen.texture, v3(wx + 0.05f, 2.06f, 5.45f), v3(wx + 0.05f, 2.06f, 4.35f),
                                    v3(wx + 0.05f, 1.44f, 4.35f), v3(wx + 0.05f, 1.44f, 5.45f));
        night = wasNight;
    }

    // Nurses' station, with a sign on the front
    shadow(0, DESK_Z, 3.4f, 1.1f);
    draw_box(v3(0, 0.45f, DESK_Z), v3(3.2f, 0.9f, 0.8f), C_DESK);
    draw_box(v3(0, 0.93f, DESK_Z), v3(3.4f, 0.06f, 0.95f), shade(C_DESK, 1.15f));
    draw_box(v3(-0.8f, 1.15f, DESK_Z), v3(0.5f, 0.38f, 0.05f), C_INK);                // computer screens
    draw_box(v3( 0.8f, 1.15f, DESK_Z), v3(0.5f, 0.38f, 0.05f), C_INK);
    draw_box(v3( 0.0f, 0.99f, DESK_Z - 0.1f), v3(0.4f, 0.06f, 0.3f), C_CARD);         // papers
    draw_box(v3( 1.35f, 1.00f, DESK_Z), v3(0.2f, 0.08f, 0.25f), C_DARK);              // phone
    draw_box(v3(0, 0.50f, DESK_Z + 0.41f), v3(2.10f, 0.36f, 0.02f), C_BRAND);
    sign_facing_front(signStation.texture, 0, 0.50f, DESK_Z + 0.425f, 2.0f, 0.30f);

    // Waiting room extras
    draw_vending_machine(-7.2f, 1.6f);
    draw_water_cooler(-6.3f, 1.5f);
    draw_plant(-7.4f, 3.6f);
    draw_plant(-7.4f, 5.5f);
    draw_plant( 7.5f, 5.6f);
    draw_reception();

    draw_entrance();
}

// ------------------------------------------------------------------------
// Patients walking about
// ------------------------------------------------------------------------
//
// Each frame, every patient in the simulation is looked up in `tracked`. A
// patient whose place has changed (a chair, an A&E bed, a room or a room's
// queue) is given a route to walk to the new place, and that place is drawn
// empty until they get there. New patients walk in from outside (or get out
// of an ambulance or the helicopter); patients who have left the simulation
// walk out of the hospital, and are then forgotten.

static int chair_seat_count(void) { return CHAIR_ROWS * CHAIRS_PER_ROW; }

static Tracked *find_tracked(int id)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        if (tracked[i].used && tracked[i].id == id) return &tracked[i];
    }
    return NULL;
}

bool view3d_patient_walking(int id)
{
    Tracked *t = find_tracked(id);
    return t != NULL && t->walking;
}

static void add_point(Tracked *t, Vector3 p)
{
    if (t->count < MAX_POINTS) t->points[t->count++] = p;
}

// Where someone stands for a place (the end of their walk there).
static Vector3 place_stand(Place pl)
{
    switch (pl.kind) {
        case AT_CHAIR:    return v3(chair_x(pl.a), 0, chair_z(pl.a) + 0.35f);
        case AT_OVERFLOW: return v3(6.4f, 0, 5.25f);
        case AT_BED:      return v3(bed_x(pl.a) + 0.9f, 0, BED_Z + 0.4f);
        default: {
            Vector3 pts[MAX_POINTS];
            int n = rooms3d_path(pl.a, pl.kind == AT_ROOM ? pl.b : -1 - pl.b, pts, MAX_POINTS);
            return pts[n - 1];
        }
    }
}

// The route from the junction outside A&E's side door to a place. Rooms come
// from rooms3d.c; an A&E bed is through the side door and along the beds.
static int path_from_junction(Place pl, Vector3 *pts)
{
    if (pl.kind == AT_ROOM || pl.kind == AT_QUEUE) {
        return rooms3d_path(pl.a, pl.kind == AT_ROOM ? pl.b : -1 - pl.b, pts, MAX_POINTS);
    }
    int n = 0;
    float x = (pl.kind == AT_BED) ? bed_x(pl.a) + 0.9f : -1.9f;
    pts[n++] = v3(-7.6f, 0, -0.9f);                     // just inside A&E's side door
    pts[n++] = v3(x, 0, -1.25f);                        // along in front of the beds
    pts[n++] = place_stand(pl);
    return n;
}

static bool same_point(Vector3 a, Vector3 b)
{
    return fabsf(a.x - b.x) < 0.01f && fabsf(a.z - b.z) < 0.01f;
}

// Walk from one place to another by way of the corridor: back along the way
// to `from` as far as the two routes share, then on to `to`.
static void route_between(Tracked *t, Place from, Place to)
{
    Vector3 a[MAX_POINTS], b[MAX_POINTS];
    int na = path_from_junction(from, a), nb = path_from_junction(to, b);
    int common = 0;
    while (common < na - 1 && common < nb - 1 && same_point(a[common], b[common])) common++;
    for (int i = na - 2; i >= common; i--) add_point(t, a[i]);      // back the way they came
    if (common == 0) add_point(t, rooms3d_junction());
    else             add_point(t, a[common - 1]);
    for (int i = common; i < nb; i++) add_point(t, b[i]);
}

// From somewhere in A&E (the doors, or a chair) to the side of a bed: out
// between the chairs, through the gap in the ward wall, and along the beds.
static void route_to_bed(Tracked *t, Vector3 from, int bed)
{
    float gapX;
    if (from.x > 6.0f) gapX = 5.95f;
    else gapX = -6.0f + roundf((from.x + 6.0f) / CHAIR_SPACING) * CHAIR_SPACING;
    float wardGap = (bed_x(bed) < 0) ? -1.9f : 1.9f;
    add_point(t, v3(gapX, 0, from.z));
    add_point(t, v3(gapX, 0, 1.6f));
    add_point(t, v3(wardGap, 0, 1.6f));
    add_point(t, v3(wardGap, 0, -1.45f));
    add_point(t, v3(bed_x(bed) + 0.9f, 0, -1.45f));
    add_point(t, place_stand((Place){ AT_BED, bed, 0 }));
}

// In through the front doors (from just inside) to a chair or a bed.
static void route_from_doors(Tracked *t, Place to)
{
    add_point(t, v3(DOOR_X, 0, 3.5f));
    add_point(t, v3(7.4f, 0, 3.5f));
    if (to.kind == AT_CHAIR) {
        float aisle = (to.a >= CHAIRS_PER_ROW) ? 5.25f : 3.45f;
        add_point(t, v3(7.0f, 0, aisle));
        add_point(t, v3(chair_x(to.a), 0, aisle));
        add_point(t, place_stand(to));
    } else if (to.kind == AT_BED) {
        route_to_bed(t, v3(7.4f, 0, 3.5f), to.a);
    } else {
        add_point(t, place_stand(to));                   // overflow: stand near the door
    }
}

// Out of A&E's front doors and away down the side of the road.
static void route_out_of_doors(Tracked *t)
{
    add_point(t, v3(5.95f, 0, 1.6f));
    add_point(t, v3(5.95f, 0, 3.5f));
    add_point(t, v3(DOOR_X, 0, 3.5f));
    add_point(t, v3(9.0f, 0, 3.5f));
    add_point(t, v3(9.2f, 0, 8.6f));
    add_point(t, v3(12.8f, 0, ROAD_END));
}

static void start_walk(Tracked *t)
{
    t->walking = true;
    t->next = 0;
}

// Plan a walk from where they are now (their old place) to `to`.
static void walk_to(Tracked *t, Place to)
{
    Place from = t->place;
    t->count = 0;
    if (to.kind == AT_BED && (from.kind == AT_CHAIR || from.kind == AT_OVERFLOW)) {
        route_to_bed(t, t->pos, to.a);
    } else if (to.kind == AT_CHAIR || to.kind == AT_OVERFLOW) {
        add_point(t, place_stand(to));                   // moving seats in the waiting room
    } else {
        route_between(t, from, to);
    }
    t->place = to;
    start_walk(t);
}

// They've left the simulation (gone home): walk out of the hospital.
static void walk_home(Tracked *t)
{
    Place from = t->place;
    t->count = 0;
    if (from.kind == AT_BED) {
        add_point(t, v3(bed_x(from.a) + 0.9f, 0, -1.45f));
        add_point(t, v3(1.95f, 0, -1.45f));
        add_point(t, v3(1.95f, 0, 1.6f));
    } else if (from.kind == AT_ROOM || from.kind == AT_QUEUE) {
        Vector3 a[MAX_POINTS];
        int na = path_from_junction(from, a);
        for (int i = na - 2; i >= 0; i--) add_point(t, a[i]);
        add_point(t, rooms3d_junction());
        add_point(t, v3(-7.6f, 0, -0.9f));
        add_point(t, v3(-1.9f, 0, -0.9f));
        add_point(t, v3(-1.9f, 0, 1.6f));
    } else {
        add_point(t, v3(7.0f, 0, 3.45f));
    }
    route_out_of_doors(t);
    t->leaving = true;
    start_walk(t);
}

// ---- Ambulances and the helicopter ----

// A point on a curve from a to c, bending towards b (t from 0 to 1).
static Vector3 curve(Vector3 a, Vector3 b, Vector3 c, float t)
{
    float u = 1 - t;
    return v3(u * u * a.x + 2 * u * t * b.x + t * t * c.x, 0, u * u * a.z + 2 * u * t * b.z + t * t * c.z);
}

static float ease(float t) { t = fminf(fmaxf(t, 0), 1); return t * t * (3 - 2 * t); }

// The angle for draw_ambulance/draw_helicopter to face along (dx, dz).
static float heading(float dx, float dz) { return atan2f(-dz, dx) * RAD2DEG; }

// Where ambulance `k` is and which way it faces, for its state and time.
static void ambulance_place(int k, Vector3 *pos, float *angle)
{
    const Vehicle *v = &vehicles[k];
    float bz = BAY_Z[k];
    Vector3 lane0 = v3(LANE_X, 0, ROAD_END), turn = v3(LANE_X, 0, bz - 2.6f);
    Vector3 corner = v3(LANE_X, 0, bz), parked = v3(AMBULANCE_X, 0, bz);
    switch (v->state) {
        case V_ARRIVING:
            if (v->t < AMB_DRIVE_IN) {                         // up the road, past the bay
                float k2 = ease(v->t / AMB_DRIVE_IN);
                *pos = v3(LANE_X, 0, lane0.z + (turn.z - lane0.z) * k2);
                *angle = 90.0f;                                // cab pointing up the road (-z)
            } else {                                           // then reversing round into it
                float k2 = ease((v->t - AMB_DRIVE_IN) / AMB_REVERSE);
                Vector3 p = curve(turn, corner, parked, k2), q = curve(turn, corner, parked, fminf(k2 + 0.01f, 1));
                *pos = p;
                *angle = (k2 >= 0.99f) ? 0.0f : heading(-(q.x - p.x), -(q.z - p.z));   // backwards
            }
            break;
        case V_UNLOADING:
            *pos = parked;
            *angle = 0.0f;
            break;
        default: {                                             // leaving: pull out, then down the road
            if (v->t < AMB_TURN_OUT) {
                float k2 = ease(v->t / AMB_TURN_OUT);
                Vector3 out = v3(LANE_X, 0, bz + 2.4f);
                Vector3 p = curve(parked, corner, out, k2), q = curve(parked, corner, out, fminf(k2 + 0.01f, 1));
                *pos = p;
                *angle = heading(q.x - p.x, q.z - p.z);
            } else {
                float k2 = ease((v->t - AMB_TURN_OUT) / AMB_DRIVE_OUT);
                *pos = v3(LANE_X, 0, (bz + 2.4f) + (ROAD_END + 4.0f - (bz + 2.4f)) * k2);
                *angle = -90.0f;                               // cab pointing down the road (+z)
            }
        }
    }
}

// Where the helicopter is (the bottom of its skids) and which way it faces.
static void helicopter_place(Vector3 *pos, float *angle)
{
    const Vehicle *v = &vehicles[HELI];
    Vector3 far0 = v3(42.0f, 20.0f, -32.0f), hover = v3(HELI_X, 6.0f, HELI_Z), far1 = v3(-30.0f, 22.0f, -40.0f);
    Vector3 pad = v3(HELI_X, 0, HELI_Z);
    float t = v->t;
    *angle = heading(hover.x - far0.x, hover.z - far0.z);       // facing the way it came in
    if (v->state == V_ARRIVING) {
        if (t < HELI_APPROACH) {
            float k = ease(t / HELI_APPROACH);
            *pos = v3(far0.x + (hover.x - far0.x) * k, far0.y + (hover.y - far0.y) * k, far0.z + (hover.z - far0.z) * k);
        } else {
            float k = ease((t - HELI_APPROACH) / HELI_DESCEND);
            *pos = v3(pad.x, hover.y * (1 - k), pad.z);
        }
    } else if (v->state == V_UNLOADING) {
        *pos = pad;
    } else {
        if (t < HELI_CLIMB) {
            float k = ease(t / HELI_CLIMB);
            *pos = v3(pad.x, hover.y * k, pad.z);
        } else {
            float k = ease((t - HELI_CLIMB) / HELI_DEPART);
            *pos = v3(hover.x + (far1.x - hover.x) * k, hover.y + (far1.y - hover.y) * k, hover.z + (far1.z - hover.z) * k);
            *angle = *angle + k * (heading(far1.x - hover.x, far1.z - hover.z) - *angle);
        }
    }
}

// How long the arriving part takes, then the unloading part.
static float vehicle_arrive_time(int k) { return (k == HELI) ? HELI_APPROACH + HELI_DESCEND : AMB_DRIVE_IN + AMB_REVERSE; }
static float vehicle_unload_time(int k) { return (k == HELI) ? HELI_UNLOAD : AMB_UNLOAD; }
static float vehicle_leave_time(int k)  { return (k == HELI) ? HELI_CLIMB + HELI_DEPART : AMB_TURN_OUT + AMB_DRIVE_OUT; }

// Is anyone still waiting to get out of vehicle k?
static bool vehicle_has_passenger(int k)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        if (tracked[i].used && tracked[i].vehicle == k) return true;
    }
    return false;
}

static void update_vehicles(float dt)
{
    rotor = fmodf(rotor + dt * 900.0f, 360.0f);
    for (int k = 0; k <= HELI; k++) {
        Vehicle *v = &vehicles[k];
        if (v->state == V_IDLE) {
            if (vehicle_has_passenger(k)) { v->state = V_ARRIVING; v->t = 0; }   // someone to bring in
            continue;
        }
        v->t += dt;
        if (v->state == V_ARRIVING && v->t >= vehicle_arrive_time(k)) { v->state = V_UNLOADING; v->t = 0; }
        else if (v->state == V_UNLOADING && v->t >= vehicle_unload_time(k)) { v->state = V_LEAVING; v->t = 0; }
        else if (v->state == V_LEAVING && v->t >= vehicle_leave_time(k)) { v->state = V_IDLE; v->t = 0; }
    }
}

// Get waiting passengers out once their vehicle has stopped.
static void unload_passengers(void)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || t->vehicle < 0 || vehicles[t->vehicle].state != V_UNLOADING) continue;
        t->count = 0;
        if (t->vehicle == HELI) {
            t->pos = v3(HELI_X - 1.0f, 0, HELI_Z + 0.6f);
            add_point(t, v3(8.85f, 0, HELI_Z + 2.6f));
            add_point(t, v3(8.85f, 0, 3.5f));
        } else {
            float bz = BAY_Z[t->vehicle];
            t->pos = v3(AMBULANCE_X - 2.0f, 0, bz);          // out of the back doors
            add_point(t, v3(8.85f, 0, bz));
            add_point(t, v3(8.85f, 0, 3.5f));
        }
        route_from_doors(t, t->place);
        t->vehicle = -1;
        start_walk(t);
    }
}

// A new patient: walk in (or ride in) to their first place.
static void arrive(Tracked *t, const Patient *p)
{
    t->count = 0;
    t->vehicle = -1;
    if (p->rescue) {
        t->vehicle = HELI;                    // waits for the helicopter to land
    } else if (p->severity == 1) {
        for (int k = 0; k < AMB_BAYS; k++) {  // a free ambulance brings them in
            if (vehicles[k].state == V_IDLE && !vehicle_has_passenger(k)) { t->vehicle = k; break; }
        }
    }
    t->walking = true;                        // their place stays empty until they get there
    if (t->vehicle >= 0) return;
    t->pos = v3(9.3f, 0, 8.8f);               // walking in from the street
    add_point(t, v3(9.0f, 0, 3.5f));
    route_from_doors(t, t->place);
    start_walk(t);
}

// Check every patient's place, and set off anyone who needs to move.
static void track_patient(const Patient *p, Place pl, bool fresh, bool resync)
{
    Tracked *t = find_tracked(p->id);
    if (t == NULL) {
        for (int i = 0; i < MAX_TRACKED && t == NULL; i++) if (!tracked[i].used) t = &tracked[i];
        if (t == NULL) return;                // table full: they just appear
        *t = (Tracked){ 0 };
        t->used = true;
        t->id = p->id;
        t->place = pl;
        t->gown = severity_color(p->severity);
        t->rescue = p->rescue;
        t->vehicle = -1;
        t->pos = place_stand(pl);
        t->seen = true;
        if (fresh && !resync) arrive(t, p);
        return;
    }
    t->seen = true;
    bool moved = (t->place.kind != pl.kind) ||
                 (pl.kind == AT_BED && t->place.a != pl.a) ||
                 (pl.kind == AT_ROOM && (t->place.a != pl.a || t->place.b != pl.b)) ||
                 (pl.kind == AT_QUEUE && t->place.a != pl.a);
    bool sameWaitingRoom = (t->place.kind == AT_CHAIR || t->place.kind == AT_OVERFLOW) &&
                           (pl.kind == AT_CHAIR || pl.kind == AT_OVERFLOW);
    if (t->vehicle >= 0) { t->place = pl; return; }               // still in the ambulance/helicopter
    if (sameWaitingRoom || (!moved && pl.kind != AT_CHAIR)) {      // shuffled along: no walk
        if (!t->walking) t->pos = place_stand(pl);
        t->place = pl;
        return;
    }
    if (!moved) { t->place = pl; return; }
    walk_to(t, pl);
}

static void update_patients(const Sim *sim, float speed)
{
    double now = GetTime();
    // Just opened the 3D view, or the simulation was reset: don't replay
    // everything that happened, just start watching from here.
    bool resync = (now - lastDrawTime > 0.5) || (sim->nextId - 1 < lastSeenId);
    lastDrawTime = now;
    if (resync) {
        for (int i = 0; i < MAX_TRACKED; i++) tracked[i].used = false;
        for (int k = 0; k <= HELI; k++) vehicles[k] = (Vehicle){ V_IDLE, 0 };
        lastSeenId = sim->nextId - 1;
    }
    for (int i = 0; i < MAX_TRACKED; i++) tracked[i].seen = false;

    // Everyone in the simulation, wherever they are
    int seat = 0;
    for (const Patient *p = sim->waiting; p != NULL; p = p->next, seat++) {
        Place pl = (seat < chair_seat_count()) ? (Place){ AT_CHAIR, seat, 0 } : (Place){ AT_OVERFLOW, 0, 0 };
        track_patient(p, pl, p->id > lastSeenId, resync);
    }
    for (int b = 0; b < BED_COUNT; b++) {
        const Patient *p = sim->beds[b].occupant;
        if (p) track_patient(p, (Place){ AT_BED, b, 0 }, p->id > lastSeenId, resync);
    }
    for (int r = 0; r < ROOM_TYPES; r++) {
        for (int s = 0; s < sim->rooms[r].capacity; s++) {
            const Patient *p = sim->rooms[r].in[s];
            if (p) track_patient(p, (Place){ AT_ROOM, r, s }, false, resync);
        }
        int k = 0;
        for (const Patient *p = sim->rooms[r].queue; p != NULL; p = p->next, k++) {
            track_patient(p, (Place){ AT_QUEUE, r, k < 4 ? k : 4 }, false, resync);
        }
    }
    if (sim->nextId - 1 > lastSeenId) lastSeenId = sim->nextId - 1;

    // Anyone no longer in the simulation has gone home
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || t->seen || t->leaving) continue;
        if (t->vehicle >= 0) { t->used = false; continue; }      // left before getting out
        walk_home(t);
    }

    // Vehicles move on real time, faster when the simulation is sped up
    float factor = fminf(fmaxf(speed, 1.0f), 4.0f);
    update_vehicles(GetFrameTime() * factor);
    unload_passengers();

    // Move everyone along
    float pace = 2.8f * factor * GetFrameTime();
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        float left = pace;
        while (left > 0 && t->next < t->count) {
            Vector3 g = t->points[t->next];
            float dx = g.x - t->pos.x, dz = g.z - t->pos.z, d = sqrtf(dx * dx + dz * dz);
            if (d <= left) { t->pos = g; t->next++; left -= d; }
            else           { t->pos.x += dx / d * left; t->pos.z += dz / d * left; left = 0; }
        }
        t->phase += pace * 5.0f;
        if (t->next >= t->count) {
            t->walking = false;                                  // arrived
            if (t->leaving) t->used = false;                     // gone home
        }
    }

    // Open the doors when anyone is near them
    bool someoneNear = false;
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        float dx = t->pos.x - DOOR_X, dz = t->pos.z - 3.5f;
        if (dx * dx + dz * dz < 2.2f * 2.2f) someoneNear = true;
    }
    float k = 1.0f - expf(-8.0f * GetFrameTime());
    doorOpen += ((someoneNear ? 1.0f : 0.0f) - doorOpen) * k;
}

static void draw_walkers(void)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        Vector3 target = t->points[t->next < t->count ? t->next : t->count - 1];
        draw_person(t->pos, facing_for(t->pos, target), patient_outfit(t->gown), true, t->phase);
        if (t->rescue) draw_box(v3(t->pos.x, 0.85f, t->pos.z), v3(0.52f, 0.5f, 0.34f), (Color){ 215, 220, 230, 255 });  // foil blanket
    }
}

static void draw_vehicles(void)
{
    for (int k = 0; k < AMB_BAYS; k++) {
        if (vehicles[k].state == V_IDLE) continue;
        Vector3 pos; float angle;
        ambulance_place(k, &pos, &angle);
        draw_ambulance(pos, angle, vehicles[k].state != V_LEAVING);
    }
    if (vehicles[HELI].state != V_IDLE) {
        Vector3 pos; float angle;
        helicopter_place(&pos, &angle);
        draw_helicopter(pos, angle);
    }
}

// Flashing blue and red light on the ground round any ambulance with its lights on.
static void vehicle_glows(void)
{
    bool redOn = fmodf((float)GetTime() * 6.0f, 2.0f) < 1.0f;
    for (int k = 0; k < AMB_BAYS; k++) {
        if (vehicles[k].state == V_IDLE || vehicles[k].state == V_LEAVING) continue;
        Vector3 pos; float angle;
        ambulance_place(k, &pos, &angle);
        glow(pos.x, pos.z, 3.0f, redOn ? (Color){ 255, 40, 40, 90 } : (Color){ 50, 110, 255, 90 });
    }
    if (vehicles[HELI].state != V_IDLE) {
        Vector3 pos; float angle;
        helicopter_place(&pos, &angle);
        if (pos.y < 8.0f) glow(HELI_X, HELI_Z, 2.6f, (Color){ 255, 255, 230, (unsigned char)(70 - pos.y * 8) });   // its searchlight
    }
}

// ------------------------------------------------------------------------
// Signs, made once the first time the 3D view is shown
// ------------------------------------------------------------------------

RenderTexture2D make_sign(const char *text, int w, int h, int size, Color bg, Color fg)
{
    RenderTexture2D rt = LoadRenderTexture(w, h);
    BeginTextureMode(rt);
        ClearBackground(bg);
        DrawText(text, w / 2 - MeasureText(text, size) / 2, h / 2 - size / 2, size, fg);
    EndTextureMode();
    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
    return rt;
}

static void make_signs(void)
{
    signWard      = make_sign("A&E  WARD",       512, 106, 60, C_BRAND, WHITE);
    signWaiting   = make_sign("WAITING ROOM",    512, 102, 54, C_BRAND, WHITE);
    signEmergency = make_sign("EMERGENCY",       400, 110, 64, (Color){ 200, 40, 50, 255 }, WHITE);
    signStation   = make_sign("NURSES' STATION", 512, 77,  40, C_BRAND, WHITE);
    signReception = make_sign("RECEPTION",       256, 128, 36, C_DARK, WHITE);
    signAmbulance = make_sign("AMBULANCES ONLY", 512, 122, 50, BLANK, C_HIVIS);
    tvScreen      = LoadRenderTexture(256, 144);
    SetTextureFilter(tvScreen.texture, TEXTURE_FILTER_BILINEAR);
    for (int i = 0; i < BED_COUNT; i++) {
        bedScreen[i] = LoadRenderTexture(256, 144);
        SetTextureFilter(bedScreen[i].texture, TEXTURE_FILTER_BILINEAR);
    }
    signsReady = true;
}

void view3d_close(void)
{
    if (!signsReady) return;
    UnloadRenderTexture(signWard);
    UnloadRenderTexture(signWaiting);
    UnloadRenderTexture(signEmergency);
    UnloadRenderTexture(signStation);
    UnloadRenderTexture(signReception);
    UnloadRenderTexture(signAmbulance);
    UnloadRenderTexture(tvScreen);
    for (int i = 0; i < BED_COUNT; i++) UnloadRenderTexture(bedScreen[i]);
    rooms3d_close();
    signsReady = false;
}

// ------------------------------------------------------------------------
// Camera
// ------------------------------------------------------------------------

// Start gliding the camera to look at `target` from `dist` away.
static void glide_to(Vector3 target, float yaw, float pitch, float dist)
{
    goalTarget = target;
    goalYaw = yaw;
    goalPitch = pitch;
    goalDist = dist;
    gliding = true;
    // Turn the short way round: keep the current angle within half a turn of the goal
    camYaw = fmodf(camYaw - yaw, 360.0f);
    if (camYaw > 180.0f)  camYaw -= 360.0f;
    if (camYaw < -180.0f) camYaw += 360.0f;
    camYaw += yaw;
}

void view3d_update_camera(void)
{
    // Number keys: 0 A&E, 1-7 the other rooms, 8 the whole hospital
    if (IsKeyPressed(KEY_ZERO))  glide_to(v3(3.0f, 0.4f, 0.9f), 0.0f, 42.0f, 21.0f);
    if (IsKeyPressed(KEY_EIGHT)) glide_to(v3(-4.0f, 0.4f, -3.2f), 0.0f, 58.0f, 38.0f);
    if (IsKeyPressed(KEY_NINE))  glide_to(v3(10.8f, 0.4f, -3.2f), 0.0f, 52.0f, 24.0f);   // helipad and bays
    for (int room = 1; room <= ROOM_COUNT; room++) {
        if (IsKeyPressed(KEY_ZERO + room)) {
            Vector3 centre;
            float dist = rooms3d_focus(room, &centre);
            glide_to(centre, 0.0f, 50.0f, dist);
        }
    }

    // Left-drag turns the camera; right-drag moves it across the floor
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 d = GetMouseDelta();
        camYaw   -= d.x * 0.3f;
        camPitch += d.y * 0.3f;
        gliding = false;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        float yaw = camYaw * DEG2RAD, k = camDist * 0.0025f;
        // right and forward along the floor, as seen from the camera
        Vector3 right = v3(cosf(yaw), 0, -sinf(yaw)), fwd = v3(-sinf(yaw), 0, -cosf(yaw));
        camTarget.x += (-right.x * d.x + fwd.x * d.y) * k;
        camTarget.z += (-right.z * d.x + fwd.z * d.y) * k;
        gliding = false;
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        camDist -= wheel * 1.2f;
        gliding = false;
    }

    if (gliding) {
        float t = 1.0f - expf(-5.0f * GetFrameTime());
        camTarget.x += (goalTarget.x - camTarget.x) * t;
        camTarget.z += (goalTarget.z - camTarget.z) * t;
        camYaw   += (goalYaw - camYaw) * t;
        camPitch += (goalPitch - camPitch) * t;
        camDist  += (goalDist - camDist) * t;
        if (fabsf(goalDist - camDist) < 0.05f && fabsf(goalYaw - camYaw) < 0.1f &&
            fabsf(goalPitch - camPitch) < 0.1f && fabsf(goalTarget.x - camTarget.x) < 0.02f &&
            fabsf(goalTarget.z - camTarget.z) < 0.02f) gliding = false;
    }

    // Keep the camera above the floor and within reach of the building
    if (camPitch < 12.0f) camPitch = 12.0f;
    if (camPitch > 85.0f) camPitch = 85.0f;
    if (camDist < 7.0f)  camDist = 7.0f;
    if (camDist > 48.0f) camDist = 48.0f;
    camTarget.x = fminf(fmaxf(camTarget.x, -17.0f), 15.0f);
    camTarget.z = fminf(fmaxf(camTarget.z, -13.0f), 8.0f);
}

static Camera3D make_camera(void)
{
    float yaw = camYaw * DEG2RAD, pitch = camPitch * DEG2RAD;
    Vector3 target = camTarget;
    Camera3D cam = { 0 };
    cam.target   = target;
    cam.position = v3(target.x + camDist * sinf(yaw) * cosf(pitch),
                      target.y + camDist * sinf(pitch),
                      target.z + camDist * cosf(yaw) * cosf(pitch));
    cam.up = v3(0, 1, 0);
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

// ------------------------------------------------------------------------
// 2D labels on top of the 3D scene
// ------------------------------------------------------------------------

static void label(const char *text, Vector3 at, Camera3D cam, int size, Color fg, Color bg)
{
    Vector2 s = GetWorldToScreen(at, cam);
    int w = MeasureText(text, size);
    DrawRectangleRounded((Rectangle){ s.x - w / 2 - 5, s.y - size / 2 - 3, w + 10, size + 6 },
                         0.5f, 6, bg);
    DrawText(text, (int)s.x - w / 2, (int)s.y - size / 2, size, fg);
}

static void progress_bar(Vector3 at, Camera3D cam, float done, Color fill)
{
    Vector2 s = GetWorldToScreen(at, cam);
    Rectangle track = { s.x - 28, s.y, 56, 7 };
    DrawRectangleRounded(track, 1.0f, 6, (Color){ 0, 0, 0, 90 });
    if (done > 0.04f) {
        DrawRectangleRounded((Rectangle){ track.x, track.y, track.width * done, track.height },
                             1.0f, 6, fill);
    }
}

static const char *status_text(const Sim *sim, int bed, const Patient *p)
{
    int n = sim_nurse_at(sim, bed), d = sim_doctor_at(sim, bed);
    if (d != -1)                       return TextFormat("Treated by Dr %s", sim->doctors[d].name);
    if (p->needsDoctor)                return "Waiting for a doctor";
    if (n != -1 && p->assessLeft > 0)  return TextFormat("Being assessed by %s", sim->nurses[n].name);
    if (n != -1)                       return TextFormat("Treated by %s", sim->nurses[n].name);
    return "Waiting for a nurse";
}

// The card shown when the mouse is over a bed.
static void bed_tooltip(const Sim *sim, int bed)
{
    const Patient *p = sim->beds[bed].occupant;
    Vector2 m = GetMousePosition();
    const char *l1 = p ? TextFormat("Bed %d   Patient %d", bed + 1, p->id) : TextFormat("Bed %d", bed + 1);
    const char *l2 = p ? (p->condition ? TextFormat("%s (%s)", p->condition->name, p->condition->icd10)
                                       : "Condition not recorded")
                       : "Free";
    const char *l3 = p ? status_text(sim, bed, p) : "";

    int w = MeasureText(l1, 18);
    if (MeasureText(l2, 16) > w) w = MeasureText(l2, 16);
    if (MeasureText(l3, 16) > w) w = MeasureText(l3, 16);
    Rectangle r = { m.x + 16, m.y + 12, w + 24, p ? 78 : 52 };
    if (r.x + r.width > SCREEN_W - 8) r.x = m.x - r.width - 16;

    DrawRectangleRounded(r, 0.2f, 8, C_CARD);
    DrawRectangleRoundedLines(r, 0.2f, 8, C_EDGE);
    if (p) DrawRectangle((int)r.x, (int)r.y + 8, 4, (int)r.height - 16, severity_color(p->severity));
    DrawText(l1, (int)r.x + 12, (int)r.y + 8, 18, C_INK);
    DrawText(l2, (int)r.x + 12, (int)r.y + 32, 16, C_MUTED);
    if (p) DrawText(l3, (int)r.x + 12, (int)r.y + 54, 16, C_BRAND);
}

static void hud(const Sim *sim, float speed, bool paused)
{
    Color text = night ? (Color){ 225, 232, 245, 255 } : C_INK;
    Color soft = night ? (Color){ 150, 165, 190, 255 } : C_MUTED;

    DrawText("Hospital Resource Simulator", 20, 16, 24, night ? (Color){ 140, 180, 240, 255 } : C_BRAND);
    const char *where = (camDist > 36.0f) ? "WHOLE HOSPITAL" : rooms3d_name_at(camTarget.x, camTarget.z);
    DrawText(TextFormat("3D VIEW   %s", where), 20, 46, 16, soft);

    // Stats card, top right
    int busyN = 0, busyD = 0;
    for (int k = 0; k < NURSES_ON(sim->shift); k++)  if (sim->nurses[k].busyWithBed != -1) busyN++;
    for (int k = 0; k < DOCTORS_ON(sim->shift); k++) if (sim->doctors[k].busyWithBed != -1) busyD++;
    int hh, mm;
    sim_time_of_day(sim, &hh, &mm);

    Rectangle card = { SCREEN_W - 230, 14, 216, 158 };
    DrawRectangleRounded(card, 0.12f, 8, night ? (Color){ 30, 38, 56, 230 } : (Color){ 255, 255, 255, 230 });
    int x = (int)card.x + 14, y = (int)card.y + 12;
    DrawText(TextFormat("%s  %02d:%02d", sim->shift == SHIFT_NIGHT ? "Night shift" : "Day shift", hh, mm),
             x, y, 18, text);                                                          y += 28;
    DrawText(TextFormat("Waiting      %d", sim->waitingCount), x, y, 16, soft);       y += 22;
    DrawText(TextFormat("Treated      %d", sim->stats.treated), x, y, 16, soft);      y += 22;
    DrawText(TextFormat("Nurses busy  %d / %d", busyN, NURSES_ON(sim->shift)), x, y, 16, soft);   y += 22;
    DrawText(TextFormat("Doctors busy %d / %d", busyD, DOCTORS_ON(sim->shift)), x, y, 16, soft);  y += 22;
    DrawText(TextFormat("Speed        %.1fx", speed), x, y, 16, soft);

    if (paused) DrawText("PAUSED", SCREEN_W / 2 - MeasureText("PAUSED", 30) / 2, 20, 30, C_EMERGENCY);

    DrawText("0 A&E   1 Ward   2 X-ray   3 MRI   4 Bloods   5 Theatre   6 Nursery   7 Admin   8 Whole hospital   9 Helipad",
             20, SCREEN_H - 50, 14, soft);
    DrawText("V 2D view   Drag to turn   Right-drag to move   Scroll to zoom   SPACE pause   UP/DOWN speed   E emergency   H helicopter",
             20, SCREEN_H - 30, 14, soft);
}

// ------------------------------------------------------------------------
// The frame
// ------------------------------------------------------------------------

void view3d_draw(const Sim *sim, float speed, bool paused)
{
    night = (sim->shift == SHIFT_NIGHT);
    if (!signsReady) make_signs();
    rooms3d_prepare(sim);            // the other rooms' signs, before the scene too
    update_tv(sim);                  // draws into the TV's picture, so before the scene
    update_bed_screens(sim);         // and the screens behind the beds
    update_patients(sim, paused ? 0.0f : speed);
    Camera3D cam = make_camera();

    // Which walls face the camera? Those are drawn low, like a doll's house.
    Vector3 look = { cam.position.x - cam.target.x, 0, cam.position.z - cam.target.z };
    float len = sqrtf(look.x * look.x + look.z * look.z);
    lowBack  = look.z / len < -0.30f;
    lowLeft  = look.x / len < -0.35f;
    lowRight = look.x / len >  0.35f;
    // Looking at the rooms behind or to the left of A&E: lower the A&E wall in the way
    if (camTarget.z < -4.5f && look.z > 0) lowBack = true;
    if (camTarget.x < -7.5f && look.x > 0) lowLeft = true;

    // Where everyone should be standing: by their bed, or at the station.
    if (!staffPlaced) {
        for (int k = 0; k < NURSE_COUNT; k++)  nursePos[k]  = v3(-3.0f + k * 1.0f, 0, DESK_Z + 0.9f);
        for (int k = 0; k < DOCTOR_COUNT; k++) doctorPos[k] = v3( 1.6f + k * 1.0f, 0, DESK_Z + 0.9f);
        staffPlaced = true;
    }
    Vector3 nurseTarget[NURSE_COUNT], doctorTarget[DOCTOR_COUNT];
    for (int k = 0; k < NURSE_COUNT; k++) {
        int b = sim->nurses[k].busyWithBed;
        nurseTarget[k] = (b != -1) ? v3(bed_x(b) + 0.30f, 0, BED_Z + 1.55f)     // foot of the bed
                                   : v3(-3.0f + k * 1.0f, 0, DESK_Z + 0.9f);    // at the station
    }
    for (int k = 0; k < DOCTOR_COUNT; k++) {
        int b = sim->doctors[k].busyWithBed;
        doctorTarget[k] = (b != -1) ? v3(bed_x(b) - 0.30f, 0, BED_Z + 1.55f)
                                    : v3(1.6f + k * 1.0f, 0, DESK_Z + 0.9f);
    }

    ClearBackground(night ? (Color){ 16, 22, 36, 255 } : C_BG);

    BeginMode3D(cam);
        rlDisableBackfaceCulling();          // so every face shows from both sides

        draw_room(sim);
        rooms3d_draw(sim, cam.position, cam.target);
        for (int i = 0; i < BED_COUNT; i++) {
            const Patient *bp = sim->beds[i].occupant;
            draw_bed(bed_x(i));
            if (bp && !view3d_patient_walking(bp->id)) draw_lying_patient(bed_x(i), bp);   // empty until they get there
            draw_monitor(bed_x(i) + 0.88f, BED_Z - 0.85f, bp);
            draw_drip(bed_x(i) - 0.88f, BED_Z - 0.60f, bed_x(i), bp != NULL);
            if (i + 1 < BED_COUNT) draw_curtain(bed_x(i) + BED_SPACING / 2);
        }

        // Waiting room: one chair per seat, filled most urgent first.
        // Someone still walking in leaves their chair empty until they get there.
        const Patient *p = sim->waiting;
        for (int seat = 0; seat < CHAIR_ROWS * CHAIRS_PER_ROW; seat++) {
            draw_chair(chair_x(seat), chair_z(seat), (p && !view3d_patient_walking(p->id)) ? p : NULL);
            if (p) p = p->next;
        }

        for (int k = 0; k < NURSES_ON(sim->shift); k++) {
            bool walking = is_walking(nursePos[k], nurseTarget[k]);
            Facing face = walking ? facing_for(nursePos[k], nurseTarget[k])
                                  : (sim->nurses[k].busyWithBed != -1 ? FACE_NORTH : FACE_SOUTH);
            nursePos[k] = walk_towards(nursePos[k], nurseTarget[k]);
            draw_person(nursePos[k], face, nurse_outfit(), walking, (float)GetTime() * 14.0f + k);
        }
        for (int k = 0; k < DOCTORS_ON(sim->shift); k++) {
            bool walking = is_walking(doctorPos[k], doctorTarget[k]);
            Facing face = walking ? facing_for(doctorPos[k], doctorTarget[k])
                                  : (sim->doctors[k].busyWithBed != -1 ? FACE_NORTH : FACE_SOUTH);
            doctorPos[k] = walk_towards(doctorPos[k], doctorTarget[k]);
            draw_person(doctorPos[k], face, doctor_outfit(), walking, (float)GetTime() * 14.0f + k);
        }
        draw_walkers();
        draw_vehicles();
        draw_helipad_kit();
        if (!lowRight) draw_door_glass();      // see-through, so last
        rooms3d_draw_glass();

        // Light. By day, soft pools under the ceiling lights. At night the room
        // is dark, with a lamp over each patient, the station lamp and the doors.
        BeginBlendMode(BLEND_ADDITIVE);
            if (!night) {
                for (int gx = -6; gx <= 6; gx += 4) {
                    glow((float)gx, -2.6f, 2.4f, (Color){ 255, 255, 255, 22 });
                    glow((float)gx,  3.6f, 2.4f, (Color){ 255, 255, 255, 22 });
                }
            } else {
                for (int i = 0; i < BED_COUNT; i++) {
                    if (sim->beds[i].occupant) glow(bed_x(i), BED_Z + 0.2f, 1.7f, (Color){ 255, 200, 120, 70 });
                }
                glow(0, DESK_Z + 0.4f, 2.2f, (Color){ 255, 220, 160, 60 });
                glow(DOOR_X, 3.5f, 1.8f, (Color){ 200, 220, 255, 50 });
                glow(6.6f, 1.75f, 1.4f, (Color){ 255, 220, 160, 45 });           // reception
            }
            rooms3d_lights(sim);
            vehicle_glows();
        EndBlendMode();

        rlEnableBackfaceCulling();
    EndMode3D();

    // Labels on top: bed numbers, progress, staff names, warnings
    Color tagBg = night ? (Color){ 30, 38, 56, 210 } : (Color){ 255, 255, 255, 210 };
    Color tagFg = night ? (Color){ 225, 232, 245, 255 } : C_INK;
    int hoverBed = -1;
    Ray ray = GetScreenToWorldRay(GetMousePosition(), cam);

    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *bp = sim->beds[i].occupant;
        label(TextFormat("Bed %d", i + 1), v3(bed_x(i), 1.30f, BED_Z - 1.10f), cam, 14, tagFg, tagBg);
        if (bp) {
            float done = 1.0f - bp->treatmentLeft / bp->treatmentTotal;
            bool stalled = bp->needsDoctor && sim_doctor_at(sim, i) == -1;
            progress_bar(v3(bed_x(i), 1.15f, BED_Z + 0.2f), cam, done, stalled ? C_EMERGENCY : C_BRAND);
            bool noNurse = !bp->needsDoctor && sim_nurse_at(sim, i) == -1;   // nobody has reached them yet
            if (stalled) label("needs Dr", v3(bed_x(i), 1.55f, BED_Z + 0.2f), cam, 14, WHITE, C_EMERGENCY);
            if (noNurse) label("needs nurse", v3(bed_x(i), 1.55f, BED_Z + 0.2f), cam, 14, WHITE, (Color){ 230, 145, 35, 255 });
        }
        BoundingBox box = { v3(bed_x(i) - 0.6f, 0, BED_Z - 1.1f), v3(bed_x(i) + 0.6f, 1.0f, BED_Z + 1.1f) };
        if (GetRayCollisionBox(ray, box).hit) hoverBed = i;
    }
    for (int k = 0; k < NURSES_ON(sim->shift); k++) {
        float h = 1.8f + (k % 2) * 0.3f;   // every second tag a little higher, so neighbours don't overlap
        label(sim->nurses[k].name, v3(nursePos[k].x, h, nursePos[k].z), cam, 12, WHITE, C_BRAND);
    }
    for (int k = 0; k < DOCTORS_ON(sim->shift); k++) {
        float h = 2.1f + (k % 2) * 0.3f;   // doctors' tags sit above the nurses'
        label(TextFormat("Dr %s", sim->doctors[k].name), v3(doctorPos[k].x, h, doctorPos[k].z),
              cam, 12, WHITE, C_INK);
    }
    int seats = CHAIR_ROWS * CHAIRS_PER_ROW;
    if (sim->waitingCount > seats) {
        label(TextFormat("+%d more waiting", sim->waitingCount - seats), v3(0, 0.1f, 6.4f),
              cam, 16, WHITE, C_EMERGENCY);
    }

    hud(sim, speed, paused);
    if (hoverBed != -1 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) bed_tooltip(sim, hoverBed);
    draw_rescue_banner(sim, SCREEN_W / 2, 72);      // "COAST GUARD RESCUE INBOUND"

}