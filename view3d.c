// view3d.c - the department in 3D, drawn in a blocky "voxel" style.
//
// Everything is built from boxes. raylib's own cubes are one flat colour, so
// draw_box() draws each face with its own brightness instead (top brightest,
// sides darker). That fakes a light shining from above and gives depth
// without needing any shaders.
//
// Layout (1 unit is roughly 1 metre, looking from the waiting room):
//   back:   6 beds along the back wall
//   middle: the nurses' station, where free staff wait
//   front:  the waiting room, with a chair for each waiting patient

#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "view3d.h"

// ---- Layout ----
#define BED_Z        -3.2f   // the row of beds
#define BED_SPACING   2.5f
#define DESK_Z       -0.2f   // nurses' station
#define CHAIRS_PER_ROW 10
#define CHAIR_ROWS     2
#define CHAIR_SPACING  1.2f

// ---- Colours used only in 3D ----
#define C_SKIN      (Color){ 232, 190, 160, 255 }
#define C_HAIR      (Color){  70,  50,  40, 255 }
#define C_BEDFRAME  (Color){ 210, 216, 226, 255 }
#define C_MATTRESS  (Color){ 245, 248, 252, 255 }
#define C_WALL      (Color){ 226, 232, 241, 255 }
#define C_DESK      (Color){ 150, 120,  95, 255 }
#define C_CHAIR     (Color){ 120, 135, 160, 255 }
#define C_TROUSERS  (Color){  45,  58,  80, 255 }
#define C_COAT      (Color){ 248, 248, 250, 255 }

// ---- Camera, controlled by the mouse ----
static float camYaw   = 0.0f;    // degrees around the department
static float camPitch = 42.0f;   // degrees above the floor
static float camDist  = 17.0f;   // distance from the centre

// ---- Where each member of staff is standing; they walk between spots ----
static Vector3 nursePos[NURSE_COUNT];
static Vector3 doctorPos[DOCTOR_COUNT];
static bool    staffPlaced = false;

static bool night = false;       // set each frame; darkens every colour

// ------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------

static Vector3 v3(float x, float y, float z) { return (Vector3){ x, y, z }; }

static float bed_x(int i) { return -6.25f + i * BED_SPACING; }

// Colour with a brightness factor, darker and bluer at night.
static Color shade(Color c, float f)
{
    float r = c.r * f, g = c.g * f, b = c.b * f;
    if (night) { r *= 0.50f; g *= 0.56f; b *= 0.78f; }
    return (Color){ (unsigned char)fminf(r, 255), (unsigned char)fminf(g, 255),
                    (unsigned char)fminf(b, 255), c.a };
}

// One flat four-sided face. Drawn with both windings, so it shows from either
// side whichever way the camera looks.
static void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col)
{
    DrawTriangle3D(a, b, c, col);  DrawTriangle3D(a, c, d, col);
    DrawTriangle3D(a, c, b, col);  DrawTriangle3D(a, d, c, col);
}

// A box centred on p, with each face shaded differently to fake lighting.
static void draw_box(Vector3 p, Vector3 size, Color c)
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

// A soft dark patch on the floor under people and furniture.
static void shadow(float x, float z, float w, float d)
{
    Color c = { 0, 0, 0, night ? 70 : 38 };
    quad(v3(x - w/2, 0.01f, z - d/2), v3(x + w/2, 0.01f, z - d/2),
         v3(x + w/2, 0.01f, z + d/2), v3(x - w/2, 0.01f, z + d/2), c);
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
// People and furniture
// ------------------------------------------------------------------------

// A standing person. Nurses wear blue scrubs, doctors a white coat.
static void draw_staff(Vector3 p, bool doctor, bool walking)
{
    float step = walking ? sinf((float)GetTime() * 14.0f) * 0.06f : 0.0f;
    Color top  = doctor ? C_COAT : C_BRAND;
    Color legs = doctor ? C_TROUSERS : C_BRAND;

    shadow(p.x, p.z, 0.6f, 0.45f);
    draw_box(v3(p.x - 0.11f, 0.25f + step, p.z), v3(0.17f, 0.50f, 0.22f), legs);
    draw_box(v3(p.x + 0.11f, 0.25f - step, p.z), v3(0.17f, 0.50f, 0.22f), legs);
    draw_box(v3(p.x, 0.80f, p.z), v3(0.48f, 0.60f, 0.30f), top);
    if (doctor) draw_box(v3(p.x, 0.86f, p.z + 0.16f), v3(0.14f, 0.40f, 0.02f), C_TROUSERS); // shirt under the coat
    draw_box(v3(p.x, 1.27f, p.z), v3(0.32f, 0.32f, 0.32f), C_SKIN);
    draw_box(v3(p.x, 1.45f, p.z - 0.02f), v3(0.34f, 0.08f, 0.34f), C_HAIR);
}

// A patient lying in a bed, in a gown of their severity colour.
static void draw_lying_patient(float x, const Patient *p)
{
    draw_box(v3(x, 0.74f, BED_Z + 0.15f), v3(0.56f, 0.26f, 1.30f), severity_color(p->severity));
    draw_box(v3(x, 0.78f, BED_Z - 0.70f), v3(0.34f, 0.32f, 0.32f), C_SKIN);
    draw_box(v3(x, 0.78f, BED_Z - 0.86f), v3(0.36f, 0.34f, 0.06f), C_HAIR);
}

static void draw_bed(float x)
{
    shadow(x, BED_Z, 1.4f, 2.4f);
    draw_box(v3(x, 0.25f, BED_Z), v3(1.2f, 0.40f, 2.2f), C_BEDFRAME);                  // frame
    draw_box(v3(x, 0.53f, BED_Z + 0.05f), v3(1.1f, 0.16f, 2.0f), C_MATTRESS);         // mattress
    draw_box(v3(x, 0.66f, BED_Z - 0.75f), v3(0.8f, 0.12f, 0.35f), C_CARD);            // pillow
    draw_box(v3(x, 0.60f, BED_Z - 1.10f), v3(1.2f, 1.00f, 0.10f), C_BEDFRAME);        // headboard
}

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
    draw_box(v3(x, 1.48f, z - 0.10f), v3(0.34f, 0.08f, 0.34f), C_HAIR);
}

static void draw_room(void)
{
    // Floor: a checkerboard of tiles, ward in one colour and waiting room in another
    for (int ix = -8; ix < 8; ix++) {
        for (int iz = -5; iz < 6; iz++) {
            bool ward = iz < 1;
            bool dark = ((ix + iz) & 1) != 0;
            Color c = ward ? (dark ? (Color){ 214, 222, 232, 255 } : (Color){ 228, 234, 242, 255 })
                           : (dark ? (Color){ 205, 222, 214, 255 } : (Color){ 220, 234, 227, 255 });
            c = shade(c, 1.0f);
            quad(v3(ix, 0, iz), v3(ix + 1, 0, iz), v3(ix + 1, 0, iz + 1), v3(ix, 0, iz + 1), c);
        }
    }

    // Low walls, so the camera can see over them
    draw_box(v3(0, 0.7f, -5.1f), v3(16.4f, 1.4f, 0.2f), C_WALL);                      // back
    draw_box(v3(-8.1f, 0.7f, 0.5f), v3(0.2f, 1.4f, 11.4f), C_WALL);                   // left
    draw_box(v3( 8.1f, 0.7f, 0.5f), v3(0.2f, 1.4f, 11.4f), C_WALL);                   // right
    draw_box(v3(-5.0f, 0.35f, 1.0f), v3(6.0f, 0.7f, 0.15f), C_WALL);                  // ward / waiting divider,
    draw_box(v3( 5.0f, 0.35f, 1.0f), v3(6.0f, 0.7f, 0.15f), C_WALL);                  // with a gap in the middle

    // Nurses' station
    shadow(0, DESK_Z, 3.4f, 1.1f);
    draw_box(v3(0, 0.45f, DESK_Z), v3(3.2f, 0.9f, 0.8f), C_DESK);
    draw_box(v3(0, 0.93f, DESK_Z), v3(3.4f, 0.06f, 0.95f), shade(C_DESK, 1.15f));
    draw_box(v3(-0.8f, 1.15f, DESK_Z), v3(0.5f, 0.38f, 0.05f), C_INK);                // computer screens
    draw_box(v3( 0.8f, 1.15f, DESK_Z), v3(0.5f, 0.38f, 0.05f), C_INK);
}

// ------------------------------------------------------------------------
// Camera
// ------------------------------------------------------------------------

void view3d_update_camera(void)
{
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 d = GetMouseDelta();
        camYaw   -= d.x * 0.3f;
        camPitch += d.y * 0.3f;
        if (camPitch < 12.0f) camPitch = 12.0f;
        if (camPitch > 85.0f) camPitch = 85.0f;
    }
    camDist -= GetMouseWheelMove() * 1.2f;
    if (camDist < 7.0f)  camDist = 7.0f;
    if (camDist > 30.0f) camDist = 30.0f;
}

static Camera3D make_camera(void)
{
    float yaw = camYaw * DEG2RAD, pitch = camPitch * DEG2RAD;
    Vector3 target = v3(0, 0.4f, 0.2f);
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
    DrawText("3D VIEW", 20, 46, 16, soft);

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

    DrawText("V 2D view   Drag to turn   Scroll to zoom   SPACE pause   UP/DOWN speed   E emergency",
             20, SCREEN_H - 30, 14, soft);
}

// ------------------------------------------------------------------------
// The frame
// ------------------------------------------------------------------------

void view3d_draw(const Sim *sim, float speed, bool paused)
{
    night = (sim->shift == SHIFT_NIGHT);
    Camera3D cam = make_camera();

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
        draw_room();
        for (int i = 0; i < BED_COUNT; i++) {
            draw_bed(bed_x(i));
            if (sim->beds[i].occupant) draw_lying_patient(bed_x(i), sim->beds[i].occupant);
        }

        // Waiting room: one chair per seat, filled most urgent first
        const Patient *p = sim->waiting;
        for (int r = 0; r < CHAIR_ROWS; r++) {
            for (int c = 0; c < CHAIRS_PER_ROW; c++) {
                float x = -5.4f + c * CHAIR_SPACING, z = 2.6f + r * 1.7f;
                draw_chair(x, z, p);
                if (p) p = p->next;
            }
        }

        for (int k = 0; k < NURSES_ON(sim->shift); k++) {
            bool walking = is_walking(nursePos[k], nurseTarget[k]);
            nursePos[k] = walk_towards(nursePos[k], nurseTarget[k]);
            draw_staff(nursePos[k], false, walking);
        }
        for (int k = 0; k < DOCTORS_ON(sim->shift); k++) {
            bool walking = is_walking(doctorPos[k], doctorTarget[k]);
            doctorPos[k] = walk_towards(doctorPos[k], doctorTarget[k]);
            draw_staff(doctorPos[k], true, walking);
        }
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
        label(TextFormat("+%d more waiting", sim->waitingCount - seats), v3(0, 0.1f, 6.0f),
              cam, 16, WHITE, C_EMERGENCY);
    }

    hud(sim, speed, paused);
    if (hoverBed != -1 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) bed_tooltip(sim, hoverBed);
}
