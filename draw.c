// draw.c - waiting room, ward, doctors and statistics panel.

#include <stddef.h>
#include "raylib.h"
#include "draw.h"

static Color severity_color(int severity)
{
    if (severity == 1) return RED;
    if (severity == 2) return ORANGE;
    return GREEN;
}

static void draw_waiting_room(const Sim *sim)
{
    const int x0 = 40, y0 = 110, step = 34, perRow = 16, maxShown = 48;

    DrawText("WAITING ROOM", x0, 60, 20, DARKGRAY);
    DrawRectangleLines(x0 - 15, y0 - 25, perRow * step + 15, 3 * step + 15, LIGHTGRAY);

    int i = 0;
    for (const Patient *p = sim->waiting; p != NULL && i < maxShown; p = p->next, i++) {
        int cx = x0 + (i % perRow) * step;
        int cy = y0 + (i / perRow) * step;
        DrawCircle(cx, cy, 13, severity_color(p->severity));
        DrawText(TextFormat("%d", p->id), cx - 8, cy - 5, 10, WHITE);
    }

    if (sim->waitingCount > maxShown) {
        DrawText(TextFormat("+%d more", sim->waitingCount - maxShown),
                 x0, y0 + 3 * step, 16, MAROON);
    }
}

static void draw_beds(const Sim *sim)
{
    const int x0 = 40, y0 = 300, w = 80, h = 130, gap = 16;

    DrawText("WARD", x0, y0 - 35, 20, DARKGRAY);

    for (int i = 0; i < BED_COUNT; i++) {
        int x = x0 + i * (w + gap);
        const Patient *p = sim->beds[i].occupant;

        DrawRectangle(x, y0, w, h, (Color){ 235, 240, 246, 255 });
        DrawRectangleLines(x, y0, w, h, GRAY);
        DrawText(TextFormat("Bed %d", i + 1), x + 8, y0 + 8, 14, DARKGRAY);

        if (p == NULL) {
            DrawText("free", x + 24, y0 + 60, 14, LIGHTGRAY);
            continue;
        }

        DrawCircle(x + w / 2, y0 + 62, 20, severity_color(p->severity));
        DrawText(TextFormat("%d", p->id), x + w / 2 - 8, y0 + 56, 12, WHITE);

        // Who is treating them. "no doctor" means the progress bar is stalled.
        int d = sim_doctor_at(sim, i);
        if (d != -1) DrawText(TextFormat("Dr %d", d + 1), x + 8, y0 + 92, 12, DARKBLUE);
        else         DrawText("no doctor",                x + 8, y0 + 92, 12, MAROON);

        // Treatment progress bar
        float done = 1.0f - p->treatmentLeft / p->treatmentTotal;
        DrawRectangle(x + 8, y0 + h - 20, w - 16, 10, LIGHTGRAY);
        DrawRectangle(x + 8, y0 + h - 20, (int)((w - 16) * done), 10, DARKBLUE);
    }
}

static void draw_doctors(const Sim *sim)
{
    const int x0 = 40, y0 = 480, w = 80, h = 40, gap = 16;

    DrawText("DOCTORS", x0, y0 - 28, 20, DARKGRAY);

    for (int d = 0; d < DOCTOR_COUNT; d++) {
        int x = x0 + d * (w + gap);
        int bed = sim->doctors[d].busyWithBed;
        bool busy = (bed != -1);

        DrawRectangle(x, y0, w, h, busy ? DARKBLUE : (Color){ 235, 240, 246, 255 });
        DrawRectangleLines(x, y0, w, h, GRAY);
        DrawText(TextFormat("Dr %d", d + 1), x + 8, y0 + 6, 14, busy ? WHITE : DARKGRAY);
        DrawText(busy ? TextFormat("Bed %d", bed + 1) : "free",
                 x + 8, y0 + 22, 12, busy ? WHITE : LIGHTGRAY);
    }
}

static void draw_stats(const Sim *sim, float speed, bool paused)
{
    const int x = 680, y = 60, line = 30;
    int inBeds = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) inBeds++;
    }
    int busyDocs = 0;
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed != -1) busyDocs++;
    }
    float avgWait = (sim->admitted > 0) ? sim->totalWait / (float)sim->admitted : 0.0f;

    DrawRectangle(x - 20, y - 20, 300, 400, (Color){ 245, 247, 250, 255 });
    DrawRectangleLines(x - 20, y - 20, 300, 400, LIGHTGRAY);

    DrawText("STATISTICS", x, y, 20, DARKGRAY);
    DrawText(TextFormat("Clock:      %.0f s", sim->clock),            x, y + 1 * line + 10, 18, BLACK);
    DrawText(TextFormat("Waiting:    %d", sim->waitingCount),         x, y + 2 * line + 10, 18, BLACK);
    DrawText(TextFormat("Beds used:  %d / %d", inBeds, BED_COUNT),    x, y + 3 * line + 10, 18, BLACK);
    DrawText(TextFormat("Treated:    %d", sim->treated),              x, y + 4 * line + 10, 18, BLACK);
    DrawText(TextFormat("Avg wait:   %.1f s", avgWait),               x, y + 5 * line + 10, 18, BLACK);
    DrawText(TextFormat("Doctors:    %d / %d", busyDocs, DOCTOR_COUNT), x, y + 6 * line + 10, 18, BLACK);
    DrawText(TextFormat("Speed:      %.1fx", speed),                  x, y + 7 * line + 10, 18, BLACK);

    DrawCircle(x + 8, y + 265, 8, RED);     DrawText("Emergency", x + 24, y + 258, 16, DARKGRAY);
    DrawCircle(x + 8, y + 290, 8, ORANGE);  DrawText("Urgent",    x + 24, y + 283, 16, DARKGRAY);
    DrawCircle(x + 8, y + 315, 8, GREEN);   DrawText("Routine",   x + 24, y + 308, 16, DARKGRAY);

    if (paused) DrawText("PAUSED", x, y + 345, 22, MAROON);
}

void draw_frame(const Sim *sim, float speed, bool paused)
{
    ClearBackground(RAYWHITE);

    DrawText("Hospital Resource Simulator", 40, 18, 26, DARKBLUE);
    draw_waiting_room(sim);
    draw_beds(sim);
    draw_doctors(sim);
    draw_stats(sim, speed, paused);
    DrawText("SPACE pause   UP/DOWN speed   E emergency   R reset",
             40, SCREEN_H - 40, 16, GRAY);
}