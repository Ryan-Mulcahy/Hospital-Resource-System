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
// The hospital has three floors. PAGE UP / PAGE DOWN (or F) moves the view
// up and down: the floors above the one you're looking at are hidden, and
// the ones below are drawn closed up from outside (outside3d.c). Above the
// top floor is the view of the whole building from outside, with its roof.
// floors3d.c draws the floors upstairs and the lifts; patients going to a
// room upstairs walk to the lifts and ride up.
//
// Equipment (monitors, ventilators, defibrillators, drip pumps and
// wheelchairs) is drawn where it is: by the bed of the patient who has it,
// or parked in the equipment bay in A&E when it's free. Patients who need a
// wheelchair are pushed to their room by a porter.
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
#include <stdio.h>
#include "raylib.h"
#include "rlgl.h"            // lower-level drawing: glows and signs with text on them
#include "draw.h"            // colours and screen size
#include "view3d.h"
#include "view3d_parts.h"   // colours and helpers shared with rooms3d.c
#include "rooms3d.h"        // the rest of the hospital
#include "sound.h"          // the door chime, helicopter blades and rain
#include "opgame.h"         // the "SURGERY READY" banner
#include "floors3d.h"       // the floors upstairs, and the lifts
#include "outside3d.h"      // the grounds, and the building seen from outside

// ---- Layout ----
#define BED_Z        -3.2f   // the row of beds along the back wall
#define BED_SPACING   2.0f
#define WALL_BEDS    (BED_COUNT - 1)   // the 8 beds along the back wall; the last bed is resus
#define RESUS_X       6.6f   // the resus bay: a bed by the right-hand wall, turned sideways
#define RESUS_Z      -0.15f
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
#define LANE_IN       15.0f  // the road has two lanes: coming in (up the road)...
#define LANE_OUT      17.2f  // ...and going out (back down it)
#define ROAD_MID      16.1f  // the white line between them
#define ROAD_X0       13.9f  // the road's edges
#define ROAD_X1       18.3f
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

// ---- Which floor is being looked at ----
// 0 ground, 1 first, 2 second, VIEW_OUTSIDE the whole building from outside
static int viewFloor = 0;

// ---- The lift: the height of its floor, and how open the doors are on each floor ----
static float liftCarY = 0.0f;
static float liftDoors[FLOORS];

// ---- Where each member of staff is standing; they walk between spots ----
static Vector3 nursePos[NURSE_COUNT];
static Vector3 doctorPos[DOCTOR_SLOTS];
static bool    onCallWasIn = false;    // to see the on-call doctor arrive
static bool    staffPlaced = false;

static bool night = false;      // set each frame: the night shift is on

// ---- Time of day and weather, worked out each frame from the sim's clock ----
static float hourNow  = 12.0f;   // the time of day in hours, 0 to 24 (14.5 is 14:30)
static float darkness = 0.0f;    // 0 = broad daylight, 1 = the middle of the night
static float sunset   = 0.0f;    // 0 to 1: how golden the light is around sunset and sunrise
static float rain     = 0.0f;    // 0 = dry, 1 = pouring
static float wetness  = 0.0f;    // the puddles: they stay for a while after the rain stops

// ---- Cinematic camera: T for a tour, or click a patient to follow them ----
static bool    touring = false;  // the tour is on
static int     tourShot = 0;     // which shot it's on
static float   tourTime = 0.0f;  // seconds into this shot
static bool    heliCut = false;  // a helicopter is coming in: the tour cuts to the helipad
static int     followId = 0;     // the patient being followed (0: nobody)
static float   followIntro = 0;  // seconds left of swinging round to them
static Vector2 pressAt;          // where the left button went down
static bool    dragged = false;  // the mouse moved while the button was down: a drag, not a click
static bool    pressedHere = false;  // the button went down in the 3D view (not on another screen)
static bool    clickPending = false;
static Vector2 clickAt;          // a click, for view3d_draw to see what was clicked on

// ---- Sounds from the 3D scene ----
static bool   doorsWereNear = false;  // someone was near the doors last frame
static double lastChime = -100.0;     // the door chime plays at most once every 4 seconds

// ---- Every patient, followed from frame to frame ----
// Where the simulation has a patient. When it changes, they walk to the new place.
typedef enum { AT_CHAIR, AT_OVERFLOW, AT_BED, AT_ROOM, AT_QUEUE, AT_EXIT, AT_INBOUND } PlaceKind;   // AT_EXIT: being taken out; AT_INBOUND: in the air

typedef struct {
    PlaceKind kind;
    int a, b;            // chair: a = seat; bed: a = bed; room/queue: a = room, b = place / position
} Place;

#define MAX_TRACKED 256
#define MAX_POINTS   32

typedef struct {
    bool used;
    int id;
    Place place;                 // where the simulation has them now
    Color gown;
    bool rescue;
    int service;                 // which helicopter brings them (a RescueService)
    bool seen;                   // found in the simulation this frame
    bool leaving;                // gone from the simulation: walking out, then forgotten
    bool walking;                // on their way somewhere
    int vehicle;                 // waiting to get out of: 0, 1 an ambulance, 2 the helicopter, -1 none
    Vector3 pos;
    Vector3 points[MAX_POINTS];
    int count;                   // points in use
    int next;                    // the point they are walking to
    float walkSpeed;             // metres per second at 1x speed for this walk
    float phase;                 // for swinging arms and legs
    bool chair;                  // in a wheelchair, pushed by a porter
} Tracked;

static Tracked tracked[MAX_TRACKED];

// Patients walk every step of the way, start to finish. Walks to a bed or a
// room take as long as the simulation says (Patient.walkLeft: treatment or the
// scan only starts once they're there), so they arrive just as it starts.
// Other walks (going home, moving seats) are at a normal pace, a bit brisker
// when they're long.
#define WALK_PACE        2.4f    // normal walking speed, metres per second
#define WALK_FAST        4.0f    // the fastest anyone strolls on other walks
#define WALK_HURRY       6.0f    // the fastest anyone walks to keep to the simulation's time
#define WALK_MAX_SECONDS 3.0f
static int    lastSeenId = 0;            // highest patient id already seen arriving
static double lastDrawTime = -100.0;     // to notice when the 3D view is opened again
static float  doorOpen = 0.0f;           // 0 = closed, 1 = fully open

// ---- Ambulances and the helicopter ----
typedef enum { V_IDLE, V_ARRIVING, V_UNLOADING, V_LEAVING } VehicleState;

typedef struct {
    VehicleState state;
    float t;                     // seconds into the current state
    int service;                 // helicopters: whose it is (a RescueService), for its colours
} Vehicle;

// Vehicles 0 and 1 are the ambulances (one per bay); 2 and 3 are helicopters.
// Each helicopter brings one patient. Two of them take turns on the pad, so
// the next can be flying in while the last one takes off.
#define HELI_FIRST    AMB_BAYS
#define HELI_COUNT    2
#define VEHICLES      (AMB_BAYS + HELI_COUNT)
#define HELI_WAITING  99         // Tracked.vehicle: waiting for the next helicopter
static Vehicle vehicles[VEHICLES];
static float rotor = 0.0f;       // the helicopters' rotor angle, in degrees

static bool is_heli(int k) { return k >= HELI_FIRST; }

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
                       signReception, signAmbulance, signEquipment, tvScreen;

// ---- Walls that face the camera are drawn low, so they never block the view ----
static bool lowBack, lowLeft, lowRight, lowFront;

// ------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------

Vector3 v3(float x, float y, float z) { return (Vector3){ x, y, z }; }

static float bed_x(int i) { return -7.0f + i * BED_SPACING; }   // back-wall beds only

// Every bed is drawn as if it were along the back wall (head to the wall,
// foot towards the camera) and then turned into place. The resus bed is
// turned a quarter turn so its head is against the right-hand wall.
static Vector3 bed_centre(int i) { return (i == RESUS_BED) ? v3(RESUS_X, 0, RESUS_Z) : v3(bed_x(i), 0, BED_Z); }
static float   bed_turn(int i)   { return (i == RESUS_BED) ? -90.0f : 0.0f; }

// A point next to bed i, given as if the bed were along the back wall:
// lx across the bed, lz from its middle towards the foot.
static Vector3 bed_point(int i, float lx, float y, float lz)
{
    Vector3 c = bed_centre(i);
    if (i == RESUS_BED) return v3(c.x - lz, y, c.z + lx);     // a quarter turn
    return v3(c.x + lx, y, c.z + lz);
}

// Colour with a brightness factor, darker and bluer as night falls, and a
// little golden around sunset.
Color shade(Color c, float f)
{
    float r = c.r * f, g = c.g * f, b = c.b * f;
    r *= 1.0f - 0.50f * darkness;          // at full darkness: half the red,
    g *= 1.0f - 0.44f * darkness;          // a bit more of the green
    b *= 1.0f - 0.22f * darkness;          // and most of the blue: night looks blue
    g *= 1.0f - 0.10f * sunset;            // sunset: less green and blue, so warmer
    b *= 1.0f - 0.28f * sunset;
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
    float wasDark = darkness, wasSun = sunset;
    night = false; darkness = 0; sunset = 0;
    draw_box(p, size, c);
    night = wasNight; darkness = wasDark; sunset = wasSun;
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

// ------------------------------------------------------------------------
// Time of day and weather
// ------------------------------------------------------------------------

// 0 below a, 1 above b, and a smooth S-curve in between.
static float smooth(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}

static float mixf(float a, float b, float k) { return a + (b - a) * k; }

static Color mix_color(Color a, Color b, float k)
{
    return (Color){ (unsigned char)mixf(a.r, b.r, k), (unsigned char)mixf(a.g, b.g, k),
                    (unsigned char)mixf(a.b, b.b, k), (unsigned char)mixf(a.a, b.a, k) };
}

// Scrambles a number into a random-looking one. The same number always gives
// the same answer, so the weather is the same every time a day is played,
// and it doesn't use up the simulation's random numbers.
static unsigned int scramble(unsigned int x)
{
    x ^= x >> 16;  x *= 0x7feb352du;
    x ^= x >> 15;  x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// How hard it rains in hour `hour` of the day (0 is 07:00 to 08:00): about
// one hour in five, at 0.5 to 1. The first two hours of day 1 stay dry, and
// so does 16:00 to 19:00 every day, so there's always a sunset to see.
static float rain_in_hour(int day, int hour)
{
    hour = ((hour % 24) + 24) % 24;                            // the same hours every day
    if (day == 1 && hour < 2) return 0.0f;
    if (hour >= 9 && hour <= 11) return 0.0f;
    unsigned int h = scramble((unsigned int)(day * 1000 + hour + 500));
    if (h % 100 >= 20) return 0.0f;
    return 0.5f + 0.5f * (float)((h >> 8) % 100) / 100.0f;
}

// Work out the light and the weather from the simulation's clock.
static void update_weather(const Sim *sim)
{
    float hours = sim->clock / SHIFT_LENGTH * 12.0f;          // hours since 07:00
    hourNow = fmodf(7.0f + hours, 24.0f);
    float h = hourNow;

    if (!night) {                      // day shift, 07:00 to 19:00: the sun goes down from 17:00
        darkness = 0.9f * smooth(17.0f, 19.0f, h);
        sunset = smooth(16.5f, 18.0f, h) * (1.0f - smooth(18.3f, 19.0f, h));
    } else if (h >= 5.0f && h < 7.0f) { // dawn, 05:00 to 07:00
        darkness = 1.0f - 0.85f * smooth(5.0f, 7.0f, h);
        sunset = 0.8f * smooth(5.0f, 6.0f, h) * (1.0f - smooth(6.3f, 7.0f, h));
    } else {                           // the middle of the night
        darkness = 1.0f;
        sunset = 0.0f;
    }

    int hour = (int)floorf(hours);
    float frac = hours - hour;
    float now = rain_in_hour(sim->day, hour), next = rain_in_hour(sim->day, hour + 1);
    rain = mixf(now, next, smooth(0.8f, 1.0f, frac));          // eases into the next hour's weather
    // Puddles dry up over the two hours after the rain stops
    wetness = fmaxf(rain, fmaxf(rain_in_hour(sim->day, hour - 1) * (1.0f - 0.5f * frac),
                                rain_in_hour(sim->day, hour - 2) * (0.5f - 0.5f * frac)));

    darkness += 0.2f * rain * (1.0f - darkness);               // rain clouds make it gloomier
    sunset *= 1.0f - rain;                                     // and hide the sunset
}

// The sky behind everything: blue by day, orange and purple at sunset, deep
// blue at night, and grey when it rains.
static void draw_sky_gradient(void)
{
    Color top    = mix_color((Color){ 150, 195, 240, 255 }, (Color){  8, 12, 30, 255 }, darkness);
    Color bottom = mix_color((Color){ 232, 241, 250, 255 }, (Color){ 26, 34, 58, 255 }, darkness);
    top    = mix_color(top,    (Color){  90,  80, 150, 255 }, 0.7f * sunset);
    bottom = mix_color(bottom, (Color){ 252, 160,  90, 255 }, 0.8f * sunset);
    float grey = 0.7f * rain;
    float light = 1.0f - 0.8f * darkness;
    top    = mix_color(top,    (Color){ (unsigned char)(110 * light + 15), (unsigned char)(118 * light + 18), (unsigned char)(128 * light + 25), 255 }, grey);
    bottom = mix_color(bottom, (Color){ (unsigned char)(170 * light + 20), (unsigned char)(175 * light + 25), (unsigned char)(182 * light + 35), 255 }, grey);
    ClearBackground(bottom);
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), top, bottom);
}

// Which way the sun is: it rises in the east (+x) at 06:00, is highest at
// about 12:45 and sets in the west (-x) at 19:30, keeping low in the sky so
// you can see it when the camera is tipped down low.
static Vector3 sun_direction(float h, float *height)
{
    float a = (h - 6.0f) / 13.5f * PI;                         // 0 at sunrise, PI at sunset
    *height = sinf(a);
    Vector3 d = v3(cosf(a), 0.03f + 0.35f * sinf(a), -0.7f);
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    return v3(d.x / len, d.y / len, d.z / len);
}

#define STARS 160
static Vector3 starDir[STARS];
static bool starsMade = false;

// A soft round glow: circles from big and faint to small, adding up to
// brighter in the middle. (raylib's DrawCircleGradient isn't used because
// it changed between raylib 5.5 and 6.0.)
static void soft_glow(Vector2 at, float radius, Color c, float strength)
{
    for (int i = 0; i < 10; i++) {
        float r = radius * (1.0f - i / 10.0f);
        DrawCircleV(at, r, (Color){ c.r, c.g, c.b, (unsigned char)(255 * strength / 10.0f) });
    }
}

// Where a direction in the sky lands on the screen; false if it's behind the camera.
static bool sky_to_screen(Vector3 dir, Camera3D cam, Vector2 *out)
{
    Vector3 f = v3(cam.target.x - cam.position.x, cam.target.y - cam.position.y, cam.target.z - cam.position.z);
    if (dir.x * f.x + dir.y * f.y + dir.z * f.z <= 0.0f) return false;
    Vector3 p = v3(cam.position.x + dir.x * 150, cam.position.y + dir.y * 150, cam.position.z + dir.z * 150);
    *out = GetWorldToScreen(p, cam);
    return true;
}

// The sun, the moon and the stars. Drawn flat on the sky before the 3D scene,
// so the ground, the hospital and the trees are in front of them.
static void draw_sky_objects(Camera3D cam)
{
    float clear = 1.0f - rain;
    float px = GetScreenHeight() / 600.0f;                     // sizes grow with the window
    Vector2 s;
    if (!starsMade) {
        for (int i = 0; i < STARS; i++) {
            unsigned int h = scramble((unsigned int)i * 7919u + 1u);
            float yaw = (h % 3600) / 3600.0f * 2 * PI;
            float up = 0.02f + 0.9f * (float)((h >> 12) % 1000) / 1000.0f;  // above the horizon
            float flat = sqrtf(1.0f - up * up);
            starDir[i] = v3(cosf(yaw) * flat, up, sinf(yaw) * flat);
        }
        starsMade = true;
    }

    // Stars come out as it gets dark, and twinkle
    float starLight = smooth(0.55f, 0.95f, darkness) * clear;
    if (starLight > 0.01f) {
        for (int i = 0; i < STARS; i++) {
            if (!sky_to_screen(starDir[i], cam, &s)) continue;
            float tw = 0.6f + 0.4f * sinf((float)GetTime() * (1.5f + (i % 7) * 0.4f) + i);
            unsigned char a = (unsigned char)(230 * starLight * tw);
            DrawCircleV(s, (i % 5 == 0 ? 1.6f : 1.0f) * px, (Color){ 255, 255, 240, a });
        }
    }

    // The sun, with a soft glow, orange when it's low
    float height;
    Vector3 sd = sun_direction(hourNow, &height);
    if (height > 0.0f && clear > 0.05f && sky_to_screen(sd, cam, &s)) {
        float low = fmaxf(1.0f - smooth(0.0f, 0.8f, height), sunset);   // orange near the horizon
        Color sun = mix_color((Color){ 255, 250, 225, 255 }, (Color){ 255, 140, 60, 255 }, low);
        unsigned char a = (unsigned char)(255 * clear);
        soft_glow(s, 90 * px, sun, 0.45f * clear);
        DrawCircleV(s, 22 * px, (Color){ sun.r, sun.g, sun.b, a });
    }

    // The moon, from dusk until dawn, on the other side of the sky from the sunset
    float moon = smooth(0.5f, 0.85f, darkness) * clear;
    if (moon > 0.01f) {
        float nh = fmodf(hourNow - 18.0f + 24.0f, 24.0f);      // hours since 18:00
        float a = nh / 13.0f * PI;
        if (a > 0 && a < PI) {
            Vector3 d = v3(cosf(a), 0.06f + 0.4f * sinf(a), 0.6f);
            float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
            if (sky_to_screen(v3(d.x / len, d.y / len, d.z / len), cam, &s)) {
                unsigned char m = (unsigned char)(255 * moon);
                soft_glow(s, 60 * px, (Color){ 200, 210, 255, 255 }, 0.2f * moon);
                DrawCircleV(s, 15 * px, (Color){ 240, 240, 225, m });
                DrawCircleV((Vector2){ s.x - 4 * px, s.y - 3 * px }, 3 * px, (Color){ 210, 210, 200, m });   // craters
                DrawCircleV((Vector2){ s.x + 5 * px, s.y + 4 * px }, 2 * px, (Color){ 210, 210, 200, m });
            }
        }
    }
}

// Inside the hospital building? (No rain falls there.)
static bool indoors(float x, float z)
{
    return x > -16.8f && x < 8.4f && z > -13.3f && z < 7.8f;   // (the floors upstairs overhang A&E's front)
}

// Puddles outside: on the grass, the path, the road and round the helipad.
typedef struct { float x, z, rx, rz; } Puddle;
static const Puddle PUDDLES[] = {
    { 10.2f, 11.0f, 0.9f, 0.5f }, { 11.8f, 15.5f, 0.7f, 0.45f }, {  9.4f, 18.0f, 1.1f, 0.6f },
    { 15.2f,  9.0f, 1.0f, 0.5f }, { 17.0f, 13.0f, 0.8f, 0.5f }, { 15.6f, -4.0f, 1.2f, 0.6f },
    { 16.8f, -9.0f, 0.9f, 0.5f }, { 12.8f, -5.6f, 0.8f, 0.4f }, {  9.2f, -11.4f, 1.0f, 0.55f },
    { -3.0f,  9.0f, 1.2f, 0.6f }, { -12.0f, 9.3f, 0.9f, 0.5f }, { -18.0f, -4.0f, 0.7f, 0.9f },
    {  2.0f, -14.6f, 1.1f, 0.5f }, { 11.0f,  2.0f, 0.8f, 0.5f },
};
#define PUDDLE_COUNT ((int)(sizeof PUDDLES / sizeof PUDDLES[0]))

static void draw_puddles(void)
{
    if (wetness < 0.02f) return;
    Color water = shade((Color){ 110, 125, 150, 255 }, 0.9f);
    water.a = (unsigned char)(170 * wetness);
    for (int i = 0; i < PUDDLE_COUNT; i++) {
        const Puddle *p = &PUDDLES[i];
        float s = 0.4f + 0.6f * wetness;                       // they shrink as they dry
        const int slices = 18;
        for (int k = 0; k < slices; k++) {
            float a0 = k * 2 * PI / slices, a1 = (k + 1) * 2 * PI / slices;
            DrawTriangle3D(v3(p->x, 0.012f, p->z),
                           v3(p->x + cosf(a1) * p->rx * s, 0.012f, p->z + sinf(a1) * p->rz * s),
                           v3(p->x + cosf(a0) * p->rx * s, 0.012f, p->z + sinf(a0) * p->rz * s), water);
        }
        // Rings spreading out where the raindrops land
        if (rain > 0.05f) {
            for (int r = 0; r < 2; r++) {
                float t = fmodf((float)GetTime() * 0.9f + i * 0.37f + r * 0.5f, 1.0f);
                Color ring = { 220, 230, 245, (unsigned char)(120 * rain * (1.0f - t)) };
                DrawCircle3D(v3(p->x + (r ? 0.2f : -0.25f) * p->rx, 0.02f, p->z + (r ? -0.1f : 0.1f)),
                             0.05f + t * 0.35f, v3(1, 0, 0), 90.0f, ring);
            }
        }
    }
}

// Falling rain, everywhere outside within sight of the camera.
static void draw_rain(Camera3D cam)
{
    if (rain < 0.02f) return;
    int drops = (int)(1400 * rain);
    float fall = 15.0f, top = 12.0f, size = 44.0f;
    Color streak = { 200, 212, 230, (unsigned char)(110 + 60 * rain) };
    Vector3 c = cam.target;
    for (int i = 0; i < drops; i++) {
        unsigned int h = scramble((unsigned int)i * 2654435761u + 17u);
        // Each drop has its own spot in a big square tile of rain; the tiles
        // repeat across the world, so the rain doesn't slide when the camera moves
        float ox = (h % 1000) / 1000.0f * size, oz = ((h >> 10) % 1000) / 1000.0f * size;
        float x = c.x - size / 2 + fmodf(ox - (c.x - size / 2) + size * 100.0f, size);
        float z = c.z - size / 2 + fmodf(oz - (c.z - size / 2) + size * 100.0f, size);
        if (indoors(x, z)) continue;
        float y = top - fmodf((float)GetTime() * fall + ((h >> 20) % 1000) / 1000.0f * top, top);
        DrawLine3D(v3(x, y, z), v3(x + 0.07f, y + 0.6f, z + 0.03f), streak);
    }
}

// Street lamps along the road and the path to the doors. They come on at dusk.
static const float LAMPS[][2] = {
    { 19.2f, -10.0f }, { 19.2f, -3.0f }, { 19.2f, 4.0f }, { 19.2f, 11.0f }, { 19.2f, 18.0f },
    { 13.55f, 10.5f }, { 13.55f, 18.6f },
};
#define LAMP_COUNT ((int)(sizeof LAMPS / sizeof LAMPS[0]))

static bool lamps_on(void) { return darkness > 0.35f; }

// For the other 3D files: the light, the clock and the weather
float view3d_darkness(void) { return darkness; }
bool  view3d_lamps_on(void) { return lamps_on(); }
float view3d_hour(void)     { return hourNow; }
float view3d_rain(void)     { return rain; }

static void draw_lamps(void)
{
    for (int i = 0; i < LAMP_COUNT; i++) {
        float x = LAMPS[i][0], z = LAMPS[i][1];
        float armX = (x > 18.0f) ? -0.45f : -0.3f;             // the arm leans out over the road
        draw_box(v3(x, 1.7f, z), v3(0.09f, 3.4f, 0.09f), C_DARK);
        draw_box(v3(x + armX / 2, 3.38f, z), v3(fabsf(armX), 0.07f, 0.07f), C_DARK);
        if (lamps_on()) draw_lit_box(v3(x + armX, 3.3f, z), v3(0.34f, 0.12f, 0.22f), (Color){ 255, 225, 150, 255 });
        else            draw_box(v3(x + armX, 3.3f, z), v3(0.34f, 0.12f, 0.22f), C_STEEL);
    }
}

static void lamp_glows(void)
{
    if (!lamps_on()) return;
    unsigned char a = (unsigned char)(80 * smooth(0.35f, 0.8f, darkness));
    for (int i = 0; i < LAMP_COUNT; i++) {
        float armX = (LAMPS[i][0] > 18.0f) ? -0.45f : -0.3f;
        glow(LAMPS[i][0] + armX, LAMPS[i][1], 2.6f, (Color){ 255, 210, 140, a });
    }
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
    float wasDark = darkness, wasSun = sunset;
    night = false; darkness = 0; sunset = 0;                                                                // screens glow
    quad(v3(x - sx, y0, fz), v3(x + sx, y0, fz), v3(x + sx, y1, fz), v3(x - sx, y1, fz), screen);
    night = wasNight; darkness = wasDark; sunset = wasSun;
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

// ---- Equipment ----

// Like part(), but for things that glow (screens and lights)
static void part_lit(float lx, float y, float lz, float sx, float sy, float sz, Color c)
{
    float wx, wz, wsx = sx, wsz = sz;
    switch (partFace) {
        case FACE_SOUTH: wx =  lx; wz =  lz;                     break;
        case FACE_NORTH: wx = -lx; wz = -lz;                     break;
        case FACE_EAST:  wx =  lz; wz = -lx; wsx = sz; wsz = sx; break;
        default:         wx = -lz; wz =  lx; wsx = sz; wsz = sx; break;
    }
    draw_lit_box(v3(partBase.x + wx, y, partBase.z + wz), v3(wsx, sy, wsz), c);
}

void draw_equipment_item(int e, Vector3 p, Facing f)
{
    partBase = p;
    partFace = f;
    Color kit = draw_equipment_colour(e);
    switch (e) {
        case EQUIP_MONITOR:          // a heart monitor on a wheeled stand
            shadow(p.x, p.z, 0.4f, 0.4f);
            part(0, 0.04f, 0, 0.40f, 0.05f, 0.40f, C_STEEL);
            part(0, 0.60f, 0, 0.06f, 1.10f, 0.06f, C_STEEL);
            part(0, 1.28f, 0, 0.46f, 0.34f, 0.10f, C_DARK);
            part_lit(0, 1.28f, 0.055f, 0.38f, 0.26f, 0.01f, (Color){ 12, 30, 22, 255 });
            part_lit(0, 1.28f, 0.062f, 0.30f, 0.02f, 0.01f, (Color){ 80, 255, 140, 255 });
            part(0, 1.47f, 0, 0.46f, 0.04f, 0.10f, kit);
            break;
        case EQUIP_VENTILATOR:       // a white box on wheels, with a screen and a breathing hose
            shadow(p.x, p.z, 0.55f, 0.5f);
            part(0, 0.10f, 0, 0.50f, 0.08f, 0.44f, C_STEEL);
            for (int i = 0; i < 4; i++) part((i % 2) ? 0.2f : -0.2f, 0.04f, (i < 2) ? 0.17f : -0.17f, 0.06f, 0.08f, 0.06f, C_DARK);
            part(0, 0.58f, 0, 0.42f, 0.80f, 0.36f, C_CARD);
            part(0, 0.99f, 0, 0.44f, 0.04f, 0.38f, kit);
            part(0, 1.18f, 0.02f, 0.36f, 0.30f, 0.06f, C_DARK);
            part_lit(0, 1.18f, 0.055f, 0.30f, 0.24f, 0.01f, (Color){ 60, 120, 210, 255 });
            for (int i = 0; i < 5; i++) part(0.24f + 0.04f * i, 0.75f - 0.06f * i, 0.12f + 0.05f * i, 0.07f, 0.07f, 0.07f, (Color){ 170, 210, 240, 255 });
            break;
        case EQUIP_DEFIB:            // a red trolley with the defibrillator on top
            shadow(p.x, p.z, 0.5f, 0.45f);
            part(0, 0.43f, 0, 0.48f, 0.76f, 0.40f, (Color){ 210, 50, 50, 255 });
            for (int i = 0; i < 3; i++) part(0, 0.22f + i * 0.2f, 0.205f, 0.40f, 0.02f, 0.01f, C_DARK);   // drawers
            part(0, 0.88f, 0, 0.38f, 0.16f, 0.30f, (Color){ 250, 205, 45, 255 });
            part(-0.10f, 0.98f, 0.04f, 0.10f, 0.05f, 0.14f, C_DARK);                               // paddles
            part( 0.10f, 0.98f, 0.04f, 0.10f, 0.05f, 0.14f, C_DARK);
            part_lit(0.12f, 0.92f, 0.155f, 0.06f, 0.04f, 0.01f, (Color){ 90, 230, 120, 255 });
            break;
        case EQUIP_DRIP:             // a drip stand with its pump and a bag of fluid
            part(0, 0.03f, 0, 0.40f, 0.04f, 0.06f, C_STEEL);
            part(0, 0.03f, 0, 0.06f, 0.04f, 0.40f, C_STEEL);
            part(0, 0.98f, 0, 0.04f, 1.95f, 0.04f, C_STEEL);
            part(0, 1.94f, 0, 0.30f, 0.03f, 0.03f, C_STEEL);
            part(0.11f, 1.74f, 0, 0.15f, 0.25f, 0.05f, (Color){ 205, 230, 245, 255 });
            part(0, 1.10f, 0.08f, 0.22f, 0.26f, 0.14f, kit);
            part_lit(0, 1.14f, 0.152f, 0.14f, 0.08f, 0.01f, (Color){ 120, 230, 160, 255 });
            break;
        default:                     // a wheelchair
            shadow(p.x, p.z, 0.65f, 0.75f);
            part(0, 0.50f, 0.02f, 0.46f, 0.06f, 0.44f, kit);                       // seat
            part(0, 0.80f, -0.22f, 0.46f, 0.52f, 0.05f, kit);                      // back
            for (int s = -1; s <= 1; s += 2) {
                part(s * 0.28f, 0.33f, -0.02f, 0.04f, 0.62f, 0.36f, C_DARK);       // big wheels (two
                part(s * 0.28f, 0.33f, -0.02f, 0.04f, 0.36f, 0.62f, C_DARK);       //  crossed boxes)
                part(s * 0.31f, 0.33f, -0.02f, 0.02f, 0.50f, 0.50f, C_STEEL);      // the hand rims
                part(s * 0.18f, 0.06f, 0.30f, 0.05f, 0.12f, 0.08f, C_DARK);        // little front wheels
                part(s * 0.22f, 0.70f, 0.04f, 0.04f, 0.04f, 0.40f, C_STEEL);       // armrests
                part(s * 0.20f, 1.02f, -0.30f, 0.04f, 0.04f, 0.16f, C_DARK);       // handles
            }
            part(0, 0.12f, 0.38f, 0.40f, 0.03f, 0.14f, C_STEEL);                   // footrest
            break;
    }
}

// The equipment a patient in an A&E bed has with them, drawn round the bed
// (as if the bed were at x = 0 on the back wall, like draw_bed).
static void draw_bed_equipment(const Patient *p)
{
    unsigned held = p ? p->equipHeld : 0;
    if (held & EQUIP_BIT(EQUIP_MONITOR)) draw_monitor(0.75f, BED_Z - 0.85f, p);
    if (held & EQUIP_BIT(EQUIP_DRIP)) {
        draw_drip(-0.75f, BED_Z - 0.60f, 0, true);
        draw_box(v3(-0.75f, 1.10f, BED_Z - 0.52f), v3(0.22f, 0.26f, 0.14f), draw_equipment_colour(EQUIP_DRIP));   // the pump
    }
    if (held & EQUIP_BIT(EQUIP_VENTILATOR)) draw_equipment_item(EQUIP_VENTILATOR, v3(-0.82f, 0, BED_Z + 0.35f), FACE_EAST);
    if (held & EQUIP_BIT(EQUIP_DEFIB))      draw_equipment_item(EQUIP_DEFIB, v3(0.82f, 0, BED_Z + 0.55f), FACE_WEST);
}

// The equipment bay in A&E, between the side door and the waiting room:
// everything that's free is parked here, ready to go (up to three of each)
static void draw_equipment_bay(const Sim *sim)
{
    float x0 = -7.85f, x1 = -3.85f, z0 = -0.45f, z1 = 0.88f;
    // yellow lines round it, and its name on the floor
    Color yel = shade((Color){ 240, 200, 40, 255 }, 1.0f);
    quad(v3(x0, 0.012f, z0), v3(x1, 0.012f, z0), v3(x1, 0.012f, z0 + 0.06f), v3(x0, 0.012f, z0 + 0.06f), yel);
    quad(v3(x0, 0.012f, z1 - 0.06f), v3(x1, 0.012f, z1 - 0.06f), v3(x1, 0.012f, z1), v3(x0, 0.012f, z1), yel);
    quad(v3(x0, 0.012f, z0), v3(x0 + 0.06f, 0.012f, z0), v3(x0 + 0.06f, 0.012f, z1), v3(x0, 0.012f, z1), yel);
    quad(v3(x1 - 0.06f, 0.012f, z0), v3(x1, 0.012f, z0), v3(x1, 0.012f, z1), v3(x1 - 0.06f, 0.012f, z1), yel);
    sign_quad(signEquipment.texture, v3(x0 + 0.4f, 0.013f, z1 - 0.32f), v3(x1 - 0.4f, 0.013f, z1 - 0.32f),
                                     v3(x1 - 0.4f, 0.013f, z1 - 0.08f), v3(x0 + 0.4f, 0.013f, z1 - 0.08f));
    for (int e = 0; e < EQUIP_TYPES; e++) {
        int free = sim->equipment[e].total - sim->equipment[e].inUse;
        float cx = x0 + 0.4f + e * 0.8f;
        Vector3 spots[3] = { v3(cx - 0.19f, 0, -0.2f), v3(cx + 0.19f, 0, -0.2f), v3(cx, 0, 0.28f) };
        for (int k = 0; k < free && k < 3; k++) draw_equipment_item(e, spots[k], FACE_SOUTH);
    }
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
            DrawText((i == RESUS_BED) ? "RESUS" : TextFormat("BED %d", i + 1), 10, 7, 20, WHITE);
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
            if (p->walkLeft > 0.0f)             { who = "On the way to the bed"; whoCol = (Color){ 170, 185, 210, 255 }; }
            else if (p->doneInAE)               { who = "Waiting for a wheelchair"; whoCol = (Color){ 255, 190, 110, 255 }; }
            else if (p->equipMissing) {
                int e = 0;
                while (e < EQUIP_TYPES - 1 && !(p->equipMissing & EQUIP_BIT(e))) e++;
                who = TextFormat("Waiting: %s", sim_equipment_short(e));
                whoCol = (Color){ 255, 120, 120, 255 };
            }
            else if (d != -1)                     who = TextFormat("Dr %s", sim->doctors[d].name);
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

}

// Outside, on the right: tarmac with two ambulance bays, the road the
// ambulances come and go on, and the helipad further back. (Drawn from every
// floor's view, unlike A&E's floor.)
static void draw_outside_ground(void)
{
    Color tar = shade(C_ASPHALT, 1.0f);
    quad(v3(8.2f, -0.005f, -13.0f), v3(ROAD_X1, -0.005f, -13.0f), v3(ROAD_X1, -0.005f, 8.4f), v3(8.2f, -0.005f, 8.4f), tar);
    quad(v3(ROAD_X0, -0.005f, 8.4f), v3(ROAD_X1, -0.005f, 8.4f), v3(ROAD_X1, -0.005f, ROAD_END), v3(ROAD_X0, -0.005f, ROAD_END), tar);
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
        quad(v3(ROAD_MID - 0.05f, 0.01f, z), v3(ROAD_MID + 0.05f, 0.01f, z), v3(ROAD_MID + 0.05f, 0.01f, z + 0.8f), v3(ROAD_MID - 0.05f, 0.01f, z + 0.8f), white);
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

// A tree: a trunk and a few blocks of leaves. `k` varies the size and shade.
static void draw_tree(float x, float z, int k)
{
    float h = 1.6f + (k % 3) * 0.35f;
    Color leaf = (k % 2) ? (Color){ 70, 150, 80, 255 } : (Color){ 90, 165, 85, 255 };
    shadow(x, z, 1.6f, 1.6f);
    draw_box(v3(x, h / 2, z), v3(0.28f, h, 0.28f), (Color){ 120, 85, 55, 255 });
    draw_box(v3(x, h + 0.45f, z), v3(1.5f, 1.0f, 1.5f), leaf);
    draw_box(v3(x + 0.15f, h + 1.15f, z - 0.1f), v3(1.0f, 0.7f, 1.0f), shade(leaf, 1.08f));
    draw_box(v3(x - 0.35f, h + 0.25f, z + 0.3f), v3(0.8f, 0.6f, 0.8f), shade(leaf, 0.92f));
}

// Grass all round the hospital, with trees along the edges, so it isn't
// floating in the sky.
static void draw_grounds(void)
{
    Color grass = shade((Color){ 150, 192, 125, 255 }, 1.0f - 0.18f * wetness);   // darker when wet
    quad(v3(-45.0f, -0.02f, -40.0f), v3(50.0f, -0.02f, -40.0f), v3(50.0f, -0.02f, 40.0f), v3(-45.0f, -0.02f, 40.0f), grass);
    // A path from the street to the front doors
    Color path = shade((Color){ 205, 200, 190, 255 }, 1.0f);
    quad(v3(8.2f, -0.01f, 8.4f), v3(13.2f, -0.01f, 8.4f), v3(13.2f, -0.01f, ROAD_END), v3(8.2f, -0.01f, ROAD_END), path);

    int k = 0;
    for (float z = -15.0f; z <= 10.0f; z += 3.6f) draw_tree(-19.5f, z, k++);      // left
    for (float x = -16.0f; x <= 6.0f; x += 3.7f)  draw_tree(x, 10.6f, k++);       // front
    for (float x = -15.0f; x <= 14.0f; x += 4.0f) draw_tree(x, -16.0f, k++);      // back
    for (float z = -14.0f; z <= 18.0f; z += 4.0f) draw_tree(20.4f, z, k++);       // right, past the road
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

// A rescue helicopter with spinning rotors, in its service's colours:
// coast guard blue and white, search and rescue white and red, fire service
// orange and yellow. `pos` is the bottom of the skids; at angle 0 the nose
// points along +x.
static void draw_helicopter(Vector3 pos, float angle, int service)
{
    Color body[SERVICE_COUNT]   = { {  40,  90, 190, 255 }, { 240, 242, 246, 255 }, { 225,  90,  25, 255 } };
    Color stripe[SERVICE_COUNT] = { { 248, 248, 250, 255 }, { 210,  45,  45, 255 }, { 250, 215,  60, 255 } };
    Color red = body[service], band = stripe[service], glass = { 50, 70, 100, 255 };
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
    draw_box(v3(0.1f, 0.95f, 0), v3(2.62f, 0.20f, 1.37f), band);
    draw_box(v3(1.45f, 1.05f, 0), v3(0.3f, 0.75f, 1.15f), glass);                    // nose window
    draw_box(v3(0.75f, 1.30f, 0), v3(0.7f, 0.45f, 1.37f), glass);                    // side windows
    draw_box(v3(-0.3f, 1.70f, 0), v3(1.4f, 0.35f, 0.9f), band);                      // engine on top
    draw_box(v3(-2.2f, 1.30f, 0), v3(2.6f, 0.30f, 0.30f), red);                      // tail boom
    draw_box(v3(-3.4f, 1.75f, 0), v3(0.45f, 0.75f, 0.10f), red);                     // tail fin
    draw_box(v3(-2.6f, 1.30f, 0), v3(0.25f, 0.30f, 0.32f), band);                    // band round the tail
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
        draw_box(v3(0, 2.66f, wz), v3(2.7f, 0.62f, 0.04f), C_BRAND);
        sign_facing_front(signWard.texture, 0, 2.66f, wz + 0.025f, 2.6f, 0.54f);

        draw_box(v3(-4.0f, 2.6f, wz), v3(0.66f, 0.66f, 0.04f), C_CARD);              // red cross
        draw_box(v3(-4.0f, 2.6f, wz + 0.03f), v3(0.50f, 0.16f, 0.03f), C_EMERGENCY);
        draw_box(v3(-4.0f, 2.6f, wz + 0.03f), v3(0.16f, 0.50f, 0.03f), C_EMERGENCY);

        for (int i = 0; i < WALL_BEDS; i++) {
            // The bed's screen: a dark case with the picture from update_bed_screens()
            draw_box(v3(bed_x(i), 1.95f, wz + 0.03f), v3(0.98f, 0.58f, 0.06f), C_DARK);
            bool wasNight = night;
    float wasDark = darkness, wasSun = sunset;
            night = false; darkness = 0; sunset = 0;                                                        // screens glow
            sign_facing_front(bedScreen[i].texture, bed_x(i), 1.95f, wz + 0.065f, 0.9f, 0.506f);
            night = wasNight; darkness = wasDark; sunset = wasSun;

            bool on = (night || lamps_on()) && sim->beds[i].occupant != NULL;
            Color lamp = on ? (Color){ 255, 225, 160, 255 } : (Color){ 225, 228, 235, 255 };
            if (on) draw_lit_box(v3(bed_x(i), 1.55f, wz + 0.04f), v3(0.6f, 0.07f, 0.08f), lamp);
            else    draw_box    (v3(bed_x(i), 1.55f, wz + 0.04f), v3(0.6f, 0.07f, 0.08f), lamp);
        }
    }

    // The resus bay: its screen on a stand at the head of the bed (the wall
    // there is low), facing into the room, and a RESUS sign above it
    {
        float sx = RESUS_X + 1.25f, sz = RESUS_Z;
        draw_box(v3(sx + 0.05f, 0.85f, sz), v3(0.08f, 1.7f, 0.08f), C_STEEL);            // stand
        draw_box(v3(sx, 1.85f, sz), v3(0.06f, 0.58f, 0.98f), C_DARK);
        bool wasNight = night;
    float wasDark = darkness, wasSun = sunset;
        night = false; darkness = 0; sunset = 0;
        sign_quad(bedScreen[RESUS_BED].texture, v3(sx - 0.035f, 2.103f, sz - 0.45f), v3(sx - 0.035f, 2.103f, sz + 0.45f),
                                               v3(sx - 0.035f, 1.597f, sz + 0.45f), v3(sx - 0.035f, 1.597f, sz - 0.45f));
        night = wasNight; darkness = wasDark; sunset = wasSun;
        draw_box(v3(sx, 2.35f, sz), v3(0.06f, 0.3f, 0.98f), C_EMERGENCY);                // red band: resus
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
    float wasDark = darkness, wasSun = sunset;
        night = false; darkness = 0; sunset = 0;                                                            // the TV glows
        sign_quad(tvScreen.texture, v3(wx + 0.05f, 2.06f, 5.45f), v3(wx + 0.05f, 2.06f, 4.35f),
                                    v3(wx + 0.05f, 1.44f, 4.35f), v3(wx + 0.05f, 1.44f, 5.45f));
        night = wasNight; darkness = wasDark; sunset = wasSun;
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

    // The front wall, with the hospital's main entrance in it (outside3d.c
    // draws the doors, which open for the visitors from the car park)
    draw_wall(v3(-6.0f, 0, AE_FRONT_Z), 4.2f, 0.2f, WALL_SIDE_H, lowFront);
    draw_wall(v3(3.0f, 0, AE_FRONT_Z), 10.2f, 0.2f, WALL_SIDE_H, lowFront);
    outside3d_draw_main_doors();
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
        case AT_EXIT:     return v3(9.3f, 0, 7.6f);                  // the end of the way out
        case AT_INBOUND:  return v3(HELI_X - 1.0f, 0, HELI_Z + 0.6f);   // (in the helicopter)
        case AT_BED:      return bed_point(pl.a, (pl.a == RESUS_BED) ? -0.9f : 0.9f, 0, 0.4f);   // by the bed, on the corridor side
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
    float x = (pl.kind == AT_BED) ? place_stand(pl).x : -1.9f;
    pts[n++] = v3(-7.6f, 0, -0.9f);                     // just inside A&E's side door
    pts[n++] = v3(x, 0, -1.25f);                        // along in front of the beds
    pts[n++] = place_stand(pl);
    return n;
}

static bool same_point(Vector3 a, Vector3 b)
{
    return fabsf(a.x - b.x) < 0.01f && fabsf(a.y - b.y) < 0.01f && fabsf(a.z - b.z) < 0.01f;
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
    Vector3 stand = place_stand((Place){ AT_BED, bed, 0 });
    float wardGap = (stand.x < 0) ? -1.9f : 1.9f;
    add_point(t, v3(gapX, 0, from.z));
    add_point(t, v3(gapX, 0, 1.6f));
    add_point(t, v3(wardGap, 0, 1.6f));
    add_point(t, v3(wardGap, 0, -1.45f));
    add_point(t, v3(stand.x, 0, -1.45f));
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
    add_point(t, v3(9.3f, 0, 7.6f));                     // off down the path
}

// Set off along the route. `duration` is how long the simulation gives them
// to get there (seconds at 1x), or 0 to walk at a normal pace.
static void start_walk(Tracked *t, float duration)
{
    t->walking = true;
    t->next = 0;
    // How far is it?
    float length = 0.0f;
    Vector3 from = t->pos;
    for (int i = 0; i < t->count; i++) {
        float dx = t->points[i].x - from.x, dy = t->points[i].y - from.y, dz = t->points[i].z - from.z;
        length += sqrtf(dx * dx + dy * dy + dz * dz);       // (up or down in the lift counts too)
        from = t->points[i];
    }
    if (duration > 0.0f) t->walkSpeed = fminf(fmaxf(WALK_PACE, length / duration), WALK_HURRY);
    else                 t->walkSpeed = fminf(fmaxf(WALK_PACE, length / WALK_MAX_SECONDS), WALK_FAST);
}

static void walk_home(Tracked *t, float duration);

// Plan a walk from where they are now (their old place) to `to`.
static void walk_to(Tracked *t, Place to, float duration)
{
    if (to.kind == AT_EXIT) {                            // pushed out to the front door, then home
        walk_home(t, duration);
        t->place = to;
        return;
    }
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
    start_walk(t, duration);
}

// They've left the simulation (gone home): walk out of the hospital.
static void walk_home(Tracked *t, float duration)
{
    Place from = t->place;
    t->count = 0;
    if (from.kind == AT_BED) {
        add_point(t, v3(place_stand(from).x, 0, -1.45f));
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
    start_walk(t, duration);
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
    Vector3 lane0 = v3(LANE_IN, 0, ROAD_END), turn = v3(LANE_IN, 0, bz - 2.6f);
    Vector3 corner = v3(LANE_IN, 0, bz), parked = v3(AMBULANCE_X, 0, bz);
    switch (v->state) {
        case V_ARRIVING:
            if (v->t < AMB_DRIVE_IN) {                         // up the road, past the bay
                float k2 = ease(v->t / AMB_DRIVE_IN);
                *pos = v3(LANE_IN, 0, lane0.z + (turn.z - lane0.z) * k2);
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
                // Out across the incoming lane and round into the outgoing one
                Vector3 bend = v3(LANE_OUT, 0, bz), out = v3(LANE_OUT, 0, bz + 2.6f);
                Vector3 p = curve(parked, bend, out, k2), q = curve(parked, bend, out, fminf(k2 + 0.01f, 1));
                *pos = p;
                *angle = heading(q.x - p.x, q.z - p.z);
            } else {
                float k2 = ease((v->t - AMB_TURN_OUT) / AMB_DRIVE_OUT);
                *pos = v3(LANE_OUT, 0, (bz + 2.6f) + (ROAD_END + 4.0f - (bz + 2.6f)) * k2);
                *angle = -90.0f;                               // cab pointing down the road (+z)
            }
        }
    }
}

// Where helicopter k is (the bottom of its skids) and which way it faces.
static void helicopter_place(int k, Vector3 *pos, float *angle)
{
    const Vehicle *v = &vehicles[k];
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
static float vehicle_arrive_time(int k) { return is_heli(k) ? HELI_APPROACH + HELI_DESCEND : AMB_DRIVE_IN + AMB_REVERSE; }
static float vehicle_unload_time(int k) { return is_heli(k) ? HELI_UNLOAD : AMB_UNLOAD; }
static float vehicle_leave_time(int k)  { return is_heli(k) ? HELI_CLIMB + HELI_DEPART : AMB_TURN_OUT + AMB_DRIVE_OUT; }

// Traffic rules for the two-lane road, so ambulances never drive through each other.
// Is another ambulance (not k) coming up the road or reversing into a bay?
static bool ambulance_coming_in(int k)
{
    for (int j = 0; j < AMB_BAYS; j++) {
        if (j != k && vehicles[j].state == V_ARRIVING) return true;
    }
    return false;
}

// Is another ambulance (not k) on its way out?
static bool ambulance_leaving(int k)
{
    for (int j = 0; j < AMB_BAYS; j++) {
        if (j != k && vehicles[j].state == V_LEAVING) return true;
    }
    return false;
}

// Is another ambulance (not k) pulling out of its bay across the road? (Once
// it's straightened up in the outgoing lane it's out of the way.)
static bool ambulance_pulling_out(int k)
{
    for (int j = 0; j < AMB_BAYS; j++) {
        if (j != k && vehicles[j].state == V_LEAVING && vehicles[j].t < AMB_TURN_OUT + 1.0f) return true;
    }
    return false;
}

// Is the helipad free for another helicopter to come in? (Not while one is
// landing or on the pad; once it's taking off the next can start its approach.)
static bool pad_free(void)
{
    for (int k = HELI_FIRST; k < VEHICLES; k++) {
        if (vehicles[k].state == V_ARRIVING || vehicles[k].state == V_UNLOADING) return false;
    }
    return true;
}

// The rescue patient who has waited longest for a helicopter, or NULL.
static Tracked *next_heli_passenger(void)
{
    Tracked *best = NULL;
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (t->used && t->vehicle == HELI_WAITING && (best == NULL || t->id < best->id)) best = t;
    }
    return best;
}

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
    for (int k = 0; k < VEHICLES; k++) {
        Vehicle *v = &vehicles[k];
        if (v->state == V_IDLE) {
            if (!is_heli(k)) {
                // Someone to bring in: set off, once no other ambulance is coming
                // in (one at a time up the road and into the bays) or pulling out
                if (vehicle_has_passenger(k) && !ambulance_coming_in(k) && !ambulance_pulling_out(k)) {
                    v->state = V_ARRIVING;
                    v->t = 0;
                }
            } else if (pad_free()) {
                Tracked *t = next_heli_passenger();      // one rescue patient per flight
                if (t != NULL) {
                    t->vehicle = k;
                    v->service = t->service;             // their service's helicopter and colours
                    v->state = V_ARRIVING;
                    v->t = 0;
                    heliCut = true;                      // the tour cuts to the helipad
                }
            }
            continue;
        }
        v->t += dt;
        if (v->state == V_ARRIVING && v->t >= vehicle_arrive_time(k)) { v->state = V_UNLOADING; v->t = 0; }
        else if (v->state == V_UNLOADING && v->t >= vehicle_unload_time(k)) {
            // Pulling out crosses the incoming lane, so wait while another ambulance
            // is coming in, or still pulling out itself (one at a time)
            // (a helicopter waits until its patient is out: the simulation says when it's landed)
            if (is_heli(k) ? !vehicle_has_passenger(k) : (!ambulance_coming_in(k) && !ambulance_leaving(k))) { v->state = V_LEAVING; v->t = 0; }
        }
        else if (v->state == V_LEAVING && v->t >= vehicle_leave_time(k)) { v->state = V_IDLE; v->t = 0; }
    }
}

// Get waiting passengers out once their vehicle has stopped.
static void unload_passengers(void)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || t->vehicle < 0 || t->vehicle == HELI_WAITING) continue;
        if (vehicles[t->vehicle].state != V_UNLOADING) continue;
        if (t->place.kind == AT_INBOUND) continue;     // not landed yet in the simulation: a moment more
        t->count = 0;
        if (is_heli(t->vehicle)) {
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
        start_walk(t, 0.0f);
    }
}

// A new patient: walk in (or ride in) to their first place.
static void arrive(Tracked *t, const Patient *p)
{
    t->count = 0;
    t->vehicle = -1;
    if (p->rescue) {
        t->vehicle = HELI_WAITING;            // waits for a helicopter to bring them
    } else if (p->severity == 1) {
        for (int k = 0; k < AMB_BAYS; k++) {  // a free ambulance brings them in
            if (vehicles[k].state == V_IDLE && !vehicle_has_passenger(k)) { t->vehicle = k; break; }
        }
    }
    t->walking = true;                        // their place stays empty until they get there
    if (t->vehicle >= 0) return;
    t->pos = v3(9.3f, 0, 6.6f);               // walking in from the path
    add_point(t, v3(9.0f, 0, 3.5f));
    route_from_doors(t, t->place);
    start_walk(t, (t->place.kind == AT_BED) ? p->walkLeft : 0.0f);
}

// Check every patient's place, and set off anyone who needs to move.
static void track_patient(const Patient *p, Place pl, bool fresh, bool resync)
{
    Tracked *t = find_tracked(p->id);
    if (t == NULL && pl.kind == AT_EXIT) return;   // already out of sight (or not seen until now)
    if (t == NULL) {
        for (int i = 0; i < MAX_TRACKED && t == NULL; i++) if (!tracked[i].used) t = &tracked[i];
        if (t == NULL) return;                // table full: they just appear
        *t = (Tracked){ 0 };
        t->used = true;
        t->id = p->id;
        t->place = pl;
        t->gown = severity_color(p->severity);
        t->rescue = p->rescue;
        t->service = p->rescueService;
        t->vehicle = -1;
        t->pos = place_stand(pl);
        t->seen = true;
        if ((fresh && !resync) || pl.kind == AT_INBOUND) arrive(t, p);   // (a rescue in the air waits for a helicopter)
        return;
    }
    t->seen = true;
    t->chair = (p->equipHeld & EQUIP_BIT(EQUIP_WHEELCHAIR)) != 0;
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
    walk_to(t, pl, (pl.kind == AT_BED || pl.kind == AT_ROOM || pl.kind == AT_QUEUE || pl.kind == AT_EXIT) ? p->walkLeft : 0.0f);
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
        for (int k = 0; k < VEHICLES; k++) vehicles[k] = (Vehicle){ V_IDLE, 0, 0 };
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
    for (const Patient *p = sim->leaving; p != NULL; p = p->next) {
        track_patient(p, (Place){ AT_EXIT, 0, 0 }, false, resync);
    }
    for (const Patient *p = sim->inbound; p != NULL; p = p->next) {     // rescues in the air
        track_patient(p, (Place){ AT_INBOUND, 0, 0 }, p->id > lastSeenId, resync);
    }
    if (sim->nextId - 1 > lastSeenId) lastSeenId = sim->nextId - 1;

    // Anyone no longer in the simulation has gone home
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || t->seen || t->leaving) continue;
        if (t->vehicle >= 0) { t->used = false; continue; }      // left before getting out
        walk_home(t, 0.0f);
    }

    // Vehicles and people move at the simulation's speed, so they keep up
    // with it (and stand still while it's paused)
    float dt = GetFrameTime() * speed;
    update_vehicles(dt);
    unload_passengers();

    // Move everyone along
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        float pace = t->walkSpeed * dt;
        float left = pace;
        while (left > 0 && t->next < t->count) {
            Vector3 g = t->points[t->next];
            float dx = g.x - t->pos.x, dy = g.y - t->pos.y, dz = g.z - t->pos.z;
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d <= left) { t->pos = g; t->next++; left -= d; }
            else           { t->pos.x += dx / d * left; t->pos.y += dy / d * left; t->pos.z += dz / d * left; left = 0; }
        }
        t->phase += pace * 5.0f;
        if (t->next >= t->count) {
            t->walking = false;                                  // arrived
            if (t->leaving) t->used = false;                     // gone home
        }
    }

    // The lift: it goes wherever someone is riding in it, and its doors open
    // on a floor when someone is about to get in or out there
    bool riding = false;
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        if (fabsf(t->pos.x - LIFT_X) < 0.75f && t->pos.z > LIFT_DOOR_Z + 0.05f && !riding) {
            liftCarY = t->pos.y;
            riding = true;
        }
    }
    for (int f = 0; f < FLOORS; f++) {
        bool near = false;
        for (int i = 0; i < MAX_TRACKED && !near; i++) {
            Tracked *t = &tracked[i];
            if (!t->used || !t->walking || t->vehicle >= 0 || fabsf(t->pos.y - f * FLOOR_H) > 0.3f) continue;
            float dx = t->pos.x - LIFT_X, dz = t->pos.z - LIFT_DOOR_Z;
            if (dx * dx + dz * dz < 1.3f * 1.3f) near = true;
        }
        bool open = near && fabsf(liftCarY - f * FLOOR_H) < 0.3f;
        liftDoors[f] += ((open ? 1.0f : 0.0f) - liftDoors[f]) * (1.0f - expf(-6.0f * GetFrameTime()));
    }

    // Open the doors when anyone is near them
    bool someoneNear = false;
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        float dx = t->pos.x - DOOR_X, dz = t->pos.z - 3.5f;
        if (dx * dx + dz * dz < 2.2f * 2.2f && t->pos.y < 0.1f) someoneNear = true;
    }
    float k = 1.0f - expf(-8.0f * GetFrameTime());
    doorOpen += ((someoneNear ? 1.0f : 0.0f) - doorOpen) * k;
    // Ding-dong as the doors start to open (not more than once every 4 seconds)
    if (someoneNear && !doorsWereNear && GetTime() - lastChime > 4.0) {
        sound_play(SND_CHIME);
        lastChime = GetTime();
    }
    doorsWereNear = someoneNear;
}

// Which floor a height is on (0, 1 or 2).
static int floor_of(float y)
{
    int f = (int)floorf(y / FLOOR_H + 0.5f);
    return f < 0 ? 0 : (f >= FLOORS ? FLOORS - 1 : f);
}

// Inside the ground floor? (Its front is further back at A&E.)
static bool inside_ground_floor(Vector3 p)
{
    return p.x > BUILDING_X0 && p.x < BUILDING_X1 && p.z > BUILDING_Z0 && p.z < BUILDING_Z1 &&
           !(p.x > -8.1f && p.z > AE_FRONT_Z);
}

// Can someone at p be seen from the floor being looked at? They're on that
// floor, or outside on the ground.
static bool visible_from_view(Vector3 p)
{
    bool outsideGround = p.y < 0.1f && !inside_ground_floor(p);
    if (viewFloor >= VIEW_OUTSIDE) return outsideGround;
    return outsideGround || floor_of(p.y) == viewFloor;
}

// A patient in a wheelchair, and the porter pushing them. `f` is the way
// they're going.
static void draw_pushed(Vector3 p, Facing f, Color gown, float phase)
{
    draw_equipment_item(EQUIP_WHEELCHAIR, p, f);
    // sitting in it: the same pieces as someone in a chair
    partBase = p;
    partFace = f;
    part(0, 0.62f, 0.10f, 0.40f, 0.14f, 0.44f, C_TROUSERS);                  // thighs
    part(0, 0.34f, 0.32f, 0.38f, 0.40f, 0.14f, C_TROUSERS);                  // shins
    part(0, 0.95f, -0.08f, 0.46f, 0.55f, 0.28f, gown);                       // body
    part(-0.28f, 0.80f, 0.02f, 0.12f, 0.16f, 0.36f, gown);                   // arms on the armrests
    part( 0.28f, 0.80f, 0.02f, 0.12f, 0.16f, 0.36f, gown);
    part(0, 1.39f, -0.08f, 0.32f, 0.32f, 0.32f, C_SKIN);                     // head
    part(-0.07f, 1.42f, 0.085f, 0.05f, 0.05f, 0.01f, C_DARK);
    part( 0.07f, 1.42f, 0.085f, 0.05f, 0.05f, 0.01f, C_DARK);
    part(0, 1.57f, -0.10f, 0.34f, 0.08f, 0.34f, C_HAIR);
    // the porter, behind the chair
    Vector3 back = p;
    switch (f) {
        case FACE_SOUTH: back.z -= 0.85f; break;
        case FACE_NORTH: back.z += 0.85f; break;
        case FACE_EAST:  back.x -= 0.85f; break;
        default:         back.x += 0.85f; break;
    }
    draw_person(back, f, (Outfit){ (Color){ 40, 70, 110, 255 }, C_TROUSERS, false, false, false, false }, true, phase);
}

static void draw_walkers(void)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        Tracked *t = &tracked[i];
        if (!t->used || !t->walking || t->vehicle >= 0) continue;
        if (!visible_from_view(t->pos)) continue;
        Vector3 target = t->points[t->next < t->count ? t->next : t->count - 1];
        bool inLift = fabsf(target.x - t->pos.x) < 0.01f && fabsf(target.z - t->pos.z) < 0.01f && fabsf(target.y - t->pos.y) > 0.01f;
        Facing face = inLift ? FACE_NORTH : facing_for(t->pos, target);   // riding the lift: stand still, face the doors
        // Everything is drawn as if on the ground, then lifted to their floor
        rlPushMatrix();
        rlTranslatef(0, t->pos.y, 0);
        Vector3 at = v3(t->pos.x, 0, t->pos.z);
        if (t->chair) {
            draw_pushed(at, face, t->gown, t->phase);
        } else {
            draw_person(at, face, patient_outfit(t->gown), !inLift, t->phase);
            if (t->rescue) draw_box(v3(at.x, 0.85f, at.z), v3(0.52f, 0.5f, 0.34f), (Color){ 215, 220, 230, 255 });  // foil blanket
        }
        rlPopMatrix();
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
    for (int k = HELI_FIRST; k < VEHICLES; k++) {
        if (vehicles[k].state == V_IDLE) continue;
        Vector3 pos; float angle;
        helicopter_place(k, &pos, &angle);
        draw_helicopter(pos, angle, vehicles[k].service);
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
    for (int k = HELI_FIRST; k < VEHICLES; k++) {
        if (vehicles[k].state == V_IDLE) continue;
        Vector3 pos; float angle;
        helicopter_place(k, &pos, &angle);
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
    signEquipment = make_sign("EQUIPMENT BAY",   512, 80,  44, BLANK, C_HIVIS);
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
    UnloadRenderTexture(signEquipment);
    UnloadRenderTexture(tvScreen);
    for (int i = 0; i < BED_COUNT; i++) UnloadRenderTexture(bedScreen[i]);
    rooms3d_close();
    floors3d_close();
    outside3d_close();
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

// ---- Places to look at: the number keys, and the tour (T) ----
// The number keys change with the floor: on the ground floor they're the
// rooms round A&E (see rooms3d.h); upstairs they're that floor's rooms (see
// floors3d.h); outside they're spots round the building. T tours the whole
// hospital, floor by floor and then outside, gliding from place to place.
typedef struct {
    const char *name;
    Vector3 target;
    float yaw, pitch, dist;
} Spot;

// Extra shots for the ground floor's tour, before its rooms
static const Spot GROUND_TOUR[] = {
    { "A&E",            {  3.0f, 0.4f,  0.9f },   0.0f, 42.0f, 21.0f },
    { "the beds",       { -1.0f, 0.4f, -3.2f },  15.0f, 28.0f, 10.0f },
    { "equipment bay",  { -5.8f, 0.4f,  0.2f },  10.0f, 35.0f,  8.0f },
    { "waiting room",   {  2.5f, 0.4f,  4.0f }, -35.0f, 32.0f, 11.0f },
    { "ambulance bays", { 13.5f, 0.4f,  3.0f }, -60.0f, 24.0f, 14.0f },
};
#define GROUND_TOUR_SHOTS ((int)(sizeof GROUND_TOUR / sizeof GROUND_TOUR[0]))

// Outside: number keys 0 to 6, and the outside tour
static const Spot OUTSIDE_SPOTS[] = {
    { "Whole building", { -3.0f, 3.0f,  2.0f },  25.0f, 24.0f, 42.0f },
    { "Main entrance",  { -3.0f, 1.0f,  7.5f }, -15.0f, 30.0f, 16.0f },
    { "Car park",       { -4.0f, 0.4f, 15.0f },  20.0f, 35.0f, 20.0f },
    { "A&E doors",      { 10.5f, 0.4f,  4.0f },  40.0f, 26.0f, 15.0f },
    { "Helipad",        { 11.4f, 0.4f, -8.7f },  45.0f, 30.0f, 15.0f },
    { "Roof",           { -4.0f, 9.8f, -3.0f },  15.0f, 48.0f, 26.0f },
    { "Bus stop",       { 18.5f, 0.4f, 15.8f }, -70.0f, 22.0f, 10.0f },
};
#define OUTSIDE_SPOT_COUNT ((int)(sizeof OUTSIDE_SPOTS / sizeof OUTSIDE_SPOTS[0]))

#define SHOT_SECONDS 4.5f
static float tourTurn = 0.0f;     // how fast the current shot turns
static int   tourFloor = 0;       // which floor the tour is on (VIEW_OUTSIDE: the outside)

// How many shots the tour of a floor has
static int tour_length(int floor)
{
    if (floor == 0) return GROUND_TOUR_SHOTS + ROOM_COUNT;
    if (floor >= VIEW_OUTSIDE) return OUTSIDE_SPOT_COUNT;
    return floors3d_key_count(floor) + 2;          // the whole floor, each room, and the lifts
}

// Glide to the i-th shot of a floor's tour
static void start_shot(int floor, int i)
{
    Vector3 target;
    float yaw = (i % 2) ? 18.0f : -18.0f, pitch = 48.0f, dist;
    if (floor == 0) {
        if (i < GROUND_TOUR_SHOTS) {
            const Spot *s = &GROUND_TOUR[i];
            target = s->target; yaw = s->yaw; pitch = s->pitch; dist = s->dist;
        } else {
            dist = rooms3d_focus(i - GROUND_TOUR_SHOTS + 1, &target) * 0.85f;
        }
    } else if (floor >= VIEW_OUTSIDE) {
        const Spot *s = &OUTSIDE_SPOTS[i % OUTSIDE_SPOT_COUNT];
        target = s->target; yaw = s->yaw; pitch = s->pitch; dist = s->dist;
    } else {
        int n = floors3d_key_count(floor);
        if (i == 0) {                                          // the whole floor
            target = v3(-1.0f, floor * FLOOR_H + 0.4f, -1.5f); yaw = 0.0f; pitch = 52.0f; dist = 30.0f;
        } else if (i <= n) {
            dist = floors3d_key_focus(floor, i, &target) * 0.85f;
        } else {                                               // the lifts
            target = v3(LIFT_X, floor * FLOOR_H + 0.4f, 5.4f); yaw = 25.0f; pitch = 40.0f; dist = 9.0f;
        }
    }
    viewFloor = floor;
    glide_to(target, yaw, pitch, dist);
    tourTurn = 4.0f;
    tourTime = 0.0f;
}

// A number key, 0 to 9, on the floor being looked at
static void number_key(int k)
{
    Vector3 centre;
    if (viewFloor == 0) {
        if (k == 0)      glide_to(v3(3.0f, 0.4f, 0.9f), 0.0f, 42.0f, 21.0f);      // A&E
        else if (k == 8) glide_to(v3(-4.0f, 0.4f, -3.2f), 0.0f, 58.0f, 38.0f);    // the whole floor
        else if (k == 9) glide_to(v3(10.8f, 0.4f, -3.2f), 0.0f, 52.0f, 24.0f);    // helipad and bays
        else {
            float d = rooms3d_focus(k, &centre);                                 // 1 to 7: a room
            glide_to(centre, 0.0f, 50.0f, d);
        }
    } else if (viewFloor >= VIEW_OUTSIDE) {
        if (k < OUTSIDE_SPOT_COUNT) {
            const Spot *s = &OUTSIDE_SPOTS[k];
            glide_to(s->target, s->yaw, s->pitch, s->dist);
        }
    } else {
        int n = floors3d_key_count(viewFloor);
        if (k == 0)              glide_to(v3(-1.0f, viewFloor * FLOOR_H + 0.4f, -1.5f), 0.0f, 52.0f, 30.0f);
        else if (k <= n) {
            float d = floors3d_key_focus(viewFloor, k, &centre);
            glide_to(centre, 0.0f, 50.0f, d);
        }
        else if (k == 8)         glide_to(v3(LIFT_X, viewFloor * FLOOR_H + 0.4f, 5.4f), 25.0f, 40.0f, 9.0f);
    }
}

// The line of number keys for the floor being looked at, for the help text
static const char *number_key_help(void)
{
    static char text[256];
    if (viewFloor == 0) return "0 A&E   1 Ward   2 X-ray   3 MRI   4 Bloods   5 Theatre   6 Nursery   7 Admin   8 Whole floor   9 Helipad";
    int used = 0;
    text[0] = '\0';
    if (viewFloor >= VIEW_OUTSIDE) {
        for (int k = 0; k < OUTSIDE_SPOT_COUNT; k++) {
            used += snprintf(text + used, sizeof text - used, "%d %s   ", k, OUTSIDE_SPOTS[k].name);
        }
    } else {
        used += snprintf(text + used, sizeof text - used, "0 Whole floor   ");
        for (int k = 1; k <= floors3d_key_count(viewFloor); k++) {
            used += snprintf(text + used, sizeof text - used, "%d %s   ", k, floors3d_key_name(viewFloor, k));
        }
        snprintf(text + used, sizeof text - used, "8 Lifts");
    }
    return text;
}

// Look at another floor (or, with VIEW_OUTSIDE, the outside of the
// building): the camera rises or falls to it, keeping its angle.
static void set_view_floor(int f)
{
    if (f < 0) f = 0;
    if (f > VIEW_OUTSIDE) f = VIEW_OUTSIDE;
    int was = viewFloor;
    viewFloor = f;
    if (f == VIEW_OUTSIDE) {
        glide_to(v3(-3.0f, 3.0f, 2.0f), 25.0f, 24.0f, 42.0f);       // the whole building
    } else if (was == VIEW_OUTSIDE) {
        glide_to(v3(-1.0f, f * FLOOR_H + 0.4f, -1.5f), 0.0f, 50.0f, 30.0f);   // the whole floor
    } else {
        glide_to(v3(camTarget.x, f * FLOOR_H + 0.4f, camTarget.z), camYaw, camPitch, camDist);
    }
}

// The floor buttons in the 3D view's corner, like the buttons in a lift
// (in HUD units; set while drawing, checked when the mouse is clicked)
static Rectangle floorButton[FLOORS + 1];

static Camera3D make_camera(void);

// Stop the tour and stop following anyone: the mouse or a key took over.
static void manual_camera(void)
{
    touring = false;
    followId = 0;
}

void view3d_update_camera(void)
{
    // Up and down the floors: PAGE UP / PAGE DOWN, or F to go round them all
    if (IsKeyPressed(KEY_PAGE_UP))   { manual_camera(); set_view_floor(viewFloor + 1); }
    if (IsKeyPressed(KEY_PAGE_DOWN)) { manual_camera(); set_view_floor(viewFloor - 1); }
    if (IsKeyPressed(KEY_F))         { manual_camera(); set_view_floor((viewFloor + 1) % (VIEW_OUTSIDE + 1)); }

    // T: start or stop the tour of the whole hospital: every room on the
    // ground floor, then the first floor, the second, and the outside
    if (IsKeyPressed(KEY_T)) {
        touring = !touring;
        followId = 0;
        if (touring) { tourFloor = 0; tourShot = 0; start_shot(tourFloor, 0); }
    }

    // Number keys: the places on this floor (see number_key)
    for (int k = 0; k <= 9; k++) {
        if (IsKeyPressed(KEY_ZERO + k)) { manual_camera(); number_key(k); }
    }

    // A click (the button goes down and up without the mouse moving) picks a
    // patient to follow; view3d_draw works out who was clicked on
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { pressAt = GetMousePosition(); dragged = false; pressedHere = true; }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        if (fabsf(m.x - pressAt.x) + fabsf(m.y - pressAt.y) > 5.0f) dragged = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (pressedHere && !dragged) {
            clickPending = true;
            clickAt = GetMousePosition();
        }
        pressedHere = false;
    }

    // Left-drag turns the camera; right-drag moves it across the floor.
    // Turning is allowed while following someone; moving stops it.
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragged) {
        Vector2 d = GetMouseDelta();
        camYaw   -= d.x * 0.3f;
        camPitch += d.y * 0.3f;
        if (followId == 0) gliding = false;
        touring = false;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        float yaw = camYaw * DEG2RAD, k = camDist * 0.0025f;
        // right and forward along the floor, as seen from the camera
        Vector3 right = v3(cosf(yaw), 0, -sinf(yaw)), fwd = v3(-sinf(yaw), 0, -cosf(yaw));
        camTarget.x += (-right.x * d.x + fwd.x * d.y) * k;
        camTarget.z += (-right.z * d.x + fwd.z * d.y) * k;
        gliding = false;
        if (d.x != 0 || d.y != 0) manual_camera();
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        camDist -= wheel * 1.2f;
        if (followId != 0) goalDist = camDist;     // zoom in or out on the person being followed
        else gliding = false;
        touring = false;
    }

    // The tour: next shot every few seconds, turning slowly; a helicopter
    // coming in cuts straight to the helipad
    if (touring) {
        tourTime += GetFrameTime();
        if (heliCut && (viewFloor == 0 || viewFloor >= VIEW_OUTSIDE)) {   // (the helipad is on the ground)
            glide_to(v3(HELI_X, 0.4f, HELI_Z), 45.0f, 30.0f, 15.0f);     // from the far side, clear of the building
            tourTurn = 2.0f;
            tourTime = -4.0f;                        // stay a bit longer to watch it land
        } else if (tourTime > SHOT_SECONDS) {
            tourShot++;
            if (tourShot >= tour_length(tourFloor)) {          // on to the next floor (then round again)
                tourShot = 0;
                tourFloor = (tourFloor + 1) % (VIEW_OUTSIDE + 1);
            }
            start_shot(tourFloor, tourShot);
        }
        goalYaw += tourTurn * GetFrameTime();
        gliding = true;
    }
    heliCut = false;

    if (gliding) {
        float t = 1.0f - expf(-5.0f * GetFrameTime());
        camTarget.x += (goalTarget.x - camTarget.x) * t;
        camTarget.y += (goalTarget.y - camTarget.y) * t;          // up or down to another floor
        camTarget.z += (goalTarget.z - camTarget.z) * t;
        camYaw   += (goalYaw - camYaw) * t;
        camPitch += (goalPitch - camPitch) * t;
        camDist  += (goalDist - camDist) * t;
        if (fabsf(goalDist - camDist) < 0.05f && fabsf(goalYaw - camYaw) < 0.1f &&
            fabsf(goalPitch - camPitch) < 0.1f && fabsf(goalTarget.x - camTarget.x) < 0.02f &&
            fabsf(goalTarget.z - camTarget.z) < 0.02f && fabsf(goalTarget.y - camTarget.y) < 0.02f &&
            !touring && followId == 0) gliding = false;
    }

    // Keep the camera above the floor and within reach of the building
    if (camPitch < 12.0f) camPitch = 12.0f;
    if (camPitch > 85.0f) camPitch = 85.0f;
    if (camDist < 7.0f)  camDist = 7.0f;
    if (camDist > 48.0f) camDist = 48.0f;
    if (viewFloor >= VIEW_OUTSIDE) {          // outside there's more to look at
        camTarget.x = fminf(fmaxf(camTarget.x, -20.0f), 20.0f);
        camTarget.z = fminf(fmaxf(camTarget.z, -16.0f), 19.0f);
        // and the camera must never end up inside the building: tip it up until it's clear
        for (int i = 0; i < 80 && camPitch < 85.0f; i++) {
            Vector3 p = make_camera().position;
            bool inside = p.x > BUILDING_X0 - 1.0f && p.x < BUILDING_X1 + 1.0f && p.z > BUILDING_Z0 - 1.0f &&
                          p.z < BUILDING_Z1 + 1.0f && p.y < FLOORS * FLOOR_H + 3.0f;
            if (!inside) break;
            camPitch += 1.0f;
        }
    } else {
        camTarget.x = fminf(fmaxf(camTarget.x, -17.0f), 15.0f);
        camTarget.z = fminf(fmaxf(camTarget.z, -13.0f), 8.0f);
    }
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

// The labels and the HUD are drawn at the size they'd be in a 1000x600 window,
// then scaled up by `ui` for bigger windows (full screen), so they stay the
// same size on screen compared to everything else. hudW and hudH are the
// window's size in those units.
static float ui = 1.0f, hudW = SCREEN_W, hudH = SCREEN_H;

// Where a point in the world is, in HUD units.
static Vector2 to_hud(Vector3 at, Camera3D cam)
{
    Vector2 s = GetWorldToScreen(at, cam);
    return (Vector2){ s.x / ui, s.y / ui };
}

static void label(const char *text, Vector3 at, Camera3D cam, int size, Color fg, Color bg)
{
    Vector2 s = to_hud(at, cam);
    int w = MeasureText(text, size);
    DrawRectangleRounded((Rectangle){ s.x - w / 2 - 5, s.y - size / 2 - 3, w + 10, size + 6 },
                         0.5f, 6, bg);
    DrawText(text, (int)s.x - w / 2, (int)s.y - size / 2, size, fg);
}

static void progress_bar(Vector3 at, Camera3D cam, float done, Color fill)
{
    Vector2 s = to_hud(at, cam);
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
    if (p->walkLeft > 0.0f)            return "On the way to the bed";
    if (p->doneInAE)                   return "Treated: waiting for a wheelchair";
    if (p->equipMissing) {
        static char text[96];
        char list[64];
        sim_equipment_text(p->equipMissing, list, sizeof list);
        snprintf(text, sizeof text, "Waiting for: %s", list);
        return text;
    }
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
    m = (Vector2){ m.x / ui, m.y / ui };
    const char *name = (bed == RESUS_BED) ? "Resus" : TextFormat("Bed %d", bed + 1);
    const char *l1 = p ? TextFormat("%s   Patient %d", name, p->id) : name;
    const char *l2 = p ? (p->condition ? TextFormat("%s (%s)", p->condition->name, p->condition->icd10)
                                       : "Condition not recorded")
                       : "Free";
    const char *l3 = p ? status_text(sim, bed, p) : "";

    int w = MeasureText(l1, 18);
    if (MeasureText(l2, 16) > w) w = MeasureText(l2, 16);
    if (MeasureText(l3, 16) > w) w = MeasureText(l3, 16);
    Rectangle r = { m.x + 16, m.y + 12, w + 24, p ? 78 : 52 };
    if (r.x + r.width > hudW - 8) r.x = m.x - r.width - 16;

    DrawRectangleRounded(r, 0.2f, 8, C_CARD);
    DrawRectangleRoundedLines(r, 0.2f, 8, C_EDGE);
    if (p) DrawRectangle((int)r.x, (int)r.y + 8, 4, (int)r.height - 16, severity_color(p->severity));
    DrawText(l1, (int)r.x + 12, (int)r.y + 8, 18, C_INK);
    DrawText(l2, (int)r.x + 12, (int)r.y + 32, 16, C_MUTED);
    if (p) DrawText(l3, (int)r.x + 12, (int)r.y + 54, 16, C_BRAND);
}

// ------------------------------------------------------------------------
// Following a patient (click on them)
// ------------------------------------------------------------------------

// Where the camera should look while following this patient: at them, or at
// the ambulance or helicopter they're in.
static Vector3 follow_point(const Tracked *t)
{
    if (t->vehicle == HELI_WAITING) return v3(HELI_X, 0, HELI_Z);        // waiting to be flown in
    if (t->vehicle >= 0) {
        if (vehicles[t->vehicle].state == V_IDLE) return v3(LANE_IN, 0, ROAD_END);   // not set off yet
        Vector3 p;
        float angle;
        if (is_heli(t->vehicle)) helicopter_place(t->vehicle, &p, &angle);
        else                     ambulance_place(t->vehicle, &p, &angle);
        return p;
    }
    return t->pos;
}

// Who is under the mouse: a patient standing, sitting or walking, or lying
// in an A&E bed. 0 if nobody.
static int patient_under(const Sim *sim, Camera3D cam, Vector2 at)
{
    Ray ray = GetScreenToWorldRay(at, cam);
    int best = 0;
    float bestDist = 1e9f;
    for (int i = 0; i < MAX_TRACKED; i++) {
        const Tracked *t = &tracked[i];
        if (!t->used || t->vehicle >= 0 || t->vehicle == HELI_WAITING) continue;
        if (!visible_from_view(t->pos)) continue;                 // on another floor
        BoundingBox box = { v3(t->pos.x - 0.45f, t->pos.y, t->pos.z - 0.45f), v3(t->pos.x + 0.45f, t->pos.y + 1.9f, t->pos.z + 0.45f) };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < bestDist) { best = t->id; bestDist = hit.distance; }
    }
    for (int i = 0; i < BED_COUNT && viewFloor == 0; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;
        Vector3 c = bed_centre(i);
        float hx = (i == RESUS_BED) ? 1.1f : 0.6f, hz = (i == RESUS_BED) ? 0.6f : 1.1f;
        BoundingBox box = { v3(c.x - hx, 0, c.z - hz), v3(c.x + hx, 1.0f, c.z + hz) };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < bestDist) { best = p->id; bestDist = hit.distance; }
    }
    return best;
}

// Find a patient anywhere in the hospital, and say where they are and what's
// happening to them. NULL if they've left.
static const Patient *find_patient(const Sim *sim, int id, char *where, int size)
{
    int n = 1;
    for (const Patient *p = sim->waiting; p != NULL; p = p->next, n++) {
        if (p->id != id) continue;
        snprintf(where, size, "Waiting for a bed (number %d in the queue)", n);
        return p;
    }
    for (int b = 0; b < BED_COUNT; b++) {
        const Patient *p = sim->beds[b].occupant;
        if (p == NULL || p->id != id) continue;
        if (b == RESUS_BED) snprintf(where, size, "Resus: %s", status_text(sim, b, p));
        else                snprintf(where, size, "Bed %d: %s", b + 1, status_text(sim, b, p));
        return p;
    }
    for (int r = 0; r < ROOM_TYPES; r++) {
        const SimRoom *room = &sim->rooms[r];
        for (int s = 0; s < room->capacity; s++) {
            const Patient *p = room->in[s];
            if (p == NULL || p->id != id) continue;
            if (p->walkLeft > 0.0f)      snprintf(where, size, "Walking to %s", room->name);
            else if (p->roomLeft <= 0) snprintf(where, size, "Done in %s: waiting for a wheelchair", room->name);
            else snprintf(where, size, "In %s (%.0f min to go)", room->name, sim_minutes(p->roomLeft));
            return p;
        }
        for (const Patient *p = room->queue; p != NULL; p = p->next) {
            if (p->id != id) continue;
            snprintf(where, size, "Queuing for %s", room->name);
            return p;
        }
    }
    for (const Patient *p = sim->leaving; p != NULL; p = p->next) {
        if (p->id != id) continue;
        snprintf(where, size, "Being taken to the front door");
        return p;
    }
    for (const Patient *p = sim->inbound; p != NULL; p = p->next) {
        if (p->id != id) continue;
        snprintf(where, size, "In the air: landing in %.0f min", sim_minutes(p->roomLeft));
        return p;
    }
    return NULL;
}

// The card in the bottom left while following someone: who they are, how
// they got here, where they are now and where they go next.
static void journey_card(const Sim *sim)
{
    // Kept from frame to frame, so the card still says who they were once they've left
    static int  cardId = 0;
    static char who[64], what[96], came[96], next[96], kit[96];
    static int  severity = 3;
    char now[128];

    const Patient *p = find_patient(sim, followId, now, sizeof now);
    if (p != NULL) {
        cardId = followId;
        severity = p->severity;
        snprintf(who, sizeof who, "Patient %d  -  %s", p->id,
                 p->severity == 1 ? "emergency" : p->severity == 2 ? "urgent" : "routine");
        snprintf(what, sizeof what, "%s (%s)", p->condition->name, p->condition->icd10);
        int mins = DAY_START_HOUR * 60 + (int)sim_minutes(p->arrivalTime);
        int stay = (int)sim_minutes(sim->clock - p->arrivalTime);
        if (p->rescue) snprintf(came, sizeof came, "Flown in at %02d:%02d by %s  (%d min ago)", (mins / 60) % 24, mins % 60,
                                rescue_service_name((RescueService)p->rescueService), stay);
        else           snprintf(came, sizeof came, "Arrived at %02d:%02d  (%d min ago)", (mins / 60) % 24, mins % 60, stay);
        char route[64];
        sim_route_text(p, route, sizeof route);
        snprintf(next, sizeof next, "After A&E: %s", route);
        char list[80];
        if (p->equipMissing) {
            sim_equipment_text(p->equipMissing, list, sizeof list);
            snprintf(kit, sizeof kit, "Waiting for: %s", list);
        } else if (p->equipHeld) {
            sim_equipment_text(p->equipHeld, list, sizeof list);
            snprintf(kit, sizeof kit, "With: %s", list);
        } else {
            sim_equipment_text(p->equipNeeds, list, sizeof list);
            snprintf(kit, sizeof kit, "%s%s", p->equipNeeds ? "Needs: " : "No equipment needed", list);
        }
    } else {
        if (cardId != followId) return;          // never found them: nothing to show
        snprintf(now, sizeof now, "Leaving the hospital");
    }

    Rectangle r = { 20, hudH - 94 - 154, 380, 146 };
    DrawRectangleRounded(r, 0.12f, 8, night ? (Color){ 30, 38, 56, 235 } : (Color){ 255, 255, 255, 235 });
    DrawRectangleRoundedLines(r, 0.12f, 8, C_EDGE);
    DrawRectangle((int)r.x, (int)r.y + 10, 5, (int)r.height - 20, severity_color(severity));
    Color ink = night ? (Color){ 225, 232, 245, 255 } : C_INK;
    Color soft = night ? (Color){ 150, 165, 190, 255 } : C_MUTED;
    int x = (int)r.x + 16, y = (int)r.y + 10;
    DrawText("FOLLOWING", x, y, 12, C_BRAND);                    y += 16;
    DrawText(who, x, y, 18, ink);                                 y += 22;
    DrawText(what, x, y, 15, soft);                               y += 19;
    DrawText(came, x, y, 14, soft);                               y += 18;
    DrawText(TextFormat("Now: %s", now), x, y, 14, C_BRAND);      y += 18;
    DrawText(next, x, y, 14, soft);                               y += 18;
    DrawText(kit, x, y, 14, soft);
    DrawText("Click anywhere else to stop", (int)r.x + (int)r.width - 12 - MeasureText("Click anywhere else to stop", 11),
             (int)r.y + 10, 11, soft);
}

static void hud(const Sim *sim, float speed, bool paused)
{
    Color text = night ? (Color){ 225, 232, 245, 255 } : C_INK;
    Color soft = night ? (Color){ 150, 165, 190, 255 } : C_MUTED;

    // Soft panels behind the text, so it reads over anything
    Color backing = night ? (Color){ 20, 26, 40, 170 } : (Color){ 255, 255, 255, 170 };
    DrawRectangleRounded((Rectangle){ 10, 8, 520, touring ? 102 : 84 }, 0.15f, 8, backing);
    DrawRectangleRounded((Rectangle){ 10, hudH - 78, 920, 70 }, 0.15f, 8, backing);
    DrawText("Hospital Resource Simulator", 20, 16, 24, night ? (Color){ 140, 180, 240, 255 } : C_BRAND);
    const char *where;
    if (viewFloor >= VIEW_OUTSIDE)  where = "OUTSIDE THE HOSPITAL";
    else if (viewFloor > 0)         where = TextFormat("%s   %s", floors3d_floor_name(viewFloor), floors3d_name_at(viewFloor, camTarget.x, camTarget.z));
    else if (camDist > 36.0f)       where = "GROUND FLOOR   WHOLE FLOOR";
    else                            where = TextFormat("GROUND FLOOR   %s", rooms3d_name_at(camTarget.x, camTarget.z));
    DrawText(TextFormat("3D VIEW   %s", where), 20, 46, 16, soft);
    const char *sky = (rain > 0.6f) ? "Heavy rain" : (rain > 0.1f) ? "Raining" :
                      (sunset > 0.3f) ? (night ? "Sunrise" : "Sunset") : (darkness > 0.6f) ? "Clear night" : "Clear";
    DrawText(sky, 20, 66, 14, soft);
    if (touring) DrawText(TextFormat("CINEMATIC TOUR: %s   T to stop", floors3d_floor_name(tourFloor)), 20, 86, 16, C_BRAND);
    if (followId != 0) journey_card(sim);

    // Stats card, top right
    int busyN = 0, busyD = 0;
    for (int k = 0; k < NURSES_ON(sim->shift); k++)  if (sim->nurses[k].busyWithBed != -1) busyN++;
    for (int k = 0; k < sim_doctors_on(sim); k++) if (sim->doctors[k].busyWithBed != -1) busyD++;
    int hh, mm;
    sim_time_of_day(sim, &hh, &mm);

    Rectangle card = { hudW - 230, 14, 216, 158 };
    DrawRectangleRounded(card, 0.12f, 8, night ? (Color){ 30, 38, 56, 230 } : (Color){ 255, 255, 255, 230 });
    int x = (int)card.x + 14, y = (int)card.y + 12;
    DrawText(TextFormat("%s  %02d:%02d", sim->shift == SHIFT_NIGHT ? "Night shift" : "Day shift", hh, mm),
             x, y, 18, text);                                                          y += 28;
    DrawText(TextFormat("Waiting      %d", sim->waitingCount), x, y, 16, soft);       y += 22;
    DrawText(TextFormat("Treated      %d", sim->stats.treated), x, y, 16, soft);      y += 22;
    DrawText(TextFormat("Nurses busy  %d / %d", busyN, NURSES_ON(sim->shift)), x, y, 16, soft);   y += 22;
    DrawText(TextFormat("Doctors busy %d / %d%s", busyD, sim_doctors_on(sim), sim->onCall ? " +on call" : ""), x, y, 16,
             sim->onCall ? C_EMERGENCY : soft);                                                               y += 22;
    DrawText(TextFormat("Speed        %.1fx", speed), x, y, 16, soft);

    // Equipment card: a row of blocks for each kind, filled for the ones in use
    Rectangle kitCard = { hudW - 230, 180, 216, 128 };
    DrawRectangleRounded(kitCard, 0.12f, 8, night ? (Color){ 30, 38, 56, 230 } : (Color){ 255, 255, 255, 230 });
    x = (int)kitCard.x + 14; y = (int)kitCard.y + 10;
    DrawText("EQUIPMENT IN USE", x, y, 13, soft);                                     y += 20;
    for (int e = 0; e < EQUIP_TYPES; e++) {
        const Equipment *q = &sim->equipment[e];
        DrawRectangle(x, y + 3, 9, 9, draw_equipment_colour(e));
        DrawText(sim_equipment_short(e), x + 14, y, 14, text);
        for (int k = 0; k < q->total; k++) {
            Rectangle b = { (float)(x + 98 + k * 15), (float)(y + 1), 12, 12 };
            if (k < q->inUse) DrawRectangleRec(b, draw_equipment_colour(e));
            else              DrawRectangleLinesEx(b, 1, soft);
        }
        y += 19;
    }

    // Floor buttons, like the ones in a lift: G, 1, 2, and the outside
    Rectangle panel = { hudW - 230, 316, 216, 64 };
    DrawRectangleRounded(panel, 0.2f, 8, night ? (Color){ 30, 38, 56, 230 } : (Color){ 255, 255, 255, 230 });
    DrawText("FLOOR   (F, PAGE UP/DOWN)", (int)panel.x + 14, (int)panel.y + 8, 12, soft);
    const char *names[FLOORS + 1] = { "G", "1", "2", "OUT" };
    for (int f = 0; f <= VIEW_OUTSIDE; f++) {
        Rectangle b = { panel.x + 12 + f * 50, panel.y + 26, (f == VIEW_OUTSIDE) ? 50 : 40, 30 };
        floorButton[f] = b;
        bool on = (viewFloor == f);
        bool over = CheckCollisionPointRec((Vector2){ GetMousePosition().x / ui, GetMousePosition().y / ui }, b);
        DrawRectangleRounded(b, 0.5f, 8, on ? C_BRAND : over ? (Color){ 200, 210, 225, 255 } : (Color){ 220, 226, 235, 255 });
        DrawText(names[f], (int)(b.x + b.width / 2 - MeasureText(names[f], 16) / 2), (int)b.y + 7, 16, on ? WHITE : C_INK);
    }

    if (paused) DrawText("PAUSED", (int)hudW / 2 - MeasureText("PAUSED", 30) / 2, 20, 30, C_EMERGENCY);

    DrawText(number_key_help(), 20, (int)hudH - 70, 14, soft);
    DrawText("Drag to turn   Right-drag to move   Scroll to zoom   Click a patient to follow   T cinematic tour   F floors",
             20, (int)hudH - 50, 14, soft);
    DrawText("V 2D view   SPACE pause   UP/DOWN speed   E emergency   H helicopter   O operate   M mute",
             20, (int)hudH - 30, 14, soft);
}

// ------------------------------------------------------------------------
// The frame
// ------------------------------------------------------------------------

// The bed a member of staff should be standing at: the bed they're looking
// after, but only once its patient has got there (not while they're still
// walking to it). -1: wait at the nurses' station.
static int staff_bed(const Sim *sim, int bed)
{
    if (bed == -1) return -1;
    const Patient *p = sim->beds[bed].occupant;
    if (p == NULL || view3d_patient_walking(p->id)) return -1;
    return bed;
}

void view3d_draw(const Sim *sim, float speed, bool paused)
{
    night = (sim->shift == SHIFT_NIGHT);
    update_weather(sim);             // the light and the rain for this time of day
    if (!signsReady) make_signs();
    rooms3d_prepare(sim);            // the other rooms' signs, before the scene too
    floors3d_prepare();              // ...and the signs upstairs and outside
    outside3d_prepare();
    update_tv(sim);                  // draws into the TV's picture, so before the scene
    update_bed_screens(sim);         // and the screens behind the beds
    update_patients(sim, paused ? 0.0f : speed);

    // Sounds: helicopter blades while one is flying, and the rain
    bool flying = false;
    for (int k = HELI_FIRST; k < VEHICLES; k++) if (vehicles[k].state != V_IDLE) flying = true;
    sound_loop(SND_ROTOR, flying && !paused, 0.35f);
    sound_loop(SND_RAIN, rain > 0.05f, 0.55f * rain);

    // A click: follow the patient clicked on, or stop following
    Camera3D cam = make_camera();
    if (clickPending) {
        clickPending = false;
        // The floor buttons first (they're in HUD units)
        Vector2 h = { clickAt.x / ui, clickAt.y / ui };
        int button = -1;
        for (int f = 0; f <= VIEW_OUTSIDE; f++) if (CheckCollisionPointRec(h, floorButton[f])) button = f;
        int id = 0;
        if (button >= 0) {
            touring = false;
            set_view_floor(button);
            id = followId;                   // (keep following whoever it was)
        } else {
            id = patient_under(sim, cam, clickAt);
        }
        followId = id;
        if (id != 0) {
            touring = false;
            followIntro = 1.2f;              // swing round to them first: closer and from above
            goalPitch = 40.0f;
            goalDist = 11.0f;
        }
    }
    // Following: keep them in the middle (turning and zooming still work)
    if (followId != 0) {
        Tracked *t = find_tracked(followId);
        if (t == NULL) {
            followId = 0;                    // gone
        } else {
            Vector3 p = follow_point(t);
            p.x = fminf(fmaxf(p.x, -17.0f), 15.0f);
            goalTarget = v3(p.x, p.y + 0.4f, p.z);
            // Inside the building: look at whichever floor they're on
            if (p.y > 0.1f || inside_ground_floor(p)) viewFloor = floor_of(p.y);
            if (followIntro > 0) {
                goalYaw = camYaw;
                followIntro -= GetFrameTime();
            } else {
                goalYaw = camYaw;            // let the mouse turn round them
                goalPitch = camPitch;
            }
            gliding = true;
        }
    }

    // Which walls face the camera? Those are drawn low, like a doll's house.
    Vector3 look = { cam.position.x - cam.target.x, 0, cam.position.z - cam.target.z };
    float len = sqrtf(look.x * look.x + look.z * look.z);
    lowBack  = look.z / len < -0.30f;
    lowLeft  = look.x / len < -0.35f;
    lowRight = look.x / len >  0.35f;
    lowFront = look.z / len >  0.30f;
    // Looking at the rooms behind or to the left of A&E: lower the A&E wall in the way
    if (camTarget.z < -4.5f && look.z > 0) lowBack = true;
    if (camTarget.x < -7.5f && look.x > 0) lowLeft = true;

    // Where everyone should be standing: by their bed, or at the station.
    if (!staffPlaced) {
        for (int k = 0; k < NURSE_COUNT; k++)  nursePos[k]  = v3(-3.0f + k * 1.0f, 0, DESK_Z + 0.9f);
        for (int k = 0; k < DOCTOR_SLOTS; k++) doctorPos[k] = v3( 1.6f + k * 1.0f, 0, DESK_Z + 0.9f);
        staffPlaced = true;
    }
    // Staff go to their patient's bed once the patient is there; until then
    // (or with nobody to see) they wait at the nurses' station.
    // The on-call doctor comes in through the front doors when they're called in
    if (sim->onCall && !onCallWasIn) doctorPos[DOCTORS_ON(sim->shift)] = v3(DOOR_X - 0.3f, 0, 3.5f);
    onCallWasIn = sim->onCall;
    Vector3 nurseTarget[NURSE_COUNT], doctorTarget[DOCTOR_SLOTS];
    int nurseBed[NURSE_COUNT], doctorBed[DOCTOR_SLOTS];
    for (int k = 0; k < NURSE_COUNT; k++) {
        nurseBed[k] = staff_bed(sim, sim->nurses[k].busyWithBed);
        int b = nurseBed[k];
        nurseTarget[k] = (b != -1) ? bed_point(b, 0.30f, 0, 1.55f)              // foot of the bed
                                   : v3(-3.0f + k * 1.0f, 0, DESK_Z + 0.9f);    // at the station
    }
    for (int k = 0; k < DOCTOR_SLOTS; k++) {
        doctorBed[k] = staff_bed(sim, sim->doctors[k].busyWithBed);
        int b = doctorBed[k];
        doctorTarget[k] = (b != -1) ? bed_point(b, -0.30f, 0, 1.55f)
                                    : v3(1.6f + k * 1.0f, 0, DESK_Z + 0.9f);
    }

    // The sky: a soft gradient that follows the time of day and the weather
    draw_sky_gradient();
    draw_sky_objects(cam);                   // the sun, moon and stars: everything else is in front

    BeginMode3D(cam);
        rlDisableBackfaceCulling();          // so every face shows from both sides

        draw_grounds();
        draw_outside_ground();
        outside3d_draw(viewFloor);           // the car park, and the floors below this one closed up
        draw_puddles();
        draw_lamps();
        bool ground = (viewFloor == 0);
        if (ground) {
            draw_room(sim);
            rooms3d_draw(sim, cam.position, cam.target);
            draw_equipment_bay(sim);
        } else {
            draw_entrance();                 // A&E's doors and sign, in the closed-up ground floor
            if (viewFloor < VIEW_OUTSIDE) floors3d_draw(sim, viewFloor, cam.position, cam.target);
        }
        if (viewFloor < VIEW_OUTSIDE) floors3d_draw_lift(viewFloor, liftCarY, liftDoors[viewFloor]);
        for (int i = 0; i < BED_COUNT && ground; i++) {
            const Patient *bp = sim->beds[i].occupant;
            // Draw the bed as if at x = 0 on the back wall, moved and turned into place
            Vector3 c = bed_centre(i);
            rlPushMatrix();
            rlTranslatef(c.x, 0, c.z);
            rlRotatef(bed_turn(i), 0, 1, 0);
            rlTranslatef(0, 0, -BED_Z);
            draw_bed(0);
            bool there = bp && !view3d_patient_walking(bp->id);
            if (there) draw_lying_patient(0, bp);                       // empty until they get there
            draw_bed_equipment(there ? bp : NULL);                      // whatever they have with them
            rlPopMatrix();
            if (i + 1 < WALL_BEDS) draw_curtain(bed_x(i) + BED_SPACING / 2);
        }

        // Waiting room: one chair per seat, filled most urgent first.
        // Someone still walking in leaves their chair empty until they get there.
        const Patient *p = sim->waiting;
        for (int seat = 0; seat < CHAIR_ROWS * CHAIRS_PER_ROW && ground; seat++) {
            draw_chair(chair_x(seat), chair_z(seat), (p && !view3d_patient_walking(p->id)) ? p : NULL);
            if (p) p = p->next;
        }

        // The staff keep moving whichever floor is being looked at
        for (int k = 0; k < NURSES_ON(sim->shift); k++) {
            bool walking = is_walking(nursePos[k], nurseTarget[k]);
            Facing face = walking ? facing_for(nursePos[k], nurseTarget[k])
                                  : (nurseBed[k] == RESUS_BED ? FACE_EAST : nurseBed[k] != -1 ? FACE_NORTH : FACE_SOUTH);
            nursePos[k] = walk_towards(nursePos[k], nurseTarget[k]);
            if (ground) draw_person(nursePos[k], face, nurse_outfit(), walking, (float)GetTime() * 14.0f + k);
        }
        for (int k = 0; k < sim_doctors_on(sim); k++) {
            bool walking = is_walking(doctorPos[k], doctorTarget[k]);
            Facing face = walking ? facing_for(doctorPos[k], doctorTarget[k])
                                  : (doctorBed[k] == RESUS_BED ? FACE_EAST : doctorBed[k] != -1 ? FACE_NORTH : FACE_SOUTH);
            doctorPos[k] = walk_towards(doctorPos[k], doctorTarget[k]);
            if (ground) draw_person(doctorPos[k], face, doctor_outfit(), walking, (float)GetTime() * 14.0f + k);
        }
        draw_walkers();
        draw_vehicles();
        draw_helipad_kit();
        if (!lowRight || !ground) draw_door_glass();      // see-through, so last
        outside3d_draw_main_door_glass();
        if (ground) rooms3d_draw_glass();
        else if (viewFloor < VIEW_OUTSIDE) floors3d_draw_glass(viewFloor);
        draw_rain(cam);

        // Light. By day, soft pools under the ceiling lights. As it gets dark
        // the lamps come on: one over each patient, the station lamp, the doors,
        // and the street lamps outside.
        BeginBlendMode(BLEND_ADDITIVE);
            unsigned char dayA = (unsigned char)(22 * (1.0f - darkness));
            if (dayA > 1 && ground) {
                for (int gx = -6; gx <= 6; gx += 4) {
                    glow((float)gx, -2.6f, 2.4f, (Color){ 255, 255, 255, dayA });
                    glow((float)gx,  3.6f, 2.4f, (Color){ 255, 255, 255, dayA });
                }
            }
            float lampK = night ? 1.0f : smooth(0.4f, 0.9f, darkness);    // lamps fade up at dusk
            if (lampK > 0.01f && ground) {
                for (int i = 0; i < BED_COUNT; i++) {
                    if (sim->beds[i].occupant) glow(bed_centre(i).x, bed_centre(i).z + 0.2f, 1.7f, (Color){ 255, 200, 120, (unsigned char)(70 * lampK) });
                }
                glow(0, DESK_Z + 0.4f, 2.2f, (Color){ 255, 220, 160, (unsigned char)(60 * lampK) });
                glow(DOOR_X, 3.5f, 1.8f, (Color){ 200, 220, 255, (unsigned char)(50 * lampK) });
                glow(6.6f, 1.75f, 1.4f, (Color){ 255, 220, 160, (unsigned char)(45 * lampK) });           // reception
            }
            lamp_glows();
            outside3d_lights(viewFloor);
            if (ground) rooms3d_lights(sim);
            else if (viewFloor < VIEW_OUTSIDE) floors3d_lights(sim, viewFloor);
            vehicle_glows();
        EndBlendMode();

        rlEnableBackfaceCulling();
    EndMode3D();

    // Labels on top: bed numbers, progress, staff names, warnings. Drawn in
    // HUD units and scaled up to the window by a 2D camera.
    ui = fmaxf(1.0f, GetScreenHeight() / (float)SCREEN_H);
    hudW = GetScreenWidth() / ui;
    hudH = GetScreenHeight() / ui;
    Camera2D hudCam = { 0 };
    hudCam.zoom = ui;
    BeginMode2D(hudCam);
    Color tagBg = night ? (Color){ 30, 38, 56, 210 } : (Color){ 255, 255, 255, 210 };
    Color tagFg = night ? (Color){ 225, 232, 245, 255 } : C_INK;
    int hoverBed = -1;
    Ray ray = GetScreenToWorldRay(GetMousePosition(), cam);

    for (int i = 0; i < BED_COUNT && ground; i++) {
        const Patient *bp = sim->beds[i].occupant;
        if (i == RESUS_BED) label("Resus", bed_point(i, 0, 1.30f, -1.10f), cam, 14, WHITE, C_EMERGENCY);
        else                label(TextFormat("Bed %d", i + 1), bed_point(i, 0, 1.30f, -1.10f), cam, 14, tagFg, tagBg);
        if (bp && !view3d_patient_walking(bp->id)) {    // only once they've reached the bed
            float done = 1.0f - bp->treatmentLeft / bp->treatmentTotal;
            bool stalled = bp->needsDoctor && sim_doctor_at(sim, i) == -1;
            progress_bar(bed_point(i, 0, 1.15f, 0.2f), cam, done, stalled ? C_EMERGENCY : C_BRAND);
            bool noNurse = !bp->needsDoctor && sim_nurse_at(sim, i) == -1 && !bp->doneInAE;   // nobody has reached them yet
            if (stalled) label("needs Dr", bed_point(i, 0, 1.55f, 0.2f), cam, 14, WHITE, C_EMERGENCY);
            if (noNurse) label("needs nurse", bed_point(i, 0, 1.55f, 0.2f), cam, 14, WHITE, (Color){ 230, 145, 35, 255 });
            // waiting for equipment: what they're waiting for, in its colour
            if (bp->equipMissing != 0 && !stalled && !noNurse) {
                int e = 0;
                while (e < EQUIP_TYPES - 1 && !(bp->equipMissing & EQUIP_BIT(e))) e++;
                label(TextFormat("needs %s", sim_equipment_short(e)), bed_point(i, 0, 1.55f, 0.2f), cam, 14, WHITE, draw_equipment_colour(e));
            }
        }
        Vector3 c = bed_centre(i);
        float hx = (i == RESUS_BED) ? 1.1f : 0.6f, hz = (i == RESUS_BED) ? 0.6f : 1.1f;   // half the bed's size
        BoundingBox box = { v3(c.x - hx, 0, c.z - hz), v3(c.x + hx, 1.0f, c.z + hz) };
        if (GetRayCollisionBox(ray, box).hit) hoverBed = i;
    }
    for (int k = 0; k < NURSES_ON(sim->shift) && ground; k++) {
        float h = 1.8f + (k % 2) * 0.3f;   // every second tag a little higher, so neighbours don't overlap
        label(sim->nurses[k].name, v3(nursePos[k].x, h, nursePos[k].z), cam, 12, WHITE, C_BRAND);
    }
    for (int k = 0; k < sim_doctors_on(sim) && ground; k++) {
        float h = 2.1f + (k % 2) * 0.3f;   // doctors' tags sit above the nurses'
        bool onCall = (k >= DOCTORS_ON(sim->shift));
        label(TextFormat(onCall ? "Dr %s (on call)" : "Dr %s", sim->doctors[k].name), v3(doctorPos[k].x, h, doctorPos[k].z),
              cam, 12, WHITE, onCall ? C_EMERGENCY : C_INK);
    }
    int seats = CHAIR_ROWS * CHAIRS_PER_ROW;
    if (sim->waitingCount > seats && ground) {
        label(TextFormat("+%d more waiting", sim->waitingCount - seats), v3(0, 0.1f, 6.4f),
              cam, 16, WHITE, C_EMERGENCY);
    }

    hud(sim, speed, paused);
    if (hoverBed != -1 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) bed_tooltip(sim, hoverBed);
    draw_rescue_banner(sim, (int)hudW / 2, 72);     // e.g. "COAST GUARD RESCUE INBOUND"
    opgame_draw_offer((int)hudW / 2, 110);          // "SURGERY READY - press O"
    EndMode2D();
}