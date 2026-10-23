// draw.c - waiting room, ward, rooms after A&E, nurses and statistics panel.
//
// The colours follow the shift: light by day, dark for the night shift.

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raylib.h"
#include "draw.h"
#include "minigames.h"        // the games ready to play

// ---- Colour theme: day or night ----
typedef struct {
    Color bg, card, edge, light, ink, muted, title, alert, doctorBusy;
} Theme;

static const Theme DAY_THEME = {
    C_BG, C_CARD, C_EDGE, C_LIGHT, C_INK, C_MUTED, C_BRAND, MAROON, C_INK
};
static const Theme NIGHT_THEME = {
    { 16, 22, 36, 255 },    // background
    { 30, 38, 56, 255 },    // cards
    { 52, 62, 84, 255 },    // card borders
    { 90, 102, 128, 255 },  // empty bars, "free"
    { 225, 232, 245, 255 }, // main text
    { 150, 165, 190, 255 }, // labels, hints
    { 140, 180, 240, 255 }, // title
    { 255, 110, 120, 255 }, // warnings
    { 88, 100, 135, 255 },  // a busy doctor's card
};

static Theme t;     // the colours for this frame

static Color severity_color(int severity)
{
    if (severity == 1) return C_EMERGENCY;
    if (severity == 2) return C_URGENT;
    return C_ROUTINE;
}

static void draw_text_centered(const char *text, int cx, int cy, int size, int maxWidth, Color color)
{   // Text centred on (cx, cy), Shrinks font till fits in maxWidth
    while (size > 8 && MeasureText(text, size) > maxWidth) size--;
    DrawText(text, cx - MeasureText(text, size) / 2, cy - size / 2, size, color);
}

// A patient's circle. Helicopter rescues get a white ring round them.
static void draw_patient_dot(const Patient *p, int cx, int cy, int radius, int textSize)
{
    if (p->rescue) DrawCircle(cx, cy, (float)radius + 3, WHITE);
    if (p->rescue) DrawCircleLines(cx, cy, (float)radius + 3, C_EMERGENCY);
    DrawCircle(cx, cy, (float)radius, severity_color(p->severity));
    draw_text_centered(TextFormat("%d", p->id), cx, cy, textSize, 2 * radius - 4, WHITE);
}

static void draw_waiting_room(const Sim *sim)
{
    // Two rows of patients, so the rooms after A&E fit underneath
    const int x0 = 40, y0 = 110, step = 34, perRow = 16, rows = 2, maxShown = perRow * rows;

    DrawText("WAITING ROOM", x0, 60, 20, t.ink);
    int flying = sim_inbound_count(sim);
    if (flying > 0) DrawText(TextFormat("+%d coming by helicopter", flying), x0 + 175, 64, 14, C_EMERGENCY);
    Rectangle room = { x0 - 15, y0 - 25, perRow * step + 15, rows * step + 15 };
    DrawRectangleRounded(room, 0.15f, 8, t.card);
    DrawRectangleRoundedLines(room, 0.15f, 8, t.edge);

    //DrawRectangleLines(x0 - 15, y0 - 25, perRow * step + 15, 3 * step + 15, C_LIGHT);

    int i = 0;
    for (const Patient *p = sim->waiting; p != NULL && i < maxShown; p = p->next, i++) {
        int cx = x0 + (i % perRow) * step;
        int cy = y0 + (i / perRow) * step;
        draw_patient_dot(p, cx, cy, 13, 12);
    }

    if (sim->waitingCount > maxShown) {
        DrawText(TextFormat("+%d more", sim->waitingCount - maxShown),
                 (int)(room.x + room.width) - 80, 62, 16, t.alert);
    }
}

Color draw_equipment_colour(int e)
{
    static const Color colours[EQUIP_TYPES] = {
        [EQUIP_MONITOR]    = {  40, 170, 160, 255 },   // teal
        [EQUIP_VENTILATOR] = {  70, 120, 225, 255 },   // blue
        [EQUIP_DEFIB]      = { 235, 100,  50, 255 },   // orange-red
        [EQUIP_DRIP]       = { 155, 100, 205, 255 },   // purple
        [EQUIP_WHEELCHAIR] = { 120, 110,  95, 255 },   // brown-grey
    };
    return (e >= 0 && e < EQUIP_TYPES) ? colours[e] : GRAY;
}

// The rooms patients go to after A&E: who is in each, and how many are
// queuing. Two rows of seven; the small number in the corner is the floor
// the room is on (G ground, 1 first, 2 second).
// Where each room's card is, in the two rows under the waiting room
static Rectangle room_card(int r)
{
    const int x0 = 25, y0 = 190, w = 76, h = 36, gap = 5, perRow = 7;
    return (Rectangle){ (float)(x0 + (r % perRow) * (w + gap)), (float)(y0 + (r / perRow) * (h + 4)), (float)w, (float)h };
}

// ...and each A&E bed's card, in a row
#define BED_CARD_X0 40
#define BED_CARD_Y0 306
#define BED_CARD_W  62
#define BED_CARD_H  130
#define BED_CARD_GAP 6
static Rectangle bed_card(int i)
{
    return (Rectangle){ (float)(BED_CARD_X0 + i * (BED_CARD_W + BED_CARD_GAP)), BED_CARD_Y0, BED_CARD_W, BED_CARD_H };
}

static void draw_rooms(const Sim *sim)
{
    DrawText("AFTER A&E", 40, 174, 14, t.muted);
    for (int r = 0; r < ROOM_TYPES; r++) {
        const SimRoom *room = &sim->rooms[r];
        Rectangle card = room_card(r);
        int x = (int)card.x, y = (int)card.y, w = (int)card.width, h = (int)card.height;
        DrawRectangleRounded(card, 0.25f, 8, t.card);
        DrawRectangleRoundedLines(card, 0.25f, 8, t.edge);
        int fl = sim_room_floor(r);
        DrawText(fl == 0 ? "G" : TextFormat("%d", fl), x + w - 11, y + h - 13, 10, t.light);
        const char *name = room->name;
        int size = 12;
        while (size > 8 && MeasureText(name, size) > w - 30) size--;
        DrawText(name, x + 6, y + 4, size, t.ink);

        // A small dot for each place: filled in the patient's colour, or empty
        for (int s = 0; s < room->capacity; s++) {
            int cx = x + 11 + s * 14, cy = y + 25;
            const Patient *p = room->in[s];
            if (p) DrawCircle(cx, cy, 5, severity_color(p->severity));
            else   DrawCircleLines(cx, cy, 5, t.light);
        }
        if (room->queueCount > 0) {
            const char *q = TextFormat("+%d", room->queueCount);
            DrawText(q, x + w - 5 - MeasureText(q, 12), y + 4, 12, t.alert);   // queuing
        }
    }
}

static void draw_beds(const Sim *sim)
{
    // 8 beds and the resus bay, narrower cards so they all fit in a row
    const int y0 = BED_CARD_Y0, w = BED_CARD_W, h = BED_CARD_H;

    DrawText("WARD", BED_CARD_X0, y0 - 30, 20, t.ink);

    for (int i = 0; i < BED_COUNT; i++) {
        int x = (int)bed_card(i).x;
        const Patient *p = sim->beds[i].occupant;
        bool resus = (i == RESUS_BED);

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.25f, 8, t.card);
        DrawRectangleRoundedLines(card, 0.25f, 8, resus ? C_EMERGENCY : t.edge);

        //DrawRectangle(x, y0, w, h, C_CARD);
        //DrawRectangleLines(x, y0, w, h, C_EDGE);
        if (resus) DrawText("Resus", x + 8, y0 + 8, 14, C_EMERGENCY);
        else       DrawText(TextFormat("Bed %d", i + 1), x + 8, y0 + 8, 14, t.ink);

        if (!sim_bed_open(sim, i)) {                 // closed for the day (setup screen)
            DrawRectangleRounded((Rectangle){ x + 4, y0 + 28, w - 8, h - 36 }, 0.2f, 6, t.edge);
            draw_text_centered("closed", x + w / 2, y0 + 66, 14, w - 8, t.muted);
            continue;
        }
        if (p == NULL) {
            draw_text_centered("free", x + w / 2, y0 + 66, 14, w - 8, t.light);
            continue;
        }

        draw_patient_dot(p, x + w / 2, y0 + 58, 18, 15);

        // The equipment they have with them: a small square in its colour,
        // or an outline for what they still need
        int nKit = 0;
        for (int e = 0; e < EQUIP_TYPES; e++) if ((p->equipHeld | p->equipNeeds) & EQUIP_BIT(e)) nKit++;
        int kx = x + w / 2 - (nKit * 10 - 2) / 2;
        for (int e = 0; e < EQUIP_TYPES; e++) {
            if (p->equipHeld & EQUIP_BIT(e))       { DrawRectangle(kx, y0 + 80, 8, 8, draw_equipment_colour(e)); kx += 10; }
            else if (p->equipNeeds & EQUIP_BIT(e)) { DrawRectangleLines(kx, y0 + 80, 8, 8, draw_equipment_colour(e)); kx += 10; }
        }

        // Who is with them. Red text means the progress bar is stalled.
        int n = sim_nurse_at(sim, i);
        int d = sim_doctor_at(sim, i);
        const char *who; Color col;
        if (p->walkLeft > 0.0f)  { who = "arriving";                         col = t.muted; }
        else if (p->doneInAE)    { who = "needs chair";                      col = t.alert; }
        else if (p->equipMissing) {
            int e = 0;
            while (e < EQUIP_TYPES - 1 && !(p->equipMissing & EQUIP_BIT(e))) e++;
            who = TextFormat("no %s", sim_equipment_short(e));               col = t.alert;
        }
        else if (d != -1)        { who = TextFormat("Dr %s", sim->doctors[d].name); col = t.ink; }
        else if (p->needsDoctor) { who = "needs Dr";                         col = t.alert; }
        else if (n != -1)        { who = sim->nurses[n].name;                col = C_BRAND; }
        else                     { who = "no nurse";                         col = t.alert; }
        draw_text_centered(who, x + w / 2, y0 + 98, 12, w - 6, col);

        // Treatment progress bar
        float done = 1.0f - p->treatmentLeft / p->treatmentTotal;
        DrawRectangle(x + 6, y0 + h - 20, w - 12, 10, t.light);
        DrawRectangle(x + 6, y0 + h - 20, (int)((w - 12) * done), 10, C_BRAND);
    }
}

// Text that fits in `width`: smaller (down to 10) if it has to be, then cut short
static void fitted_text(const char *text, int x, int y, int size, int width, Color c)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%s", text);      // (a copy: `text` may be TextFormat's, which gets reused)
    while (size > 10 && MeasureText(buf, size) > width) size--;
    size_t len = strlen(buf);
    while (len > 1 && MeasureText(buf, size) > width) buf[--len] = '\0';
    DrawText(buf, x, y, size, c);
}

static void draw_nurses(const Sim *sim)
{
    // The cards get narrower when there are more staff, so they fit in the row
    const int x0 = 40, y0 = 480, h = 40, gap = 6;
    int people = sim_nurses_on(sim) + sim_doctors_on(sim);
    int w = (600 - gap * (people - 1)) / people;
    if (w > 78) w = 78;

    DrawText("STAFF", x0, y0 - 28, 20, t.ink);

    for (int d = 0; d < sim_nurses_on(sim); d++) {
        int x = x0 + d * (w + gap);
        int bed = sim->nurses[d].busyWithBed;
        bool busy = (bed != -1);

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.4f, 8, busy ? C_BRAND : t.card);
        DrawRectangleRoundedLines(card, 0.4f, 8, t.edge);
        //DrawRectangle(x, y0, w, h, busy ? C_BRAND : C_CARD);
        //DrawRectangleLines(x, y0, w, h, C_EDGE);
        fitted_text(sim->nurses[d].name, x + 6, y0 + 6, 14, w - 10, busy ? WHITE : t.ink);
        fitted_text(busy ? (bed == RESUS_BED ? "Resus" : TextFormat("Bed %d", bed + 1)) : "free",
                    x + 6, y0 + 22, 12, w - 10, busy ? WHITE : t.light);
    }

    // Doctors go in the same row, after the nurses, in a darker colour (and
    // the on-call doctor, when they've been called in, with a red edge)
    for (int d = 0; d < sim_doctors_on(sim); d++) {
        int x = x0 + (sim_nurses_on(sim) + d) * (w + gap);
        int bed = sim->doctors[d].busyWithBed;
        bool busy = (bed != -1);

        Rectangle card = { x, y0, w, h };
        bool onCall = (d >= sim_shift_doctors(sim));
        DrawRectangleRounded(card, 0.4f, 8, busy ? t.doctorBusy : t.card);
        DrawRectangleRoundedLines(card, 0.4f, 8, onCall ? C_EMERGENCY : t.edge);
        // ("Dr Byrne", or just "Byrne" on a narrow card: doctors' cards are the dark ones)
        const char *name = (w >= 70) ? TextFormat("Dr %s", sim->doctors[d].name) : sim->doctors[d].name;
        fitted_text(name, x + 6, y0 + 6, 14, w - 10, busy ? WHITE : t.ink);
        const char *where = busy ? (bed == RESUS_BED ? "Resus" : TextFormat("Bed %d", bed + 1)) : "free";
        if (onCall && !busy) where = "on call";
        fitted_text(where, x + 6, y0 + 22, 12, w - 10, busy ? WHITE : (onCall ? C_EMERGENCY : t.light));
    }
}

static void draw_stats(const Sim *sim, float speed, bool paused)
{
    const int x = 680, y = 60, line = 23;
    int inBeds = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) inBeds++;
    }
    int busyNurses = 0;
    for (int d = 0; d < NURSE_MAX; d++) {
        if (sim->nurses[d].busyWithBed != -1) busyNurses++;
    }

    int busyDoctors = 0;
    for (int d = 0; d < DOCTOR_SLOTS; d++) {
        if (sim->doctors[d].busyWithBed != -1) busyDoctors++;
    }

    float avgWait = (sim->stats.admitted > 0) ? sim->stats.totalWait / (float)sim->stats.admitted : 0.0f;

    Rectangle panel = { x - 20, y - 20, 300, 450 };
    DrawRectangleRounded(panel, 0.06f, 8, t.card);
    DrawRectangleRoundedLines(panel, 0.06f, 8, t.light);

    //DrawRectangle(x - 20, y - 20, 300, 400, C_CARD);
    //DrawRectangleLines(x - 20, y - 20, 300, 400, C_LIGHT);

    DrawText("STATISTICS", x, y, 20, t.ink);
    int hh, mm;
    sim_time_of_day(sim, &hh, &mm);
    DrawText(TextFormat("%s:  %02d:%02d", sim->shift == SHIFT_DAY ? "Day shift" : "Night shift", hh, mm),
             x, y + 1 * line + 8, 16, t.ink);
    DrawText(TextFormat("Waiting:    %d", sim->waitingCount),         x, y + 2 * line + 8, 16, t.ink);
    DrawText(TextFormat("Beds used:  %d / %d", inBeds, BED_COUNT),    x, y + 3 * line + 8, 16, t.ink);
    DrawText(TextFormat("Treated:    %d", sim->stats.treated),              x, y + 4 * line + 8, 16, t.ink);
    DrawText(TextFormat("Avg wait:   %.1f s", avgWait),               x, y + 5 * line + 8, 16, t.ink);
    DrawText(TextFormat("Nurses:    %d / %d", busyNurses, sim_nurses_on(sim)), x, y + 6 * line + 8, 16, t.ink);

    DrawText(TextFormat("Doctors:    %d / %d%s", busyDoctors, sim_doctors_on(sim), sim->onCall ? "  (on call in)" : ""),
             x, y + 7 * line + 8, 16, sim->onCall ? t.alert : t.ink);
    DrawText(TextFormat("In rooms:   %d", sim_patients_in_rooms(sim)), x, y + 8 * line + 8, 16, t.ink);
    DrawText(TextFormat("Speed:      %.1fx", speed),                  x, y + 9 * line + 8, 16, t.ink);

    // Equipment: a bar for each kind, filled for the ones in use
    int ey = y + 10 * line + 22;
    DrawText("EQUIPMENT IN USE", x, ey, 14, t.muted);
    ey += 20;
    for (int e = 0; e < EQUIP_TYPES; e++) {
        const Equipment *q = &sim->equipment[e];
        int row = ey + e * 20;
        DrawRectangle(x, row + 2, 10, 10, draw_equipment_colour(e));
        DrawText(sim_equipment_short(e), x + 16, row, 14, t.ink);
        // one block per item
        for (int k = 0; k < q->total; k++) {
            Rectangle b = { (float)(x + 110 + k * 16), (float)(row + 1), 13, 12 };
            if (k < q->inUse) DrawRectangleRec(b, draw_equipment_colour(e));
            else              DrawRectangleLinesEx(b, 1, t.light);
        }
        const char *c = TextFormat("%d/%d", q->inUse, q->total);
        DrawText(c, x + 258 - MeasureText(c, 14), row, 14, q->inUse >= q->total ? t.alert : t.muted);
    }

    // The colours of the patients' dots, in one line
    int ly = y + 405;
    DrawCircle(x + 6, ly + 7, 6, C_EMERGENCY);  DrawText("Emergency", x + 16, ly, 14, t.ink);
    DrawCircle(x + 104, ly + 7, 6, C_URGENT);   DrawText("Urgent",    x + 114, ly, 14, t.ink);
    DrawCircle(x + 180, ly + 7, 6, C_ROUTINE);  DrawText("Routine",   x + 190, ly, 14, t.ink);

    if (paused) DrawText("PAUSED", x + 160, y, 20, t.alert);
}

// ---- Mini games ready to play ----
// A pulsing badge on the card of each room or bed where a patient is ready
// for a game (theatre, bloods, a bed with the defibrillator...), and the
// "GAMES READY" table under the statistics, listing them all.
static void draw_game_badges(const Sim *sim)
{
    MiniOffer offers[MINIGAME_COUNT];
    int n = minigames_offers(offers, MINIGAME_COUNT);
    for (int i = 0; i < n; i++) {
        int room, slot, bed;
        if (offers[i].game == MINIGAME_LANDING) {   // the helicopter: after "+1 coming by helicopter"
            const char *text = TextFormat("+%d coming by helicopter", sim_inbound_count(sim));
            minigames_draw_badge(offers[i].game, 215 + MeasureText(text, 14) + 16, 71);
            continue;
        }
        if (!minigames_patient_place(sim, offers[i].patientId, &room, &slot, &bed)) continue;
        Rectangle c = (bed >= 0) ? bed_card(bed) : room_card(room);
        // bottom-right corner of a room card; top-right corner of a bed card
        if (bed >= 0) minigames_draw_badge(offers[i].game, c.x + c.width - 6, c.y + 6);
        else          minigames_draw_badge(offers[i].game, c.x + c.width - 4, c.y + c.height - 4);
    }
    minigames_draw_table(660, 494, 300, 2, t.card, t.ink, t.muted);
}

// ---- Helicopter rescue banner ----
static int    rescueLastId = 0;     // highest patient id already checked
static double rescueUntil = 0.0;
static int    rescueService = 0;    // which service is bringing the latest one

// Each service's message and banner colour
static const char *SERVICE_MESSAGE[SERVICE_COUNT] = {
    "COAST GUARD RESCUE INBOUND",
    "SEARCH AND RESCUE INBOUND",
    "FIRE SERVICE HELICOPTER INBOUND",
};
static const Color SERVICE_COLOUR[SERVICE_COUNT] = {
    {  40,  90, 190, 255 },     // coast guard: blue
    { 210,  45,  45, 255 },     // search and rescue: red
    { 235, 110,  30, 255 },     // fire service: orange
};

bool draw_rescue_banner(const Sim *sim, int centreX, int y)
{
    int newest = sim->nextId - 1;
    if (newest < rescueLastId) rescueLastId = newest;     // the day was restarted
    bool arrived = false;
    for (const Patient *p = sim->waiting; p != NULL; p = p->next) {
        if (p->id > rescueLastId && p->rescue) { arrived = true; rescueService = p->rescueService; }
    }
    for (int b = 0; b < BED_COUNT; b++) {
        const Patient *p = sim->beds[b].occupant;
        if (p && p->id > rescueLastId && p->rescue) { arrived = true; rescueService = p->rescueService; }
    }
    for (const Patient *p = sim->inbound; p != NULL; p = p->next) {      // called in: on its way
        if (p->id > rescueLastId) { arrived = true; rescueService = p->rescueService; }
    }
    rescueLastId = newest;
    if (arrived) rescueUntil = GetTime() + 4.0;

    if (GetTime() >= rescueUntil) return arrived;
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 8.0f);
    const char *text = SERVICE_MESSAGE[rescueService];
    int w = MeasureText(text, 18) + 30;
    Color bg = SERVICE_COLOUR[rescueService];
    bg.a = (unsigned char)(180 + 75 * pulse);
    DrawRectangleRounded((Rectangle){ (float)(centreX - w / 2), (float)y, (float)w, 30 }, 0.5f, 8, bg);
    DrawText(text, centreX - MeasureText(text, 18) / 2, y + 6, 18, WHITE);
    return arrived;
}

// ---- Power cuts and major incidents ----

void draw_power_overlay(const Sim *sim, float w, float h, bool lamps)
{
    float lights = sim_lights(sim);
    if (lights >= 1.0f) return;
    float dark = 1.0f - lights;
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 3.0f);
    DrawRectangle(0, 0, (int)w, (int)h, (Color){ 0, 0, 8, (unsigned char)(165 * dark) });          // the lights are out
    DrawRectangle(0, 0, (int)w, (int)h, (Color){ 150, 0, 0, (unsigned char)((25 + 25 * pulse) * dark) });  // red emergency lights
    if (!lamps) return;
    // The emergency lamps themselves, high up on the walls
    for (int k = 0; k < 2; k++) {
        float x = (k == 0) ? 30.0f : w - 30.0f;
        for (int r = 6; r >= 1; r--)                 // a soft glow: rings, fainter further out
            DrawCircle((int)x, 20, r * (12 + 2 * pulse), (Color){ 255, 60, 40, (unsigned char)(22 * dark) });
        DrawCircle((int)x, 20, 7, (Color){ 255, 120, 90, (unsigned char)(255 * dark) });
    }
}

void draw_power_status(const Sim *sim, int centreX, int y)
{
    const char *line1 = NULL, *line2 = NULL;
    Color bg = { 140, 20, 24, 235 };
    if (sim->power == POWER_OUT) {
        line1 = "POWER CUT  -  emergency lighting";
        if (sim->generatorStuck)                         line2 = "The generator won't start!";
        else if (sim->powerTimer > GENERATOR_START + 0.01f)
            line2 = TextFormat("Electricians on the way: about %.0f min", sim_minutes(sim->powerTimer));
        else                                             line2 = "Backup generator starting...";
    } else if (sim->powerBackAt >= 0.0f && sim->clock - sim->powerBackAt < 4.0f) {
        line1 = "POWER BACK  -  running on the generator";
        bg = (Color){ 40, 120, 70, 235 };
    }
    if (line1 == NULL) return;
    int w = MeasureText(line1, 22) + 40;
    if (line2 && MeasureText(line2, 16) + 40 > w) w = MeasureText(line2, 16) + 40;
    int h = line2 ? 64 : 38;
    DrawRectangleRounded((Rectangle){ (float)(centreX - w / 2), (float)y, (float)w, (float)h }, 0.3f, 8, bg);
    DrawText(line1, centreX - MeasureText(line1, 22) / 2, y + 8, 22, WHITE);
    if (line2) DrawText(line2, centreX - MeasureText(line2, 16) / 2, y + 38, 16, (Color){ 255, 220, 210, 255 });
}

void draw_incident_banner(const Sim *sim, int centreX, int y)
{
    if (sim->incidentSince < 0.0f) return;
    float since = sim->clock - sim->incidentSince;
    if (since < 0.0f || (sim->incidentLeft == 0 && since > 20.0f)) return;      // shown while they arrive, and a bit after
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 6.0f);
    const char *text = sim->incidentLeft > 0
        ? TextFormat("MAJOR INCIDENT: %d more casualties coming", sim->incidentLeft)
        : "MAJOR INCIDENT: all the casualties are in";
    int w = MeasureText(text, 16) + 30;
    DrawRectangleRounded((Rectangle){ (float)(centreX - w / 2), (float)y, (float)w, 26 }, 0.5f, 8,
                         (Color){ 200, 40, 40, (unsigned char)(190 + 65 * pulse) });
    DrawText(text, centreX - MeasureText(text, 16) / 2, y + 5, 16, WHITE);
}

void draw_frame(const Sim *sim, float speed, bool paused)
{
    t = (sim->shift == SHIFT_NIGHT) ? NIGHT_THEME : DAY_THEME;
    ClearBackground(t.bg);

    DrawText("Hospital Resource Simulator", 40, 18, 26, t.title);
    draw_waiting_room(sim);
    draw_rooms(sim);
    draw_beds(sim);
    draw_nurses(sim);
    draw_stats(sim, speed, paused);
    draw_game_badges(sim);
    DrawText("V 3D view   SPACE pause   UP/DOWN speed   E emergency   H helicopter   C power cut   G games (or O S X P)   M mute   R restart",
             40, SCREEN_H - 38, 14, t.muted);
    draw_rescue_banner(sim, 810, 6);
    draw_incident_banner(sim, 400, 138);               // in the waiting room, where they arrive
    draw_power_overlay(sim, SCREEN_W, SCREEN_H, true);  // dark, in a power cut
    draw_power_status(sim, SCREEN_W / 2, 250);
}
