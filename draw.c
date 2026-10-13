// draw.c - waiting room, ward, doctors and statistics panel.

#include <stddef.h>
#include "raylib.h"
#include "draw.h"

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

static void draw_waiting_room(const Sim *sim)
{
    const int x0 = 40, y0 = 110, step = 34, perRow = 16, maxShown = 48;

    DrawText("WAITING ROOM", x0, 60, 20, C_INK);
    Rectangle room = { x0 - 15, y0 - 25, perRow * step + 15, 3 * step + 15 };
    DrawRectangleRounded(room, 0.15f, 8, C_CARD);
    DrawRectangleRoundedLines(room, 0.15f, 8, C_EDGE);
    
    //DrawRectangleLines(x0 - 15, y0 - 25, perRow * step + 15, 3 * step + 15, C_LIGHT);

    int i = 0;
    for (const Patient *p = sim->waiting; p != NULL && i < maxShown; p = p->next, i++) {
        int cx = x0 + (i % perRow) * step;
        int cy = y0 + (i / perRow) * step;
        DrawCircle(cx, cy, 13, severity_color(p->severity));
        draw_text_centered(TextFormat("%d", p->id), cx, cy, 12, 22, WHITE);
    }

    if (sim->waitingCount > maxShown) {
        DrawText(TextFormat("+%d more", sim->waitingCount - maxShown),
                 x0, y0 + 3 * step, 16, MAROON);
    }
}

static void draw_beds(const Sim *sim)
{
    const int x0 = 40, y0 = 300, w = 80, h = 130, gap = 16;

    DrawText("WARD", x0, y0 - 35, 20, C_INK);

    for (int i = 0; i < BED_COUNT; i++) {
        int x = x0 + i * (w + gap);
        const Patient *p = sim->beds[i].occupant;

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.25f, 8, C_CARD);
        DrawRectangleRoundedLines(card, 0.25f, 8, C_EDGE);

        //DrawRectangle(x, y0, w, h, C_CARD);
        //DrawRectangleLines(x, y0, w, h, C_EDGE);
        DrawText(TextFormat("Bed %d", i + 1), x + 8, y0 + 8, 14, C_INK);

        if (p == NULL) {
            DrawText("free", x + 24, y0 + 60, 14, C_LIGHT);
            continue;
        }

        DrawCircle(x + w / 2, y0 + 62, 20, severity_color(p->severity));
        draw_text_centered(TextFormat("%d", p->id), x + w / 2, y0 + 62, 16, 34, WHITE);

        // Who is treating them. "no doctor" means the progress bar is stalled.
        int d = sim_doctor_at(sim, i);
        if (d != -1) DrawText(TextFormat("Dr %d", d + 1), x + 8, y0 + 92, 12, C_BRAND);
        else         DrawText("no doctor",                x + 8, y0 + 92, 12, MAROON);

        // Treatment progress bar
        float done = 1.0f - p->treatmentLeft / p->treatmentTotal;
        DrawRectangle(x + 8, y0 + h - 20, w - 16, 10, C_LIGHT);
        DrawRectangle(x + 8, y0 + h - 20, (int)((w - 16) * done), 10, C_BRAND);
    }
}

static void draw_doctors(const Sim *sim)
{
    const int x0 = 40, y0 = 480, w = 80, h = 40, gap = 16;

    DrawText("DOCTORS", x0, y0 - 28, 20, C_INK);

    for (int d = 0; d < DOCTOR_COUNT; d++) {
        int x = x0 + d * (w + gap);
        int bed = sim->doctors[d].busyWithBed;
        bool busy = (bed != -1);

        Rectangle card = { x, y0, w, h };
        DrawRectangleRounded(card, 0.4f, 8, busy ? C_BRAND : C_CARD);
        DrawRectangleRoundedLines(card, 0.4f, 8, C_EDGE);
        //DrawRectangle(x, y0, w, h, busy ? C_BRAND : C_CARD);
        //DrawRectangleLines(x, y0, w, h, C_EDGE);
        DrawText(TextFormat("Dr %d", d + 1), x + 8, y0 + 6, 14, busy ? WHITE : C_INK);
        DrawText(busy ? TextFormat("Bed %d", bed + 1) : "free",
                 x + 8, y0 + 22, 12, busy ? WHITE : C_LIGHT);
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

    Rectangle panel = { x - 20, y - 20, 300, 400 };
    DrawRectangleRounded(panel, 0.06f, 8, C_CARD);
    DrawRectangleRoundedLines(panel, 0.06f, 8, C_LIGHT);

    //DrawRectangle(x - 20, y - 20, 300, 400, C_CARD);
    //DrawRectangleLines(x - 20, y - 20, 300, 400, C_LIGHT);

    DrawText("STATISTICS", x, y, 20, C_INK);
    DrawText(TextFormat("Clock:      %.0f s", sim->clock),            x, y + 1 * line + 10, 18, C_INK);
    DrawText(TextFormat("Waiting:    %d", sim->waitingCount),         x, y + 2 * line + 10, 18, C_INK);
    DrawText(TextFormat("Beds used:  %d / %d", inBeds, BED_COUNT),    x, y + 3 * line + 10, 18, C_INK);
    DrawText(TextFormat("Treated:    %d", sim->treated),              x, y + 4 * line + 10, 18, C_INK);
    DrawText(TextFormat("Avg wait:   %.1f s", avgWait),               x, y + 5 * line + 10, 18, C_INK);
    DrawText(TextFormat("Doctors:    %d / %d", busyDocs, DOCTOR_COUNT), x, y + 6 * line + 10, 18, C_INK);
    DrawText(TextFormat("Speed:      %.1fx", speed),                  x, y + 7 * line + 10, 18, C_INK);

    DrawCircle(x + 8, y + 265, 8, C_EMERGENCY);     DrawText("Emergency", x + 24, y + 258, 16, C_INK);
    DrawCircle(x + 8, y + 290, 8, C_URGENT);  DrawText("Urgent",    x + 24, y + 283, 16, C_INK);
    DrawCircle(x + 8, y + 315, 8, C_ROUTINE);   DrawText("Routine",   x + 24, y + 308, 16, C_INK);

    if (paused) DrawText("PAUSED", x, y + 345, 22, MAROON);
}

void draw_frame(const Sim *sim, float speed, bool paused)
{
    ClearBackground(C_BG);

    DrawText("Hospital Resource Simulator", 40, 18, 26, C_BRAND);
    draw_waiting_room(sim);
    draw_beds(sim);
    draw_doctors(sim);
    draw_stats(sim, speed, paused);
    DrawText("SPACE pause   UP/DOWN speed   E emergency   R reset",
             40, SCREEN_H - 40, 16, C_MUTED);
}