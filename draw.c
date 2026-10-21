// draw.c - waiting room, ward, rooms after A&E, nurses and statistics panel.
//
// The colours follow the shift: light by day, dark for the night shift.

#include <stddef.h>
#include <math.h>
#include "raylib.h"
#include "draw.h"

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

// The rooms patients go to after A&E: who is in each, and how many are queuing.
static void draw_rooms(const Sim *sim)
{
    const int x0 = 25, y0 = 196, w = 104, h = 52, gap = 9;

    DrawText("AFTER A&E", x0 + 15, y0 - 20, 14, t.muted);
    for (int r = 0; r < ROOM_TYPES; r++) {
        const SimRoom *room = &sim->rooms[r];
        int x = x0 + r * (w + gap);
        Rectangle card = { (float)x, (float)y0, (float)w, (float)h };
        DrawRectangleRounded(card, 0.25f, 8, t.card);
        DrawRectangleRoundedLines(card, 0.25f, 8, t.edge);
        DrawText(room->name, x + 8, y0 + 6, 14, t.ink);

        // A small dot for each place: filled in the patient's colour, or empty
        for (int s = 0; s < room->capacity; s++) {
            int cx = x + 14 + s * 20, cy = y0 + 34;
            const Patient *p = room->in[s];
            if (p) DrawCircle(cx, cy, 7, severity_color(p->severity));
            else   DrawCircleLines(cx, cy, 7, t.light);
        }
        if (room->queueCount > 0) {
            const char *q = TextFormat("+%d", room->queueCount);
            DrawText(q, x + w - 8 - MeasureText(q, 14), y0 + 6, 14, t.alert);   // queuing
        }
    }
}

static void draw_beds(const Sim *sim)
{
    const int x0 = 40, y0 = 300, w = 80, h = 130, gap = 16;

    DrawText("WARD", x0, y0 - 35, 20, t.ink);

    for (int i = 0; i < BED_COUNT; i++) {
        int x = x0 + i * (w + gap);
        const Patient *p = sim->beds[i].occupant;

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.25f, 8, t.card);
        DrawRectangleRoundedLines(card, 0.25f, 8, t.edge);

        //DrawRectangle(x, y0, w, h, C_CARD);
        //DrawRectangleLines(x, y0, w, h, C_EDGE);
        DrawText(TextFormat("Bed %d", i + 1), x + 8, y0 + 8, 14, t.ink);

        if (p == NULL) {
            DrawText("free", x + 24, y0 + 60, 14, t.light);
            continue;
        }

        draw_patient_dot(p, x + w / 2, y0 + 62, 20, 16);

        // Who is with them. Red text means the progress bar is stalled.
        int n = sim_nurse_at(sim, i);
        int d = sim_doctor_at(sim, i);
        if (d != -1)             DrawText(TextFormat("Dr %s", sim->doctors[d].name), x + 8, y0 + 92, 12, t.ink);
        else if (p->needsDoctor) DrawText("needs Dr",                    x + 8, y0 + 92, 12, t.alert);
        else if (n != -1)        DrawText(sim->nurses[n].name,           x + 8, y0 + 92, 12, C_BRAND);
        else                     DrawText("no nurse",                    x + 8, y0 + 92, 12, t.alert);

        // Treatment progress bar
        float done = 1.0f - p->treatmentLeft / p->treatmentTotal;
        DrawRectangle(x + 8, y0 + h - 20, w - 16, 10, t.light);
        DrawRectangle(x + 8, y0 + h - 20, (int)((w - 16) * done), 10, C_BRAND);
    }
}

static void draw_nurses(const Sim *sim)
{
    const int x0 = 40, y0 = 480, w = 80, h = 40, gap = 16;

    DrawText("STAFF", x0, y0 - 28, 20, t.ink);

    for (int d = 0; d < NURSES_ON(sim->shift); d++) {
        int x = x0 + d * (w + gap);
        int bed = sim->nurses[d].busyWithBed;
        bool busy = (bed != -1);

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.4f, 8, busy ? C_BRAND : t.card);
        DrawRectangleRoundedLines(card, 0.4f, 8, t.edge);
        //DrawRectangle(x, y0, w, h, busy ? C_BRAND : C_CARD);
        //DrawRectangleLines(x, y0, w, h, C_EDGE);
        DrawText(sim->nurses[d].name, x + 8, y0 + 6, 14, busy ? WHITE : t.ink);
        DrawText(busy ? TextFormat("Bed %d", bed + 1) : "free",
                 x + 8, y0 + 22, 12, busy ? WHITE : t.light);
    }

    // Doctors go in the same row, after the nurses, in a darker colour
    for (int d = 0; d < DOCTORS_ON(sim->shift); d++) {
        int x = x0 + (NURSES_ON(sim->shift) + d) * (w + gap);
        int bed = sim->doctors[d].busyWithBed;
        bool busy = (bed != -1);

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.4f, 8, busy ? t.doctorBusy : t.card);
        DrawRectangleRoundedLines(card, 0.4f, 8, t.edge);
        DrawText(TextFormat("Dr %s", sim->doctors[d].name), x + 8, y0 + 6, 14, busy ? WHITE : t.ink);
        DrawText(busy ? TextFormat("Bed %d", bed + 1) : "free",
                 x + 8, y0 + 22, 12, busy ? WHITE : t.light);
    }
}

static void draw_stats(const Sim *sim, float speed, bool paused)
{
    const int x = 680, y = 60, line = 30;
    int inBeds = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) inBeds++;
    }
    int busyNurses = 0;
    for (int d = 0; d < NURSE_COUNT; d++) {
        if (sim->nurses[d].busyWithBed != -1) busyNurses++;
    }

    int busyDoctors = 0;
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed != -1) busyDoctors++;
    }

    float avgWait = (sim->stats.admitted > 0) ? sim->stats.totalWait / (float)sim->stats.admitted : 0.0f;

    Rectangle panel = { x - 20, y - 20, 300, 430 };
    DrawRectangleRounded(panel, 0.06f, 8, t.card);
    DrawRectangleRoundedLines(panel, 0.06f, 8, t.light);

    //DrawRectangle(x - 20, y - 20, 300, 400, C_CARD);
    //DrawRectangleLines(x - 20, y - 20, 300, 400, C_LIGHT);

    DrawText("STATISTICS", x, y, 20, t.ink);
    int hh, mm;
    sim_time_of_day(sim, &hh, &mm);
    DrawText(TextFormat("%s:  %02d:%02d", sim->shift == SHIFT_DAY ? "Day shift" : "Night shift", hh, mm),
             x, y + 1 * line + 10, 18, t.ink);
    DrawText(TextFormat("Waiting:    %d", sim->waitingCount),         x, y + 2 * line + 10, 18, t.ink);
    DrawText(TextFormat("Beds used:  %d / %d", inBeds, BED_COUNT),    x, y + 3 * line + 10, 18, t.ink);
    DrawText(TextFormat("Treated:    %d", sim->stats.treated),              x, y + 4 * line + 10, 18, t.ink);
    DrawText(TextFormat("Avg wait:   %.1f s", avgWait),               x, y + 5 * line + 10, 18, t.ink);
    DrawText(TextFormat("Nurses:    %d / %d", busyNurses, NURSES_ON(sim->shift)), x, y + 6 * line + 10, 18, t.ink);

    DrawText(TextFormat("Doctors:    %d / %d", busyDoctors, DOCTORS_ON(sim->shift)), x, y + 7 * line + 10, 18, t.ink);
    DrawText(TextFormat("In rooms:   %d", sim_patients_in_rooms(sim)), x, y + 8 * line + 10, 18, t.ink);
    DrawText(TextFormat("Speed:      %.1fx", speed),                  x, y + 9 * line + 10, 18, t.ink);

    DrawCircle(x + 8, y + 325, 8, C_EMERGENCY);     DrawText("Emergency", x + 24, y + 318, 16, t.ink);
    DrawCircle(x + 8, y + 350, 8, C_URGENT);  DrawText("Urgent",    x + 24, y + 343, 16, t.ink);
    DrawCircle(x + 8, y + 375, 8, C_ROUTINE);   DrawText("Routine",   x + 24, y + 368, 16, t.ink);

    if (paused) DrawText("PAUSED", x + 160, y, 20, t.alert);
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
    DrawText("V 3D view   SPACE pause   UP/DOWN speed   E emergency   H helicopter   R reset",
             40, SCREEN_H - 40, 16, t.muted);
    draw_rescue_banner(sim, 810, 6);
}