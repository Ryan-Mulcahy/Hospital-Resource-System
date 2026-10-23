// setup.c - the setup screen before each day, and the table of earlier shifts.
//
// The screen is a card with two columns. Left: the kind of day, the staff on
// each shift, the beds and the power cut switch. Right: the equipment, and
// the earlier shifts' results. Every number has a - and a + button beside it.
// The same functions give the buttons' positions for drawing and for
// clicking, so they always line up.

#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "setup.h"

// ---- The results of earlier shifts ----
#define HISTORY 8            // the most shown (the oldest drop off)

typedef struct {
    int day;
    ShiftType shift;
    Setup setup;
    int arrived, treated;
    int carriedOver;             // patients already in when the shift started
    float avgWait, longestWait;  // simulated seconds
    int powerCuts;
} ShiftResult;

static ShiftResult history[HISTORY];
static int historyCount = 0;

void setup_record_shift(const Sim *sim)
{
    if (historyCount == HISTORY) {                   // full: drop the oldest
        for (int i = 1; i < HISTORY; i++) history[i - 1] = history[i];
        historyCount--;
    }
    const ShiftStats *s = &sim->stats;
    history[historyCount++] = (ShiftResult){
        sim->day, sim->shift, sim->setup, s->arrived, s->treated, s->carriedOver,
        s->admitted > 0 ? s->totalWait / s->admitted : 0.0f, s->longestWait, s->powerCuts
    };
}

const char *setup_summary(const Setup *setup)
{
    return TextFormat("%d+%d nurses, %d+%d doctors, %d beds", setup->nurses[SHIFT_DAY], setup->nurses[SHIFT_NIGHT],
                      setup->doctors[SHIFT_DAY], setup->doctors[SHIFT_NIGHT], setup->beds);
}

// ---- Where everything is ----
#define LEFT_X   70
#define RIGHT_X  530
#define ROW_H    28

// The things with - and + buttons, in order
enum {
    ITEM_DAY_NURSES, ITEM_DAY_DOCTORS, ITEM_NIGHT_NURSES, ITEM_NIGHT_DOCTORS, ITEM_BEDS,
    ITEM_EQUIP,                      // ...then one for each piece of equipment
    ITEM_COUNT = ITEM_EQUIP + EQUIP_TYPES
};

// Where an item's row is (its label starts here)
static Vector2 item_pos(int item)
{
    if (item < ITEM_EQUIP) return (Vector2){ LEFT_X, 222.0f + item * ROW_H + (item >= ITEM_BEDS ? 22 : 0) };
    return (Vector2){ RIGHT_X, 122.0f + (item - ITEM_EQUIP) * ROW_H };
}
static Rectangle minus_button(int item) { Vector2 p = item_pos(item); return (Rectangle){ p.x + 200, p.y - 2, 26, 24 }; }
static Rectangle plus_button(int item)  { Vector2 p = item_pos(item); return (Rectangle){ p.x + 270, p.y - 2, 26, 24 }; }
static Rectangle scenario_button(int k) { return (Rectangle){ LEFT_X + k * 136.0f, 118, 128, 34 }; }
static Rectangle power_button(void)     { return (Rectangle){ LEFT_X + 200, 396, 96, 26 }; }
static Rectangle carry_button(void)     { return (Rectangle){ LEFT_X + 200, 460, 96, 26 }; }
static Rectangle usual_button(void)     { return (Rectangle){ SCREEN_W / 2 - 250, 506, 220, 46 }; }
static Rectangle start_button(void)     { return (Rectangle){ SCREEN_W / 2 + 30, 506, 220, 46 }; }

// The number an item changes, and its limits
static int *item_value(Setup *s, int item, int *lo, int *hi)
{
    switch (item) {
    case ITEM_DAY_NURSES:    *lo = SETUP_NURSES_MIN;  *hi = NURSE_MAX;  return &s->nurses[SHIFT_DAY];
    case ITEM_DAY_DOCTORS:   *lo = SETUP_DOCTORS_MIN; *hi = DOCTOR_MAX; return &s->doctors[SHIFT_DAY];
    case ITEM_NIGHT_NURSES:  *lo = SETUP_NURSES_MIN;  *hi = NURSE_MAX;  return &s->nurses[SHIFT_NIGHT];
    case ITEM_NIGHT_DOCTORS: *lo = SETUP_DOCTORS_MIN; *hi = DOCTOR_MAX; return &s->doctors[SHIFT_NIGHT];
    case ITEM_BEDS:          *lo = SETUP_BEDS_MIN;    *hi = BED_COUNT - 1; return &s->beds;
    default:                 *lo = 1; *hi = EQUIP_MAX_EACH; return &s->equipment[item - ITEM_EQUIP];
    }
}

static const char *item_label(int item)
{
    switch (item) {
    case ITEM_DAY_NURSES:    return "Day nurses";
    case ITEM_DAY_DOCTORS:   return "Day doctors";
    case ITEM_NIGHT_NURSES:  return "Night nurses";
    case ITEM_NIGHT_DOCTORS: return "Night doctors";
    case ITEM_BEDS:          return "Beds (+ resus)";
    default:                 return sim_equipment_short(item - ITEM_EQUIP);
    }
}

static const char *SCENARIO_TEXT[SCENARIO_COUNT] = {
    "An ordinary day and night.",
    "About half as many patients again, all day and night.",
    "A bus crash in the middle of the day: 12 casualties at once.",
};

// ---- Input ----
bool setup_screen_update(Setup *setup)
{
    if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ENTER)) return true;
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    Vector2 m = GetMousePosition();
    for (int item = 0; item < ITEM_COUNT; item++) {
        int lo, hi;
        int *v = item_value(setup, item, &lo, &hi);
        if (CheckCollisionPointRec(m, minus_button(item)) && *v > lo) (*v)--;
        if (CheckCollisionPointRec(m, plus_button(item)) && *v < hi) (*v)++;
    }
    for (int k = 0; k < SCENARIO_COUNT; k++)
        if (CheckCollisionPointRec(m, scenario_button(k))) setup->scenario = (Scenario)k;
    if (CheckCollisionPointRec(m, power_button())) setup->powerCuts = !setup->powerCuts;
    if (CheckCollisionPointRec(m, carry_button())) setup->carryOver = !setup->carryOver;
    if (CheckCollisionPointRec(m, usual_button())) {
        bool carry = setup->carryOver;               // (the usual numbers, but leave this switch as it is)
        sim_default_setup(setup);
        setup->carryOver = carry;
    }
    return CheckCollisionPointRec(m, start_button());
}

// ---- Drawing ----

static void small_button(Rectangle r, const char *text, bool enabled)
{
    bool over = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.3f, 6, !enabled ? C_EDGE : over ? C_INK : C_BRAND);
    DrawText(text, (int)(r.x + r.width / 2) - MeasureText(text, 18) / 2, (int)r.y + 3, 18, WHITE);
}

// An ON / OFF switch
static void switch_button(Rectangle r, bool on)
{
    DrawRectangleRounded(r, 0.5f, 8, on ? C_ROUTINE : C_LIGHT);
    const char *t = on ? "ON" : "OFF";
    DrawText(t, (int)(r.x + r.width / 2) - MeasureText(t, 16) / 2, (int)r.y + 5, 16, WHITE);
}

static void big_button(Rectangle r, const char *text, Color colour)
{
    bool over = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.4f, 8, over ? C_INK : colour);
    DrawText(text, (int)(r.x + r.width / 2) - MeasureText(text, 20) / 2, (int)r.y + 13, 20, WHITE);
}

void draw_setup_screen(const Setup *setup, int day)
{
    ClearBackground(C_BG);
    Rectangle card = { 40, 20, 920, 560 };
    DrawRectangleRounded(card, 0.04f, 8, C_CARD);
    DrawRectangleRoundedLines(card, 0.04f, 8, C_EDGE);
    const char *title = TextFormat("SET UP DAY %d", day);
    DrawText(title, SCREEN_W / 2 - MeasureText(title, 28) / 2, 36, 28, C_BRAND);
    const char *sub = "Change the numbers, then compare how each shift went in the table.";
    DrawText(sub, SCREEN_W / 2 - MeasureText(sub, 14) / 2, 70, 14, C_MUTED);

    // The kind of day
    DrawText("KIND OF DAY", LEFT_X, 98, 14, C_MUTED);
    for (int k = 0; k < SCENARIO_COUNT; k++) {
        Rectangle r = scenario_button(k);
        bool on = (setup->scenario == (Scenario)k);
        bool over = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRounded(r, 0.3f, 6, on ? C_BRAND : over ? C_EDGE : C_BG);
        DrawRectangleRoundedLines(r, 0.3f, 6, on ? C_BRAND : C_LIGHT);
        const char *name = sim_scenario_name((Scenario)k);
        DrawText(name, (int)(r.x + r.width / 2) - MeasureText(name, 16) / 2, (int)r.y + 9, 16, on ? WHITE : C_INK);
    }
    DrawText(SCENARIO_TEXT[setup->scenario], LEFT_X, 160, 14, C_INK);

    // The rows of numbers
    DrawText("STAFF", LEFT_X, 198, 14, C_MUTED);
    DrawText("A&E", LEFT_X, item_pos(ITEM_BEDS).y - 22, 14, C_MUTED);
    DrawText("EQUIPMENT", RIGHT_X, 98, 14, C_MUTED);
    Setup copy = *setup;                             // (item_value wants a changeable setup)
    for (int item = 0; item < ITEM_COUNT; item++) {
        int lo, hi;
        int v = *item_value(&copy, item, &lo, &hi);
        Vector2 p = item_pos(item);
        if (item >= ITEM_EQUIP) DrawRectangle((int)p.x, (int)p.y + 4, 10, 10, draw_equipment_colour(item - ITEM_EQUIP));
        DrawText(item_label(item), (int)p.x + (item >= ITEM_EQUIP ? 16 : 0), (int)p.y, 18, C_INK);
        small_button(minus_button(item), "-", v > lo);
        const char *num = TextFormat("%d", v);
        DrawText(num, (int)p.x + 248 - MeasureText(num, 20) / 2, (int)p.y, 20, C_INK);
        small_button(plus_button(item), "+", v < hi);
    }

    // Power cuts on or off
    DrawText("POWER CUTS", LEFT_X, 378, 14, C_MUTED);
    DrawText("By themselves", LEFT_X, 398, 18, C_INK);
    switch_button(power_button(), setup->powerCuts);
    DrawText("(C cuts the power at any time)", LEFT_X, 424, 12, C_MUTED);

    // Yesterday's patients: kept, or an empty hospital
    DrawText("THE NEXT DAY", LEFT_X, 442, 14, C_MUTED);
    DrawText("Keep patients", LEFT_X, 462, 18, C_INK);
    switch_button(carry_button(), setup->carryOver);
    DrawText(day == 1 ? "(from day 2: nobody's in yet)" : setup->carryOver ? "(yesterday's patients stay in)" : "(the hospital starts empty)",
             LEFT_X, 488, 12, C_MUTED);

    // The earlier shifts
    int y = 280;
    DrawText("EARLIER SHIFTS", RIGHT_X, y, 14, C_MUTED);                          y += 22;
    if (historyCount == 0) {
        DrawText("None yet: they'll appear here after each shift.", RIGHT_X, y, 14, C_MUTED);
    } else {
        // Shift, kind of day, staff and beds (4N 2D 8B), treated, average wait, power cuts
        const int cx[] = { RIGHT_X, RIGHT_X + 82, RIGHT_X + 172, RIGHT_X + 262, RIGHT_X + 326, RIGHT_X + 396 };
        DrawText("Shift", cx[0], y, 12, C_MUTED);   DrawText("Kind", cx[1], y, 12, C_MUTED);
        DrawText("Staff, beds", cx[2], y, 12, C_MUTED); DrawText("Treated", cx[3], y, 12, C_MUTED);
        DrawText("Avg wait", cx[4], y, 12, C_MUTED); DrawText("Cuts", cx[5], y, 12, C_MUTED); y += 17;
        for (int i = 0; i < historyCount; i++, y += 19) {
            const ShiftResult *r = &history[i];
            int sh = r->shift;
            DrawText(TextFormat("Day %d %s", r->day, sh == SHIFT_DAY ? "day" : "night"), cx[0], y, 14, C_INK);
            const char *kind = r->setup.scenario == SCENARIO_NORMAL ? "Normal" : r->setup.scenario == SCENARIO_FLU ? "Flu" : "Incident";
            DrawText(kind, cx[1], y, 14, C_INK);
            // A day shift that started with patients already in (from
            // yesterday): not a clean comparison, so it's marked
            if (sh == SHIFT_DAY && r->carriedOver > 0)
                DrawText(TextFormat("+%d", r->carriedOver), cx[1] + MeasureText(kind, 14) + 4, y + 2, 12, C_EMERGENCY);
            DrawText(TextFormat("%dN %dD %dB", r->setup.nurses[sh], r->setup.doctors[sh], r->setup.beds), cx[2], y, 14, C_INK);
            DrawText(TextFormat("%d", r->treated), cx[3], y, 14, C_INK);
            DrawText(TextFormat("%.0f min", sim_minutes(r->avgWait)), cx[4], y, 14, C_INK);
            DrawText(TextFormat("%d", r->powerCuts), cx[5], y, 14, r->powerCuts ? C_EMERGENCY : C_INK);
        }
        bool anyCarried = false;
        for (int i = 0; i < historyCount; i++) if (history[i].shift == SHIFT_DAY && history[i].carriedOver > 0) anyCarried = true;
        if (anyCarried) DrawText("+n: the day started with n patients already in", RIGHT_X, y + 2, 12, C_EMERGENCY);
    }

    big_button(usual_button(), "Usual setup", C_MUTED);
    big_button(start_button(), TextFormat("Start day %d", day), C_BRAND);
    DrawText("or press N", (int)(start_button().x + start_button().width / 2) - MeasureText("or press N", 14) / 2, 556, 14, C_MUTED);
}
