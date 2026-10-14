// report.c - end-of-shift report: what happened during the shift, and how
// busy the beds and doctors were. After the night shift it also shows
// totals for the whole day.

#include <stddef.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "report.h"

// Two columns of label/value rows.
#define LEFT_LABEL   130
#define LEFT_VALUE   470     // values are right-aligned to this x
#define RIGHT_LABEL  540
#define RIGHT_VALUE  870
#define ROW          28

static Rectangle continue_button(void)
{
    return (Rectangle){ SCREEN_W / 2 - 120, 470, 240, 50 };
}

bool report_new_shift_clicked(void)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), continue_button());
}

static void heading(const char *text, int x, int y)
{
    DrawText(text, x, y, 16, C_BRAND);
}

static void row(const char *label, const char *value, int xLabel, int xValue, int y)
{
    DrawText(label, xLabel, y, 18, C_MUTED);
    DrawText(value, xValue - MeasureText(value, 18), y, 18, C_INK);
}

// A severity row with a coloured dot, e.g. "(o) emergency ..... 29".
static void severity_row(const char *label, int count, Color dot, int y)
{
    DrawCircle(LEFT_LABEL + 20, y + 8, 6, dot);
    row(label, TextFormat("%d", count), LEFT_LABEL + 34, LEFT_VALUE, y);
}

static float average_wait(const ShiftStats *a, const ShiftStats *b)
{
    int admitted = a->admitted + (b ? b->admitted : 0);
    float total  = a->totalWait + (b ? b->totalWait : 0.0f);
    return (admitted > 0) ? total / (float)admitted : 0.0f;
}

static void centered(const char *text, int y, int size, Color color)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, color);
}

void draw_shift_report(const Sim *sim)
{
    const ShiftStats *s = &sim->stats;
    bool night = (sim->shift == SHIFT_NIGHT);

    ClearBackground(C_BG);
    Rectangle card = { 100, 30, 800, 545 };
    DrawRectangleRounded(card, 0.05f, 8, C_CARD);
    DrawRectangleRoundedLines(card, 0.05f, 8, C_EDGE);

    // Title
    centered(night ? "NIGHT SHIFT COMPLETE" : "DAY SHIFT COMPLETE", 55, 30, C_BRAND);
    int from = night ? DAY_START_HOUR + SHIFT_HOURS : DAY_START_HOUR;
    centered(TextFormat("Day %d   %02d:00 to %02d:00", sim->day, from % 24,
                        (from + SHIFT_HOURS) % 24), 92, 18, C_MUTED);

    // Left column: patients
    int inBeds = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) inBeds++;
    }
    int y = 135;
    heading("PATIENTS", LEFT_LABEL, y);                                                  y += 26;
    row("Arrived", TextFormat("%d", s->arrived), LEFT_LABEL, LEFT_VALUE, y);              y += ROW;
    row("Treated", TextFormat("%d", s->treated), LEFT_LABEL, LEFT_VALUE, y);              y += ROW;
    severity_row("emergency", s->treatedBySeverity[0], C_EMERGENCY, y);                   y += ROW;
    severity_row("urgent",    s->treatedBySeverity[1], C_URGENT,    y);                   y += ROW;
    severity_row("routine",   s->treatedBySeverity[2], C_ROUTINE,   y);                   y += ROW;
    row(night ? "Waiting at end of day" : "Waiting at handover",
        TextFormat("%d", sim->waitingCount), LEFT_LABEL, LEFT_VALUE, y);                  y += ROW;
    row("Still in a bed", TextFormat("%d", inBeds), LEFT_LABEL, LEFT_VALUE, y);

    // Right column: waiting times and resource use
    y = 135;
    heading("WAITING FOR A BED", RIGHT_LABEL, y);                                         y += 26;
    row("Average wait", TextFormat("%.0f min", sim_minutes(average_wait(s, NULL))),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row("Longest wait", TextFormat("%.0f min", sim_minutes(s->longestWait)),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 40;

    float bedUse = 100.0f * s->bedBusyTime    / (BED_COUNT    * SHIFT_LENGTH);
    float docUse = 100.0f * s->doctorBusyTime / (DOCTOR_COUNT * SHIFT_LENGTH);
    heading("RESOURCES", RIGHT_LABEL, y);                                                 y += 26;
    row(TextFormat("Beds in use (%d)", BED_COUNT), TextFormat("%.0f%%", bedUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row(TextFormat("Doctors busy (%d)", DOCTOR_COUNT), TextFormat("%.0f%%", docUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 40;

    // After the night shift: totals for the whole day
    if (night) {
        const ShiftStats *d = &sim->dayStats;
        heading("WHOLE DAY", RIGHT_LABEL, y);                                             y += 26;
        row("Arrived", TextFormat("%d", d->arrived + s->arrived),
            RIGHT_LABEL, RIGHT_VALUE, y);                                                 y += ROW;
        row("Treated", TextFormat("%d", d->treated + s->treated),
            RIGHT_LABEL, RIGHT_VALUE, y);                                                 y += ROW;
        row("Average wait", TextFormat("%.0f min", sim_minutes(average_wait(d, s))),
            RIGHT_LABEL, RIGHT_VALUE, y);
    }

    // Continue button
    Rectangle b = continue_button();
    bool hover = CheckCollisionPointRec(GetMousePosition(), b);
    DrawRectangleRounded(b, 0.4f, 8, hover ? C_INK : C_BRAND);
    const char *label = night ? TextFormat("Start day %d", sim->day + 1) : "Start night shift";
    DrawText(label, (int)(b.x + b.width / 2 - MeasureText(label, 20) / 2),
             (int)(b.y + 15), 20, WHITE);
    centered("or press N", 530, 14, C_MUTED);
}