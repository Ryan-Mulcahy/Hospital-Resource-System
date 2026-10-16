// roster.c - the staff pools and the rota.
// Each shift takes the next staff from the pool, wrapping round at the end,
// so nobody works two shifts in a row and everyone gets days and nights.
 
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "roster.h"
 
static const char *NURSE_POOL[] = {
    "Aoife", "Ciaran", "Niamh", "Sean", "Orla", "Darragh", "Meabh", "Conor", "Grainne"
};
static const char *DOCTOR_POOL[] = {
    "Byrne", "Walsh", "Kelly", "Murphy", "Nolan"
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
 
const char *roster_nurse(int shiftNumber, int k)
{
    int start = pool_start(shiftNumber, NURSE_COUNT, NIGHT_NURSES, NURSE_POOL_SIZE);
    return NURSE_POOL[(start + k) % NURSE_POOL_SIZE];
}
 
const char *roster_doctor(int shiftNumber, int k)
{
    int start = pool_start(shiftNumber, DOCTOR_COUNT, NIGHT_DOCTORS, DOCTOR_POOL_SIZE);
    return DOCTOR_POOL[(start + k) % DOCTOR_POOL_SIZE];
}
 
void roster_assign(Sim *sim)
{
    int s = roster_shift_number(sim->day, sim->shift);
    for (int k = 0; k < NURSES_ON(sim->shift); k++)  sim->nurses[k].name  = roster_nurse(s, k);
    for (int k = 0; k < DOCTORS_ON(sim->shift); k++) sim->doctors[k].name = roster_doctor(s, k);
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
static void draw_team(int x, const char *heading, int day, ShiftType shift)
{
    int s = roster_shift_number(day, shift);
    bool night = (shift == SHIFT_NIGHT);
 
    DrawText(heading, x, 140, 16, C_BRAND);
    DrawText(TextFormat("Day %d  %s", day, night ? "Night shift" : "Day shift"), x, 164, 22, C_INK);
    DrawText(night ? "19:00 to 07:00" : "07:00 to 19:00", x, 192, 18, C_MUTED);
 
    int y = 228;
    DrawText("NURSES", x, y, 14, C_MUTED);                          y += 24;
    for (int k = 0; k < NURSES_ON(shift); k++) {
        DrawCircle(x + 6, y + 9, 6, C_BRAND);
        DrawText(roster_nurse(s, k), x + 20, y, 20, C_INK);         y += 26;
    }
    y += 8;
    DrawText("DOCTORS", x, y, 14, C_MUTED);                         y += 24;
    for (int k = 0; k < DOCTORS_ON(shift); k++) {
        DrawCircle(x + 6, y + 9, 6, C_INK);
        DrawText(TextFormat("Dr %s", roster_doctor(s, k)), x + 20, y, 20, C_INK);  y += 26;
    }
}
 
void draw_roster_screen(int day, ShiftType shift)
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
    draw_team(170, "ON THIS SHIFT", day, shift);
    draw_team(560, "NEXT SHIFT", nextDay, nextShift);
    DrawLine(SCREEN_W / 2, 140, SCREEN_W / 2, 430, C_EDGE);
 
    // Start button
    Rectangle b = start_button();
    bool hover = CheckCollisionPointRec(GetMousePosition(), b);
    DrawRectangleRounded(b, 0.4f, 8, hover ? C_INK : C_BRAND);
    const char *label = (shift == SHIFT_NIGHT) ? "Start night shift" : "Start day shift";
    DrawText(label, (int)(b.x + b.width / 2 - MeasureText(label, 20) / 2), (int)(b.y + 15), 20, WHITE);
    centered("or press N", 530, 14, C_MUTED);
}