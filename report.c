// report.c - end-of-shift report: what happened during the shift, and how
// busy the beds and nurses were. After the night shift it also shows
// totals for the whole day. The report links to a second page listing
// every patient treated during the shift (the treatment log), and a third
// showing how the equipment and the rooms after A&E were used.
 
#include <math.h>
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
 
// Report buttons, in a row: "Patients treated", "Equipment & rooms",
// "Graphs", and continue.
static Rectangle report_button(int k) { return (Rectangle){ SCREEN_W / 2 - 362 + k * 185.0f, 500, 170, 50 }; }
static Rectangle view_log_button(void)  { return report_button(0); }
static Rectangle equipment_button(void) { return report_button(1); }
static Rectangle graphs_button(void)    { return report_button(2); }
static Rectangle continue_button(void)  { return report_button(3); }
 
static bool clicked(Rectangle r)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), r);
}
 
bool report_new_shift_clicked(void) { return clicked(continue_button()); }
bool report_view_log_clicked(void)  { return clicked(view_log_button()); }
bool report_view_equipment_clicked(void) { return clicked(equipment_button()); }
bool report_view_graphs_clicked(void) { return clicked(graphs_button()); }
 
// A rounded button with centred white text; darker while the mouse is over it.
static void button(Rectangle b, const char *label, Color colour)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), b);
    DrawRectangleRounded(b, 0.4f, 8, hover ? C_INK : colour);
    int size = 20;
    while (size > 12 && MeasureText(label, size) > b.width - 16) size--;      // smaller text if it won't fit
    DrawText(label, (int)(b.x + b.width / 2 - MeasureText(label, size) / 2),
             (int)(b.y + b.height / 2 - size / 2), size, WHITE);
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
    centered(TextFormat("Day %d   %02d:00 to %02d:00     %s,  %d nurses, %d doctors, %d beds", sim->day, from % 24,
                        (from + SHIFT_HOURS) % 24, sim_scenario_name(sim->setup.scenario), sim_nurses_on(sim),
                        sim_shift_doctors(sim), sim->setup.beds), 92, 16, C_MUTED);
    if (s->carriedOver > 0)                          // patients already in at the start (not counted as arrivals)
        centered(TextFormat(night ? "%d patients handed over by the day shift" : "Started with %d patients from the day before",
                            s->carriedOver), 112, 14, C_EMERGENCY);
 
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
    row("Helicopter rescues", TextFormat("%d", s->rescues), LEFT_LABEL, LEFT_VALUE, y);   y += ROW;
    row("Power cuts", s->powerCuts ? TextFormat("%dx, %.0f min off", s->powerCuts, sim_minutes(s->powerOffTime)) : "none",
        LEFT_LABEL, LEFT_VALUE, y);
    // Right column: waiting times and resource use
    y = 135;
    heading("WAITING FOR A BED", RIGHT_LABEL, y);                                         y += 26;
    row("Average wait", TextFormat("%.0f min", sim_minutes(average_wait(s, NULL))),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row("Longest wait", TextFormat("%.0f min", sim_minutes(s->longestWait)),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    // The mini games (G): games played for real patients, and how many were won
    int played = s->surgeriesPlayed + s->shocksPlayed + s->spinsPlayed + s->scansPlayed + s->triageGames +
                 s->pharmacyPlayed + s->drivesPlayed + s->landingsPlayed;
    int lost = s->complications + s->shocksFailed + s->spinsFailed + s->scansFailed + s->triageGamesLost +
               s->pharmacyFailed + s->drivesSlow + s->landingsFailed;
    row("Mini games won / played", TextFormat("%d / %d", played - lost, played),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    // The triage game: calls right, and the wrong ones (too low: seen too late)
    int calls = s->triageRight + s->triageTooLow + s->triageTooHigh;
    row("Triage calls right", calls == 0 ? "none made" :
        TextFormat("%d / %d (%d low, %d high)", s->triageRight, calls, s->triageTooLow, s->triageTooHigh),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 28;
 
    float bedUse = 100.0f * s->bedBusyTime    / (BED_COUNT    * SHIFT_LENGTH);
    float docUse = 100.0f * s->nurseBusyTime / (sim_nurses_on(sim) * SHIFT_LENGTH);
    heading("RESOURCES", RIGHT_LABEL, y);                                                 y += 26;
    row(TextFormat("Beds in use (%d)", BED_COUNT), TextFormat("%.0f%%", bedUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row(TextFormat("Nurses busy (%d)", sim_nurses_on(sim)), TextFormat("%.0f%%", docUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    float drUse = 100.0f * s->doctorBusyTime / (sim_shift_doctors(sim) * SHIFT_LENGTH);
    row(TextFormat("Doctors busy (%d)", sim_shift_doctors(sim)), TextFormat("%.0f%%", drUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row("On-call doctor called", s->onCallCalls ? TextFormat("%dx, %.0f min", s->onCallCalls, sim_minutes(s->onCallTime)) : "no",
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 28;
    if (s->incidentCasualties > 0) {                    // (only in the day shift, so it doesn't clash with WHOLE DAY)
        heading("MAJOR INCIDENT", RIGHT_LABEL, y);                                        y += 26;
        row("Bus crash casualties", TextFormat("%d", s->incidentCasualties), RIGHT_LABEL, RIGHT_VALUE, y);
    }
    
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
    Rectangle g = graphs_button();
    Rectangle b = continue_button();
    button(v, "Patients treated", C_MUTED);
    button(e, "Equipment & rooms", C_MUTED);
    button(g, "Graphs", C_MUTED);
    button(b, "Continue", C_BRAND);
 
    const char *vHint = "or press L";
    const char *eHint = "or press E";
    const char *bHint = "or press N";
    DrawText(vHint, (int)(v.x + v.width / 2 - MeasureText(vHint, 14) / 2), 556, 14, C_MUTED);
    DrawText(eHint, (int)(e.x + e.width / 2 - MeasureText(eHint, 14) / 2), 556, 14, C_MUTED);
    DrawText(bHint, (int)(b.x + b.width / 2 - MeasureText(bHint, 14) / 2), 556, 14, C_MUTED);
    const char *gHint = "or press G";
    DrawText(gHint, (int)(g.x + g.width / 2 - MeasureText(gHint, 14) / 2), 556, 14, C_MUTED);
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
    centered("or press E (or B)", 568, 14, C_MUTED);
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
    const char *hint = "Mouse wheel or Up/Down to scroll   L (or B) to go back";
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
// ---------------------------------------------------------------- graphs page
// Two line graphs across the shift: how many were in the waiting room, and
// the average wait for a bed so far. Coloured lines down both mark what
// happened when (a helicopter landing, the on-call doctor coming in, the
// power going off and coming back, a major incident), and the time the power
// was off is shaded.

static Rectangle graphs_back_button(void) { return (Rectangle){ SCREEN_W / 2 - 120, 516, 240, 40 }; }
bool report_graphs_back_clicked(void) { return clicked(graphs_back_button()); }

#define GRAPH_X0 120
#define GRAPH_X1 880

static const Color EVENT_COLOUR[EVENT_TYPES] = {
    { 230, 130, 40, 255 },   // helicopter landed
    { 40, 60, 90, 255 },     // on-call doctor in
    { 200, 40, 40, 255 },    // power off
    { 60, 160, 90, 255 },    // power back
    { 150, 40, 160, 255 },   // major incident
};

static float graph_x(float t) { return GRAPH_X0 + (GRAPH_X1 - GRAPH_X0) * t / SHIFT_LENGTH; }

// A round number at least v, for the top of a graph's scale
static float nice_max(float v)
{
    // (all divide by 4, so the four grid lines get whole numbers)
    const float steps[] = { 4, 8, 12, 20, 40, 60, 80, 100, 120, 160, 200, 300, 400, 600, 800, 1000, 1200, 1600, 2000 };
    for (int i = 0; i < (int)(sizeof steps / sizeof steps[0]); i++) if (v <= steps[i]) return steps[i];
    return v;
}

// One graph: the values (count of them) across the shift, in a box from y0 to y1
static void line_graph(const Sim *sim, const char *title, const char *explain, const float *values, int count,
                       float y0, float y1, Color colour, const char *unit)
{
    const ShiftStats *s = &sim->stats;
    float top = 0;
    for (int i = 0; i < count; i++) if (values[i] > top) top = values[i];
    top = nice_max(top < 1 ? 1 : top);

    DrawText(title, GRAPH_X0, (int)y0 - 22, 16, C_INK);
    DrawText(explain, GRAPH_X0 + MeasureText(title, 16) + 14, (int)y0 - 19, 12, C_MUTED);   // what it means, in plain words
    DrawRectangle(GRAPH_X0, (int)y0, GRAPH_X1 - GRAPH_X0, (int)(y1 - y0), (Color){ 248, 250, 252, 255 });
    // The power off: shaded
    float offAt = -1;
    for (int e = 0; e <= s->eventCount; e++) {
        bool end = (e == s->eventCount);
        if (!end && s->events[e].type == EVENT_POWER_OFF) offAt = s->events[e].time;
        if (offAt >= 0 && (end || s->events[e].type == EVENT_POWER_ON)) {
            float t1 = end ? (sim->power == POWER_OUT ? SHIFT_LENGTH : offAt) : s->events[e].time;
            DrawRectangle((int)graph_x(offAt), (int)y0, (int)(graph_x(t1) - graph_x(offAt)) + 1, (int)(y1 - y0), (Color){ 240, 80, 70, 50 });
            offAt = -1;
        }
    }
    // Grid lines and the scale on the left
    for (int k = 0; k <= 4; k++) {
        float y = y1 - (y1 - y0) * k / 4.0f;
        DrawLine(GRAPH_X0, (int)y, GRAPH_X1, (int)y, C_EDGE);
        const char *v = TextFormat("%.0f", top * k / 4.0f);
        DrawText(v, GRAPH_X0 - 8 - MeasureText(v, 12), (int)y - 6, 12, C_MUTED);
    }
    DrawText(unit, GRAPH_X1 + 8, (int)y0, 12, C_MUTED);
    // What happened when
    for (int e = 0; e < s->eventCount; e++) {
        float x = graph_x(s->events[e].time);
        Color c = EVENT_COLOUR[s->events[e].type];
        for (float y = y0; y < y1; y += 8) DrawLine((int)x, (int)y, (int)x, (int)fminf(y + 4, y1), c);   // dashed
    }
    // The line
    for (int i = 1; i < count; i++) {
        Vector2 a = { graph_x((i - 1) * SAMPLE_EVERY), y1 - (y1 - y0) * values[i - 1] / top };
        Vector2 b = { graph_x(i * SAMPLE_EVERY), y1 - (y1 - y0) * values[i] / top };
        DrawLineEx(a, b, 2.5f, colour);
    }
    DrawRectangleLines(GRAPH_X0, (int)y0, GRAPH_X1 - GRAPH_X0, (int)(y1 - y0), C_LIGHT);
}

void draw_graphs_page(const Sim *sim)
{
    const ShiftStats *s = &sim->stats;
    bool night = (sim->shift == SHIFT_NIGHT);
    ClearBackground(C_BG);
    Rectangle card = { 40, 20, 920, 560 };
    DrawRectangleRounded(card, 0.04f, 8, C_CARD);
    DrawRectangleRoundedLines(card, 0.04f, 8, C_EDGE);
    centered(TextFormat("HOW THE %s SHIFT WENT: DAY %d", night ? "NIGHT" : "DAY", sim->day), 36, 24, C_BRAND);

    if (s->samples < 2) {
        centered("Not enough of the shift to draw yet.", 260, 18, C_MUTED);
    } else {
        float waiting[SAMPLES], longest[SAMPLES];
        for (int i = 0; i < s->samples; i++) { waiting[i] = (float)s->sampleWaiting[i]; longest[i] = sim_minutes(s->sampleLongestWait[i]); }
        line_graph(sim, "Patients in the waiting room", "how many were waiting for a bed, every 10 minutes",
                   waiting, s->samples, 104, 244, C_BRAND, "patients");
        line_graph(sim, "Longest wait right then", "how long the person who'd waited longest had been there (0: nobody waiting)",
                   longest, s->samples, 298, 418, C_URGENT, "minutes");
        // The times along the bottom, every 2 hours
        int from = night ? DAY_START_HOUR + SHIFT_HOURS : DAY_START_HOUR;
        for (int h = 0; h <= SHIFT_HOURS; h += 2) {
            float x = graph_x(SHIFT_LENGTH * h / SHIFT_HOURS);
            const char *t = TextFormat("%02d:00", (from + h) % 24);
            DrawText(t, (int)x - MeasureText(t, 12) / 2, 424, 12, C_MUTED);
            DrawText(t, (int)x - MeasureText(t, 12) / 2, 250, 12, C_MUTED);
        }
    }

    // The key to the lines, for the things that happened this shift
    int x = GRAPH_X0;
    bool seen[EVENT_TYPES] = { false };
    for (int e = 0; e < s->eventCount; e++) seen[s->events[e].type] = true;
    if (s->eventCount > 0) {
        DrawText("Dashed lines show when:", x, 444, 14, C_MUTED);
        x += MeasureText("Dashed lines show when:", 14) + 14;
    }
    for (int k = 0; k < EVENT_TYPES; k++) {
        if (!seen[k]) continue;
        DrawRectangle(x, 450, 14, 4, EVENT_COLOUR[k]);
        DrawText(sim_event_name((EventType)k), x + 20, 444, 14, C_INK);
        x += 40 + MeasureText(sim_event_name((EventType)k), 14);
    }
    if (s->powerCuts > 0) {
        DrawRectangle(x, 444, 14, 14, (Color){ 240, 80, 70, 70 });
        DrawText("power off", x + 20, 444, 14, C_INK);
    }

    // Power cut details
    if (s->powerCuts > 0) {
        const char *gen = s->generatorGames == 0 ? "the generator started by itself"
                        : s->generatorFailed == 0 ? "you started the generator by hand"
                        : "the generator wouldn't start: the electricians fixed it";
        centered(TextFormat("Power cuts: %d, %.0f min without power, %d %s held up, %d stuck in the lifts; %s",
                            s->powerCuts, sim_minutes(s->powerOffTime), s->scansHeld, s->scansHeld == 1 ? "scan" : "scans",
                            s->liftStuck, gen), 474, 14, C_INK);
    } else if (s->eventCount == 0) {
        centered("Nothing out of the ordinary happened this shift.", 474, 14, C_MUTED);
    }

    button(graphs_back_button(), "Back to report", C_BRAND);
    centered("or press G (or B)", 560, 14, C_MUTED);
}
