// roster.c - the staff pools and the rota.
// Each shift takes the next staff from the pool, wrapping round at the end,
// so nobody works two shifts in a row and everyone gets days and nights.
 
#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "roster.h"
 
// (Big enough for the most staff the setup screen allows on a day and a
// night, so nobody works the night after their day.)
static const char *NURSE_POOL[] = {
    "Aoife", "Ciaran", "Niamh", "Sean", "Orla", "Darragh", "Meabh", "Conor", "Grainne",
    "Eoin", "Sinead", "Padraig", "Aisling", "Fionn", "Roisin", "Cathal"
};
static const char *DOCTOR_POOL[] = {
    "Byrne", "Walsh", "Kelly", "Murphy", "Nolan", "O'Brien", "Daly", "Quinn", "Farrell"
};
// The on-call doctors take turns, one per shift. They're kept apart from the
// doctors above, so the on-call doctor is never someone who has just worked a
// shift or is about to.
static const char *ON_CALL_POOL[] = {
    "Doyle", "Ryan", "Brennan", "Kavanagh", "Gallagher", "Lynch"
};
#define NURSE_POOL_SIZE  (int)(sizeof NURSE_POOL / sizeof NURSE_POOL[0])
#define DOCTOR_POOL_SIZE (int)(sizeof DOCTOR_POOL / sizeof DOCTOR_POOL[0])
 
int roster_shift_number(int day, ShiftType shift)
{
    return (day - 1) * 2 + (shift == SHIFT_NIGHT ? 1 : 0);
}
 
// Where in the pool a shift starts: everyone used by the shifts before it.
// Day and night shifts can need different numbers of staff.
static int pool_start(int shiftNumber, int dayCount, int nightCount, int poolSize)
{
    int daysBefore   = (shiftNumber + 1) / 2;
    int nightsBefore = shiftNumber / 2;
    return (daysBefore * dayCount + nightsBefore * nightCount) % poolSize;
}
 
const char *roster_nurse(const Setup *setup, int shiftNumber, int k)
{
    int start = pool_start(shiftNumber, setup->nurses[SHIFT_DAY], setup->nurses[SHIFT_NIGHT], NURSE_POOL_SIZE);
    return NURSE_POOL[(start + k) % NURSE_POOL_SIZE];
}
 
const char *roster_doctor(const Setup *setup, int shiftNumber, int k)
{
    int start = pool_start(shiftNumber, setup->doctors[SHIFT_DAY], setup->doctors[SHIFT_NIGHT], DOCTOR_POOL_SIZE);
    return DOCTOR_POOL[(start + k) % DOCTOR_POOL_SIZE];
}
 
const char *roster_on_call(int shiftNumber, int k)
{
    // Three a shift (ON_CALL_MAX), from the next three in the pool, so
    // nobody's on call two shifts in a row
    int n = (int)(sizeof ON_CALL_POOL / sizeof ON_CALL_POOL[0]);
    return ON_CALL_POOL[(shiftNumber * ON_CALL_MAX + k) % n];
}

void roster_assign(Sim *sim)
{
    int s = roster_shift_number(sim->day, sim->shift);
    for (int k = 0; k < sim_nurses_on(sim); k++)     sim->nurses[k].name  = roster_nurse(&sim->setup, s, k);
    for (int k = 0; k < sim_shift_doctors(sim); k++) sim->doctors[k].name = roster_doctor(&sim->setup, s, k);
}
 
// ---- Roster screen ----
 
static Rectangle start_button(void)
{
    return (Rectangle){ SCREEN_W / 2 - 120, 470, 240, 50 };
}
 
bool roster_start_clicked(void)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), start_button());
}
 
static void centered(const char *text, int y, int size, Color color)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, color);
}
 
// One column of the roster: which shift it is, then its nurses and doctors.
static void draw_team(const Setup *setup, int x, const char *heading, int day, ShiftType shift)
{
    int s = roster_shift_number(day, shift);
    bool night = (shift == SHIFT_NIGHT);
 
    DrawText(heading, x, 140, 16, C_BRAND);
    DrawText(TextFormat("Day %d  %s", day, night ? "Night shift" : "Day shift"), x, 164, 22, C_INK);
    DrawText(night ? "19:00 to 07:00" : "07:00 to 19:00", x, 192, 18, C_MUTED);
 
    int y = 222;
    DrawText("NURSES", x, y, 14, C_MUTED);                          y += 22;
    // Up to 6 nurses: in two columns if there are more than 4
    int n = setup->nurses[shift];
    for (int k = 0; k < n; k++) {
        int col = (n > 4) ? k % 2 : 0;
        DrawCircle(x + 6 + col * 150, y + 9, 6, C_BRAND);
        DrawText(roster_nurse(setup, s, k), x + 20 + col * 150, y, 20, C_INK);
        if (n <= 4 || col == 1 || k == n - 1) y += 24;
    }
    y += 6;
    DrawText("DOCTORS", x, y, 14, C_MUTED);                         y += 22;
    int d = setup->doctors[shift];                                  // (two columns if more than 2)
    for (int k = 0; k < d; k++) {
        int col = (d > 2) ? k % 2 : 0;
        DrawCircle(x + 6 + col * 150, y + 9, 6, C_INK);
        DrawText(TextFormat("Dr %s", roster_doctor(setup, s, k)), x + 20 + col * 150, y, 20, C_INK);
        if (d <= 2 || col == 1 || k == d - 1) y += 24;
    }
    // The on-call doctors: at home unless they're needed (sim.c calls them in)
    if (setup->onCall == 0) {
        DrawText("Nobody on call", x + 20, y, 16, C_MUTED);
        return;
    }
    char names[96] = "";
    for (int k = 0; k < setup->onCall; k++) {
        char one[32];
        snprintf(one, sizeof one, "%sDr %s", k ? ", " : "", roster_on_call(s, k));
        strncat(names, one, sizeof names - strlen(names) - 1);
    }
    DrawCircleLines(x + 6, y + 9, 6, C_EMERGENCY);
    DrawText("ON CALL", x + 20, y + 3, 12, C_EMERGENCY);
    DrawText(names, x + 82, y, setup->onCall > 1 ? 16 : 20, C_INK);                 y += 22;
    DrawText(TextFormat("called in when the waiting room is full (%d)", WAITING_ROOM_SEATS), x + 20, y, 12, C_MUTED);
}
 
void draw_roster_screen(const Setup *setup, int day, ShiftType shift)
{
    ClearBackground(C_BG);
    Rectangle card = { 100, 30, 800, 545 };
    DrawRectangleRounded(card, 0.05f, 8, C_CARD);
    DrawRectangleRoundedLines(card, 0.05f, 8, C_EDGE);
 
    centered("STAFF ROSTER", 55, 30, C_BRAND);
    centered(TextFormat("Day %d", day), 92, 18, C_MUTED);
 
    // Left: the shift about to start. Right: the shift after it.
    int nextDay = (shift == SHIFT_NIGHT) ? day + 1 : day;
    ShiftType nextShift = (shift == SHIFT_NIGHT) ? SHIFT_DAY : SHIFT_NIGHT;
    draw_team(setup, 170, "ON THIS SHIFT", day, shift);
    draw_team(setup, 560, "NEXT SHIFT", nextDay, nextShift);
    DrawLine(SCREEN_W / 2, 140, SCREEN_W / 2, 450, C_EDGE);
 
    // Start button
    Rectangle b = start_button();
    bool hover = CheckCollisionPointRec(GetMousePosition(), b);
    DrawRectangleRounded(b, 0.4f, 8, hover ? C_INK : C_BRAND);
    const char *label = (shift == SHIFT_NIGHT) ? "Start night shift" : "Start day shift";
    DrawText(label, (int)(b.x + b.width / 2 - MeasureText(label, 20) / 2), (int)(b.y + 15), 20, WHITE);
    centered("or press N", 530, 14, C_MUTED);
}