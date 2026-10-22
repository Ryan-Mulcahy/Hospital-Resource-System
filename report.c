// report.c - end-of-shift report: what happened during the shift, and how
// busy the beds and nurses were. After the night shift it also shows
// totals for the whole day. The report links to a second page listing
// every patient treated during the shift (the treatment log), and a third
// showing how the equipment and the rooms after A&E were used.
 
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "report.h"
 
// Two columns of label/value rows.
#define LEFT_LABEL   130
#define LEFT_VALUE   470     // values are right-aligned to this x
#define RIGHT_LABEL  540
#define RIGHT_VALUE  870
#define ROW          26
 
// Report buttons: "Patients treated", "Equipment & rooms", and continue.
static Rectangle view_log_button(void)
{
    return (Rectangle){ SCREEN_W / 2 - 345, 484, 220, 50 };
}

static Rectangle equipment_button(void)
{
    return (Rectangle){ SCREEN_W / 2 - 110, 484, 220, 50 };
}
 
static Rectangle continue_button(void)
{
    return (Rectangle){ SCREEN_W / 2 + 125, 484, 220, 50 };
}
 
static bool clicked(Rectangle r)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), r);
}
 
bool report_new_shift_clicked(void) { return clicked(continue_button()); }
bool report_view_log_clicked(void)  { return clicked(view_log_button()); }
bool report_view_equipment_clicked(void) { return clicked(equipment_button()); }
 
// A rounded button with centred white text; darker while the mouse is over it.
static void button(Rectangle b, const char *label, Color colour)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), b);
    DrawRectangleRounded(b, 0.4f, 8, hover ? C_INK : colour);
    DrawText(label, (int)(b.x + b.width / 2 - MeasureText(label, 20) / 2),
             (int)(b.y + 15), 20, WHITE);
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
    Rectangle card = { 100, 25, 800, 555 };
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
    row("Still in a bed", TextFormat("%d", inBeds), LEFT_LABEL, LEFT_VALUE, y);           y += ROW;
    row("Sent to a doctor", TextFormat("%d", s->sawDoctor), LEFT_LABEL, LEFT_VALUE, y);    y += ROW;
    int tests = 0;                                     // every room after A&E except the ward
    for (int r = 0; r < ROOM_TYPES; r++) if (r != ROOM_WARD) tests += s->sentTo[r];
    row("Visits to rooms after A&E", TextFormat("%d", tests), LEFT_LABEL, LEFT_VALUE, y); y += ROW;
    row("Admitted to the ward", TextFormat("%d", s->sentTo[ROOM_WARD]), LEFT_LABEL, LEFT_VALUE, y); y += ROW;
    row("Helicopter rescues", TextFormat("%d", s->rescues), LEFT_LABEL, LEFT_VALUE, y);
    // Right column: waiting times and resource use
    y = 135;
    heading("WAITING FOR A BED", RIGHT_LABEL, y);                                         y += 26;
    row("Average wait", TextFormat("%.0f min", sim_minutes(average_wait(s, NULL))),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row("Longest wait", TextFormat("%.0f min", sim_minutes(s->longestWait)),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    // The operation game (O): operations you did on real patients, and how many went wrong
    int played = s->surgeriesPlayed + s->shocksPlayed + s->veinsPlayed;
    int lost = s->complications + s->shocksFailed + s->veinsFailed;
    row("Mini games won / played", TextFormat("%d / %d", played - lost, played),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 32;
 
    float bedUse = 100.0f * s->bedBusyTime    / (BED_COUNT    * SHIFT_LENGTH);
    float docUse = 100.0f * s->nurseBusyTime / (NURSES_ON(sim->shift) * SHIFT_LENGTH);
    heading("RESOURCES", RIGHT_LABEL, y);                                                 y += 26;
    row(TextFormat("Beds in use (%d)", BED_COUNT), TextFormat("%.0f%%", bedUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row(TextFormat("Nurses busy (%d)", NURSES_ON(sim->shift)), TextFormat("%.0f%%", docUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    float drUse = 100.0f * s->doctorBusyTime / (DOCTORS_ON(sim->shift) * SHIFT_LENGTH);
    row(TextFormat("Doctors busy (%d)", DOCTORS_ON(sim->shift)), TextFormat("%.0f%%", drUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row("On-call doctor called", s->onCallCalls ? TextFormat("%dx, %.0f min", s->onCallCalls, sim_minutes(s->onCallTime)) : "no",
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 32;
    
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
 
    // Buttons: view the treatment log, or continue to the next shift
    Rectangle v = view_log_button();
    Rectangle e = equipment_button();
    Rectangle b = continue_button();
    button(v, "Patients treated", C_MUTED);
    button(e, "Equipment & rooms", C_MUTED);
    button(b, "Continue", C_BRAND);
 
    const char *vHint = "or press L";
    const char *eHint = "or press E";
    const char *bHint = "or press N";
    DrawText(vHint, (int)(v.x + v.width / 2 - MeasureText(vHint, 14) / 2), 540, 14, C_MUTED);
    DrawText(eHint, (int)(e.x + e.width / 2 - MeasureText(eHint, 14) / 2), 540, 14, C_MUTED);
    DrawText(bHint, (int)(b.x + b.width / 2 - MeasureText(bHint, 14) / 2), 540, 14, C_MUTED);
}

// ---------------------------------------------------------------- equipment and rooms page

bool report_equipment_back_clicked(void)
{
    return clicked((Rectangle){ SCREEN_W / 2 - 120, 520, 240, 44 });
}

// A small bar for a percentage, red when it's nearly always busy.
static void percent_bar(int x, int y, int w, float pct)
{
    if (pct > 100) pct = 100;
    DrawRectangle(x, y, w, 10, C_EDGE);
    DrawRectangle(x, y, (int)(w * pct / 100.0f), 10, pct >= 85 ? C_EMERGENCY : pct >= 60 ? C_URGENT : C_ROUTINE);
}

void draw_equipment_report(const Sim *sim)
{
    const ShiftStats *s = &sim->stats;
    bool night = (sim->shift == SHIFT_NIGHT);

    ClearBackground(C_BG);
    Rectangle card = { 40, 20, 920, 560 };
    DrawRectangleRounded(card, 0.03f, 8, C_CARD);
    DrawRectangleRoundedLines(card, 0.03f, 8, C_EDGE);
    centered(TextFormat("EQUIPMENT AND ROOMS: %s SHIFT, DAY %d", night ? "NIGHT" : "DAY", sim->day), 38, 26, C_BRAND);
    centered("How busy each one was, and how long patients waited for it", 72, 16, C_MUTED);

    // Equipment: left
    int x = 70, y = 110;
    heading("EQUIPMENT", x, y);                                   y += 24;
    DrawText("Item",          x,       y, 14, C_MUTED);
    DrawText("Have",          x + 130, y, 14, C_MUTED);
    DrawText("Busy",          x + 175, y, 14, C_MUTED);
    DrawText("Most at once",  x + 285, y, 14, C_MUTED);
    y += 22;
    for (int e = 0; e < EQUIP_TYPES; e++) {
        const Equipment *q = &sim->equipment[e];
        float pct = 100.0f * s->equipBusyTime[e] / (q->total * SHIFT_LENGTH);
        DrawRectangle(x, y + 3, 10, 10, draw_equipment_colour(e));
        DrawText(sim_equipment_short(e), x + 16, y, 16, C_INK);
        DrawText(TextFormat("%d", q->total), x + 140, y, 16, C_INK);
        percent_bar(x + 175, y + 4, 60, pct);
        DrawText(TextFormat("%.0f%%", pct), x + 242, y, 16, C_INK);
        DrawText(TextFormat("%d", s->equipPeak[e]), x + 310, y, 16, s->equipPeak[e] >= q->total ? C_EMERGENCY : C_INK);
        y += 24;
    }
    y += 16;
    heading("WAITING FOR EQUIPMENT", x, y);                       y += 24;
    DrawText("Item",            x,       y, 14, C_MUTED);
    DrawText("Patients",        x + 130, y, 14, C_MUTED);
    DrawText("Total",           x + 210, y, 14, C_MUTED);
    DrawText("Longest",         x + 290, y, 14, C_MUTED);
    y += 22;
    for (int e = 0; e < EQUIP_TYPES; e++) {
        DrawRectangle(x, y + 3, 10, 10, draw_equipment_colour(e));
        DrawText(sim_equipment_short(e), x + 16, y, 16, C_INK);
        Color c = s->equipWaits[e] > 0 ? C_EMERGENCY : C_INK;
        DrawText(TextFormat("%d", s->equipWaits[e]), x + 150, y, 16, c);
        DrawText(TextFormat("%.0f min", sim_minutes(s->equipWaitTotal[e])), x + 210, y, 16, C_INK);
        DrawText(TextFormat("%.0f min", sim_minutes(s->equipLongestWait[e])), x + 290, y, 16, C_INK);
        y += 24;
    }

    // Rooms: right
    x = 500; y = 110;
    heading("ROOMS AFTER A&E", x, y);                             y += 24;
    DrawText("Room",          x,       y, 14, C_MUTED);
    DrawText("Floor",         x + 115, y, 14, C_MUTED);
    DrawText("Patients",      x + 160, y, 14, C_MUTED);
    DrawText("Busy",          x + 235, y, 14, C_MUTED);
    DrawText("Longest queue", x + 330, y, 14, C_MUTED);
    y += 22;
    for (int r = 0; r < ROOM_TYPES; r++) {
        const SimRoom *room = &sim->rooms[r];
        float pct = 100.0f * s->roomBusyTime[r] / (room->capacity * SHIFT_LENGTH);
        int fl = sim_room_floor(r);
        if (r % 2 == 1) DrawRectangle(x - 6, y - 2, 430, 21, C_BG);
        DrawText(room->name, x, y, 16, C_INK);
        DrawText(fl == 0 ? "G" : TextFormat("%d", fl), x + 125, y, 16, C_MUTED);
        DrawText(TextFormat("%d", s->sentTo[r]), x + 175, y, 16, C_INK);
        percent_bar(x + 235, y + 4, 50, pct);
        DrawText(TextFormat("%.0f%%", pct), x + 290, y, 16, C_INK);
        DrawText(TextFormat("%d", s->roomLongestQueue[r]), x + 360, y, 16, s->roomLongestQueue[r] >= 4 ? C_EMERGENCY : C_INK);
        y += 21;
    }

    button((Rectangle){ SCREEN_W / 2 - 120, 520, 240, 44 }, "Back to report", C_BRAND);
}
 
// ---------------------------------------------------------------- treatment log page
 
#define LOG_TOP       140    // y of the first row
#define LOG_ROW_H      22
#define LOG_ROWS      15     // rows visible at once
#define LOG_TEXT      16
 
// Column x positions
#define COL_ID         70
#define COL_DOT       118
#define COL_COND      132
#define COL_ICD       320
#define COL_ARRIVED   400
#define COL_WAITED    465
#define COL_OUT       540
#define COL_BY        600
#define COL_SENT      755    // where they went after A&E, as in the CSV
#define COL_END       915
 
static Rectangle back_button(void)
{
    return (Rectangle){ SCREEN_W / 2 - 120, 505, 240, 44 };
}
 
bool report_log_back_clicked(void) { return clicked(back_button()); }
 
int report_log_max_scroll(const Sim *sim)
{
    int extra = sim->treatedLogCount - LOG_ROWS;
    return extra > 0 ? extra : 0;
}
 
// Copy text into out, shortening it with "..." if it is wider than maxWidth.
static void fit_text(char *out, size_t size, const char *text, int maxWidth)
{
    snprintf(out, size, "%s", text);
    for (int keep = (int)strlen(text) - 1; keep > 0; keep--) {
        if (MeasureText(out, LOG_TEXT) <= maxWidth) return;
        snprintf(out, size, "%.*s...", keep, text);
    }
}
 
// A clock value as a time of day, e.g. "18:52".
static const char *hhmm(float clock)
{
    int total = DAY_START_HOUR * 60 + (int)sim_minutes(clock);
    return TextFormat("%02d:%02d", (total / 60) % 24, total % 60);
}
 
static Color severity_dot(int severity)
{
    if (severity == 1) return C_EMERGENCY;
    if (severity == 2) return C_URGENT;
    return C_ROUTINE;
}
 
void draw_patient_log(const Sim *sim, int scroll)
{
    bool night = (sim->shift == SHIFT_NIGHT);
    int count = sim->treatedLogCount;
 
    ClearBackground(C_BG);
    Rectangle card = { 40, 20, 920, 560 };
    DrawRectangleRounded(card, 0.03f, 8, C_CARD);
    DrawRectangleRoundedLines(card, 0.03f, 8, C_EDGE);
 
    centered(TextFormat("PATIENTS TREATED: %s SHIFT, DAY %d", night ? "NIGHT" : "DAY", sim->day),
             38, 26, C_BRAND);
    centered(TextFormat("%d patients discharged", count), 72, 16, C_MUTED);
 
    // Column headings
    int hy = 108;
    DrawText("No.",        COL_ID,      hy, 14, C_MUTED);
    DrawText("Condition",  COL_COND,    hy, 14, C_MUTED);
    DrawText("ICD-10",     COL_ICD,     hy, 14, C_MUTED);
    DrawText("Arrived",    COL_ARRIVED, hy, 14, C_MUTED);
    DrawText("Waited",     COL_WAITED,  hy, 14, C_MUTED);
    DrawText("Left",       COL_OUT,     hy, 14, C_MUTED);
    DrawText("Treated by", COL_BY,      hy, 14, C_MUTED);
    DrawText("Sent to",    COL_SENT,    hy, 14, C_MUTED);
    DrawLine(COL_ID, hy + 22, COL_END, hy + 22, C_EDGE);
 
    if (count == 0) {
        centered("No patients were discharged during this shift.", 250, 18, C_MUTED);
    }
 
    // The visible rows
    for (int row = 0; row < LOG_ROWS; row++) {
        int i = scroll + row;
        if (i < 0 || i >= count) break;
        const TreatedRecord *r = &sim->treatedLog[i];
        int y = LOG_TOP + row * LOG_ROW_H;
 
        if (row % 2 == 1) DrawRectangle(COL_ID - 10, y - 3, COL_END - COL_ID + 20, LOG_ROW_H, C_BG);
 
        char cond[CONDITION_NAME_LEN], code[ICD10_LEN], by[TREATED_BY_LEN], sent[sizeof r->sentTo];
        fit_text(cond, sizeof cond, r->condition->name,  COL_ICD - COL_COND - 12);
        fit_text(code, sizeof code, r->condition->icd10, COL_ARRIVED - COL_ICD - 8);
        fit_text(by,   sizeof by,   r->treatedBy,       COL_SENT - COL_BY - 10);
        fit_text(sent, sizeof sent, r->sentTo,          COL_END - COL_SENT);
 
        DrawText(TextFormat("%d", r->id), COL_ID, y, LOG_TEXT, C_INK);
        DrawCircle(COL_DOT, y + 8, 6, severity_dot(r->severity));
        DrawText(cond,                      COL_COND,    y, LOG_TEXT, C_INK);
        DrawText(code,                      COL_ICD,     y, LOG_TEXT, C_MUTED);
        DrawText(hhmm(r->arrivalTime),      COL_ARRIVED, y, LOG_TEXT, C_INK);
        DrawText(TextFormat("%.0f min", sim_minutes(r->wait)), COL_WAITED, y, LOG_TEXT, C_INK);
        DrawText(hhmm(r->dischargeTime),    COL_OUT,     y, LOG_TEXT, C_INK);
        DrawText(by, COL_BY, y, LOG_TEXT, r->sawDoctor ? C_BRAND : C_INK);
        DrawText(sent, COL_SENT, y, LOG_TEXT, (strcmp(r->sentTo, "Home") == 0) ? C_MUTED : C_INK);
    }
 
    // Scroll position and hints
    if (count > 0) {
        int last = scroll + LOG_ROWS < count ? scroll + LOG_ROWS : count;
        DrawText(TextFormat("Showing %d-%d of %d", scroll + 1, last, count), COL_ID, 476, 14, C_MUTED);
    }
    const char *hint = "Mouse wheel or Up/Down to scroll   B to go back";
    DrawText(hint, COL_END - MeasureText(hint, 14), 476, 14, C_MUTED);
 
    // Scroll bar on the right, only when the list doesn't fit
    if (count > LOG_ROWS) {
        float trackY = LOG_TOP, trackH = LOG_ROWS * LOG_ROW_H;
        float thumbH = trackH * LOG_ROWS / count;
        float thumbY = trackY + (trackH - thumbH) * scroll / (count - LOG_ROWS);
        DrawRectangleRounded((Rectangle){ COL_END + 18, trackY, 6, trackH }, 1.0f, 4, C_EDGE);
        DrawRectangleRounded((Rectangle){ COL_END + 18, thumbY, 6, thumbH }, 1.0f, 4, C_MUTED);
    }
 
    button(back_button(), "Back to report", C_BRAND);
}