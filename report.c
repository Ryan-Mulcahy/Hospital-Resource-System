// report.c - end-of-shift report: what happened during the shift, and how
// busy the beds and nurses were. After the night shift it also shows
// totals for the whole day. The report links to a second page listing
// every patient treated during the shift (the treatment log).
 
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
#define ROW          28
 
// Report buttons: "View patients treated" on the left, continue on the right.
static Rectangle view_log_button(void)
{
    return (Rectangle){ SCREEN_W / 2 - 250, 470, 240, 50 };
}
 
static Rectangle continue_button(void)
{
    return (Rectangle){ SCREEN_W / 2 + 10, 470, 240, 50 };
}
 
static bool clicked(Rectangle r)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), r);
}
 
bool report_new_shift_clicked(void) { return clicked(continue_button()); }
bool report_view_log_clicked(void)  { return clicked(view_log_button()); }
 
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
    row("Still in a bed", TextFormat("%d", inBeds), LEFT_LABEL, LEFT_VALUE, y);           y += ROW;
    row("Sent to a doctor", TextFormat("%d", s->sawDoctor), LEFT_LABEL, LEFT_VALUE, y);    y += ROW;
    int tests = s->sentTo[ROOM_XRAY] + s->sentTo[ROOM_MRI] + s->sentTo[ROOM_BLOODS] + s->sentTo[ROOM_THEATRE];
    row("Sent for tests / surgery", TextFormat("%d", tests), LEFT_LABEL, LEFT_VALUE, y);  y += ROW;
    row("Admitted to the ward", TextFormat("%d", s->sentTo[ROOM_WARD]), LEFT_LABEL, LEFT_VALUE, y); y += ROW;
    row("Helicopter rescues", TextFormat("%d", s->rescues), LEFT_LABEL, LEFT_VALUE, y);
    // Right column: waiting times and resource use
    y = 135;
    heading("WAITING FOR A BED", RIGHT_LABEL, y);                                         y += 26;
    row("Average wait", TextFormat("%.0f min", sim_minutes(average_wait(s, NULL))),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row("Longest wait", TextFormat("%.0f min", sim_minutes(s->longestWait)),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += 40;
 
    float bedUse = 100.0f * s->bedBusyTime    / (BED_COUNT    * SHIFT_LENGTH);
    float docUse = 100.0f * s->nurseBusyTime / (NURSES_ON(sim->shift) * SHIFT_LENGTH);
    heading("RESOURCES", RIGHT_LABEL, y);                                                 y += 26;
    row(TextFormat("Beds in use (%d)", BED_COUNT), TextFormat("%.0f%%", bedUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    row(TextFormat("Nurses busy (%d)", NURSES_ON(sim->shift)), TextFormat("%.0f%%", docUse),
        RIGHT_LABEL, RIGHT_VALUE, y);                                                     y += ROW;
    float drUse = 100.0f * s->doctorBusyTime / (DOCTORS_ON(sim->shift) * SHIFT_LENGTH);
    row(TextFormat("Doctors busy (%d)", DOCTORS_ON(sim->shift)), TextFormat("%.0f%%", drUse),
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
 
    // Buttons: view the treatment log, or continue to the next shift
    Rectangle v = view_log_button();
    Rectangle b = continue_button();
    button(v, "Patients treated", C_MUTED);
    button(b, "Continue", C_BRAND);
 
    const char *vHint = "or press L";
    const char *bHint = "or press N";
    DrawText(vHint, (int)(v.x + v.width / 2 - MeasureText(vHint, 14) / 2), 530, 14, C_MUTED);
    DrawText(bHint, (int)(b.x + b.width / 2 - MeasureText(bHint, 14) / 2), 530, 14, C_MUTED);
}
 
// ---------------------------------------------------------------- treatment log page
 
#define LOG_TOP       140    // y of the first row
#define LOG_ROW_H      22
#define LOG_ROWS      15     // rows visible at once
#define LOG_TEXT      16
 
// Column x positions
#define COL_ID         70
#define COL_DOT       125
#define COL_COND      140
#define COL_ICD       365
#define COL_ARRIVED   460
#define COL_WAITED    535
#define COL_OUT       625
#define COL_BY        700
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
 
        char cond[CONDITION_NAME_LEN], code[ICD10_LEN], by[TREATED_BY_LEN];
        fit_text(cond, sizeof cond, r->condition->name,  COL_ICD - COL_COND - 12);
        fit_text(code, sizeof code, r->condition->icd10, COL_ARRIVED - COL_ICD - 8);
        fit_text(by,   sizeof by,   r->treatedBy,       COL_END - COL_BY);
 
        DrawText(TextFormat("%d", r->id), COL_ID, y, LOG_TEXT, C_INK);
        DrawCircle(COL_DOT, y + 8, 6, severity_dot(r->severity));
        DrawText(cond,                      COL_COND,    y, LOG_TEXT, C_INK);
        DrawText(code,                      COL_ICD,     y, LOG_TEXT, C_MUTED);
        DrawText(hhmm(r->arrivalTime),      COL_ARRIVED, y, LOG_TEXT, C_INK);
        DrawText(TextFormat("%.0f min", sim_minutes(r->wait)), COL_WAITED, y, LOG_TEXT, C_INK);
        DrawText(hhmm(r->dischargeTime),    COL_OUT,     y, LOG_TEXT, C_INK);
        DrawText(by, COL_BY, y, LOG_TEXT, r->sawDoctor ? C_BRAND : C_INK);
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