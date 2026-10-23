// minigames.c - the mini-game menu, and passing input and drawing on to
// whichever game is being played (opgame.c, defibgame.c, spingame.c,
// scangame.c, triagegame.c, pharmgame.c, drivegame.c, heligame.c).
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "opgame.h"
#include "defibgame.h"
#include "spingame.h"
#include "scangame.h"
#include "triagegame.h"
#include "pharmgame.h"
#include "drivegame.h"
#include "heligame.h"
#include "gengame.h"          // (not on the menu: it opens when the generator fails)
#include "minigames.h"

static bool menuOpen = false;
static bool fromMenu = false;    // the game being played was picked from the menu (go back to it after)

// ---- Each game's details, for the menu, the table and the bubbles ----
typedef struct {
    const char *name;
    const char *lines[3];        // what it is, on the menu card
    const char *rowName;         // its short name in the table and the bubbles, e.g. "SURGERY"
    const char *shortKeys;       // ...and the keys that start it: its own key ("O") or the menu ("G 3")
    Color colour;
} GameInfo;

static const GameInfo INFO[MINIGAME_COUNT] = {
    { "Operation", { "Take the item out along", "the channel without", "touching the edge." },
      "SURGERY", "O", { 40, 140, 110, 255 } },
    { "Shock!", { "Charge, then shock as", "the spike passes the", "zone. Three good shocks." },
      "DEFIB", "S", { 190, 60, 50, 255 } },
    { "Centrifuge", { "Load the blood tubes so", "the rotor balances, then", "spin it. Unbalanced: wobble!" },
      "BLOODS", "G 3", { 60, 100, 190, 255 } },
    { "Hold still", { "Line up the X-ray or CT", "and take the image when", "the patient is still." },
      "SCAN", "X", { 70, 130, 170, 255 } },
    { "Triage call", { "Three new arrivals: drag", "each to emergency, urgent", "or routine." },
      NULL, NULL, { 80, 90, 160, 255 } },
    { "Pharmacy rush", { "Click the right pills", "into the bag before the", "clock runs out." },
      "PHARMACY", "P", { 60, 140, 80, 255 } },
    { "Blue lights", { "Drive the ambulance in,", "weaving through traffic.", "Don't clip the cars!" },
      NULL, NULL, { 50, 70, 170, 255 } },
    { "Touchdown", { "Land the rescue", "helicopter on the pad", "in a gusty wind." },
      "HELI", "G 8", { 200, 110, 40, 255 } },
};

// The real patient a game could be played for now, or 0 (a practice round).
// Triage and the ambulance are always real: they bring new patients in.
static int offer_for(MiniGame g)
{
    switch (g) {
    case MINIGAME_OPERATION: return opgame_offer();
    case MINIGAME_SHOCK:     return defibgame_offer();
    case MINIGAME_SPIN:      return spingame_offer();
    case MINIGAME_SCAN:      return scangame_offer();
    case MINIGAME_PHARMACY:  return pharmgame_offer();
    case MINIGAME_LANDING:   return heligame_offer();
    default:                 return 0;
    }
}

static bool game_on(void)
{
    return opgame_active() || defibgame_active() || spingame_active() || scangame_active() ||
           triagegame_active() || pharmgame_active() || drivegame_active() || heligame_active() ||
           gengame_active();
}

// ---- The calls from main.c ----

void minigames_watch(const Sim *sim)
{
    opgame_watch(sim);
    defibgame_watch(sim);
    spingame_watch(sim);
    scangame_watch(sim);
    pharmgame_watch(sim);
    heligame_watch(sim);
}

void minigames_open_menu(void) { menuOpen = true; }

static void start_game(Sim *sim, MiniGame g)
{
    switch (g) {
    case MINIGAME_OPERATION: opgame_start(sim);     break;
    case MINIGAME_SHOCK:     defibgame_start(sim);  break;
    case MINIGAME_SPIN:      spingame_start(sim);   break;
    case MINIGAME_SCAN:      scangame_start(sim);   break;
    case MINIGAME_TRIAGE:    triagegame_start(sim); break;
    case MINIGAME_PHARMACY:  pharmgame_start(sim);  break;
    case MINIGAME_DRIVE:     drivegame_start();     break;
    case MINIGAME_LANDING:   heligame_start(sim);   break;
    default: break;
    }
}

bool minigames_rotor(void) { return heligame_flying(); }

void minigames_start_generator(Sim *sim)
{
    (void)sim;
    if (game_on()) return;
    menuOpen = false;
    fromMenu = false;
    gengame_start();
}

void minigames_start(Sim *sim, MiniGame game)
{
    menuOpen = false;
    fromMenu = false;
    start_game(sim, game);
}

bool minigames_active(void) { return menuOpen || game_on(); }

// Where each card is on the menu: two rows of four
static Rectangle card_rect(int i) { return (Rectangle){ 30.0f + (i % 4) * 240.0f, 112.0f + (i / 4) * 218.0f, 220.0f, 206.0f }; }

void minigames_update(Sim *sim)
{
    if (game_on()) {
        opgame_update(sim);
        defibgame_update(sim);
        spingame_update(sim);
        scangame_update(sim);
        triagegame_update(sim);
        pharmgame_update(sim);
        drivegame_update(sim);
        heligame_update(sim);
        gengame_update(sim);
        if (!game_on() && fromMenu) menuOpen = true;   // finished: back to the menu
        return;
    }
    if (!menuOpen) return;

    // The menu: 1-8 or a click picks a game; G or BACKSPACE goes back to the hospital
    if (IsKeyPressed(KEY_G) || IsKeyPressed(KEY_BACKSPACE)) { menuOpen = false; return; }
    int pick = -1;
    for (int i = 0; i < MINIGAME_COUNT; i++)
        if (IsKeyPressed(KEY_ONE + i) || IsKeyPressed(KEY_KP_1 + i)) pick = i;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        for (int i = 0; i < MINIGAME_COUNT; i++) if (CheckCollisionPointRec(m, card_rect(i))) pick = i;
    }
    if (pick >= 0) {
        menuOpen = false;
        fromMenu = true;
        start_game(sim, (MiniGame)pick);
    }
}

// ---- Drawing ----

// A little picture for each card, centred on (cx, cy)
static void draw_icon(MiniGame g, float cx, float cy)
{
    Color steel = { 200, 205, 215, 255 };
    if (g == MINIGAME_OPERATION) {                   // tweezers over a channel
        DrawLineEx((Vector2){ cx - 50, cy + 18 }, (Vector2){ cx + 50, cy + 18 }, 14, steel);
        DrawLineEx((Vector2){ cx - 50, cy + 18 }, (Vector2){ cx + 50, cy + 18 }, 9, (Color){ 120, 40, 45, 255 });
        DrawCircleV((Vector2){ cx - 8, cy + 18 }, 5, (Color){ 200, 170, 90, 255 });
        DrawLineEx((Vector2){ cx - 12, cy + 12 }, (Vector2){ cx + 20, cy - 26 }, 4, steel);
        DrawLineEx((Vector2){ cx - 4, cy + 12 }, (Vector2){ cx + 28, cy - 22 }, 4, steel);
    } else if (g == MINIGAME_SHOCK) {                // a heart trace with a lightning bolt
        Vector2 pts[] = { { cx - 55, cy + 10 }, { cx - 25, cy + 10 }, { cx - 15, cy - 24 }, { cx - 5, cy + 26 },
                          { cx + 5, cy + 10 }, { cx + 55, cy + 10 } };
        for (int i = 0; i + 1 < 6; i++) DrawLineEx(pts[i], pts[i + 1], 3, (Color){ 80, 255, 140, 255 });
        DrawTriangle((Vector2){ cx + 30, cy - 30 }, (Vector2){ cx + 18, cy - 4 }, (Vector2){ cx + 32, cy - 8 }, (Color){ 255, 210, 60, 255 });
        DrawTriangle((Vector2){ cx + 32, cy - 12 }, (Vector2){ cx + 20, cy - 8 }, (Vector2){ cx + 26, cy + 16 }, (Color){ 255, 210, 60, 255 });
    } else if (g == MINIGAME_SPIN) {                 // the rotor from above, with tubes in
        DrawCircleV((Vector2){ cx, cy }, 32, (Color){ 60, 66, 76, 255 });
        DrawCircleV((Vector2){ cx, cy }, 28, (Color){ 175, 182, 192, 255 });
        for (int k = 0; k < 6; k++) {
            float a = k * PI / 3;
            Vector2 c = { cx + 19 * cosf(a), cy + 19 * sinf(a) };
            DrawCircleV(c, 6, (Color){ 70, 76, 86, 255 });
            if (k % 3 != 1) DrawCircleV(c, 4, k == 0 ? (Color){ 220, 50, 60, 255 } : (Color){ 250, 200, 60, 255 });
        }
        DrawCircleV((Vector2){ cx, cy }, 5, (Color){ 120, 126, 136, 255 });
    } else if (g == MINIGAME_SCAN) {                 // an X-ray of a hand in a frame
        DrawRectangle((int)cx - 40, (int)cy - 32, 80, 64, (Color){ 10, 20, 32, 255 });
        Color bone = { 190, 220, 245, 255 };
        for (int f = 0; f < 4; f++) DrawLineEx((Vector2){ cx - 12 + f * 8, cy + 18 }, (Vector2){ cx - 18 + f * 11, cy - 22 }, 4, bone);
        DrawLineEx((Vector2){ cx - 16, cy + 14 }, (Vector2){ cx - 30, cy - 4 }, 4, bone);
        DrawRectangleLinesEx((Rectangle){ cx - 26, cy - 24, 52, 48 }, 2, (Color){ 120, 255, 160, 255 });
    } else if (g == MINIGAME_TRIAGE) {               // three coloured cards
        Color cs[3] = { C_EMERGENCY, C_URGENT, C_ROUTINE };
        for (int i = 0; i < 3; i++) {
            DrawRectangleRounded((Rectangle){ cx - 54 + i * 38, cy - 24 + i * 6, 32, 42 }, 0.2f, 4, WHITE);
            DrawRectangle((int)cx - 54 + i * 38, (int)cy - 24 + i * 6, 32, 8, cs[i]);
        }
    } else if (g == MINIGAME_PHARMACY) {             // a tablet and a capsule
        DrawCircleV((Vector2){ cx - 26, cy }, 16, (Color){ 50, 90, 180, 255 });
        DrawCircleV((Vector2){ cx - 26, cy }, 13, (Color){ 70, 120, 220, 255 });
        DrawRectangleRounded((Rectangle){ cx, cy - 10, 54, 20 }, 1.0f, 8, (Color){ 240, 200, 60, 255 });
        DrawRectangleRounded((Rectangle){ cx, cy - 10, 27, 20 }, 1.0f, 8, (Color){ 220, 70, 70, 255 });
    } else if (g == MINIGAME_DRIVE) {                // the ambulance from above, on a road
        DrawRectangle((int)cx - 40, (int)cy - 34, 80, 68, (Color){ 60, 62, 68, 255 });
        DrawRectangle((int)cx - 2, (int)cy - 34, 4, 20, WHITE);
        DrawRectangleRounded((Rectangle){ cx - 14, cy - 22, 28, 46 }, 0.3f, 4, (Color){ 245, 245, 240, 255 });
        DrawRectangle((int)cx - 14, (int)cy, 28, 5, (Color){ 210, 40, 40, 255 });
        DrawRectangle((int)cx - 12, (int)cy - 12, 8, 4, (Color){ 60, 140, 255, 255 });
        DrawRectangle((int)cx + 4, (int)cy - 12, 8, 4, (Color){ 60, 140, 255, 255 });
    } else {                                         // a helicopter over an H
        DrawRectangle((int)cx - 30, (int)cy + 22, 60, 8, (Color){ 70, 74, 80, 255 });
        DrawText("H", (int)cx - 5, (int)cy + 20, 10, WHITE);
        DrawEllipse((int)cx, (int)cy - 2, 26, 12, (Color){ 220, 60, 50, 255 });
        DrawLineEx((Vector2){ cx - 20, cy - 4 }, (Vector2){ cx - 56, cy - 8 }, 5, (Color){ 220, 60, 50, 255 });
        DrawLineEx((Vector2){ cx - 46, cy - 16 }, (Vector2){ cx + 46, cy - 16 }, 3, (Color){ 40, 44, 52, 255 });
        DrawLineEx((Vector2){ cx - 20, cy + 14 }, (Vector2){ cx + 20, cy + 14 }, 3, (Color){ 40, 44, 52, 255 });
    }
}

void minigames_draw(const Sim *sim, Camera2D fit)
{
    if (opgame_active())    { opgame_draw(fit);    return; }
    if (defibgame_active()) { defibgame_draw(fit); return; }
    if (spingame_active())  { spingame_draw(fit);  return; }
    if (scangame_active())  { scangame_draw(fit);  return; }
    if (triagegame_active()) { triagegame_draw(fit); return; }
    if (pharmgame_active()) { pharmgame_draw(fit); return; }
    if (drivegame_active()) { drivegame_draw(fit); return; }
    if (heligame_active())  { heligame_draw(fit);  return; }
    if (gengame_active())   { gengame_draw(fit);   return; }
    if (!menuOpen) return;

    ClearBackground(C_BG);
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 28, 34, 44, 255 });
    DrawText("MINI GAMES", SCREEN_W / 2 - MeasureText("MINI GAMES", 36) / 2, 22, 36, WHITE);
    const char *sub = "The hospital waits while you play. Win for a real patient and they get through quicker.";
    DrawText(sub, SCREEN_W / 2 - MeasureText(sub, 16) / 2, 72, 16, (Color){ 180, 190, 205, 255 });

    // How each game has gone this shift (real patients only): played, and lost
    const ShiftStats *s = &sim->stats;
    int played[MINIGAME_COUNT] = { s->surgeriesPlayed, s->shocksPlayed, s->spinsPlayed, s->scansPlayed,
                                   s->triageGames, s->pharmacyPlayed, s->drivesPlayed, s->landingsPlayed };
    int lost[MINIGAME_COUNT]   = { s->complications, s->shocksFailed, s->spinsFailed, s->scansFailed,
                                   s->triageGamesLost, s->pharmacyFailed, s->drivesSlow, s->landingsFailed };

    Vector2 m = GetMousePosition();
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
    for (int i = 0; i < MINIGAME_COUNT; i++) {
        Rectangle r = card_rect(i);
        bool hover = CheckCollisionPointRec(m, r);
        Color c = INFO[i].colour;
        DrawRectangleRounded(r, 0.08f, 10, hover ? (Color){ 52, 62, 78, 255 } : (Color){ 40, 48, 62, 255 });
        DrawRectangleRounded((Rectangle){ r.x, r.y, r.width, 38 }, 0.4f, 10, c);
        DrawRectangle((int)r.x, (int)r.y + 24, (int)r.width, 14, c);           // square off the bottom of the header
        if (hover) DrawRectangleRoundedLines(r, 0.08f, 10, (Color){ 255, 255, 255, 160 });
        DrawText(TextFormat("%d", i + 1), (int)r.x + 12, (int)r.y + 8, 24, (Color){ 255, 255, 255, 200 });
        DrawText(INFO[i].name, (int)r.x + 38, (int)r.y + 10, 20, WHITE);
        const char *quick = INFO[i].shortKeys;          // its own key, if it has one (O, S, X, P)
        if (quick != NULL && quick[0] != 'G') {
            Rectangle chip = { r.x + r.width - 30, r.y + 8, 22, 22 };
            DrawRectangleRounded(chip, 0.3f, 6, WHITE);
            DrawText(quick, (int)(chip.x + 11) - MeasureText(quick, 18) / 2, (int)chip.y + 2, 18, c);
        }

        draw_icon((MiniGame)i, r.x + r.width / 2, r.y + 78);
        for (int k = 0; k < 3; k++)
            DrawText(INFO[i].lines[k], (int)r.x + 14, (int)r.y + 116 + k * 17, 14, (Color){ 210, 218, 230, 255 });

        // A real patient ready for it, or a practice round
        int id = offer_for((MiniGame)i);
        const char *t;
        bool real = true;
        if (i == MINIGAME_TRIAGE)      t = "3 arrivals at the door";
        else if (i == MINIGAME_DRIVE)  t = "Emergency call waiting";
        else if (id != 0)              t = (i == MINIGAME_LANDING) ? TextFormat("Rescue %d in the air", id) : TextFormat("Patient %d is ready", id);
        else                         { t = "Practice round"; real = false; }
        Rectangle tag = { r.x + 12, r.y + 168, r.width - 24, 22 };
        DrawRectangleRounded(tag, 0.5f, 8, real ? (Color){ 40, 150, 90, (unsigned char)(190 + 65 * pulse) } : (Color){ 70, 78, 92, 255 });
        DrawText(t, (int)(tag.x + tag.width / 2) - MeasureText(t, 14) / 2, (int)tag.y + 4, 14,
                 real ? WHITE : (Color){ 200, 205, 215, 255 });
        const char *score = played[i] == 0 ? "This shift: none yet"
                                           : TextFormat("This shift: %d won of %d", played[i] - lost[i], played[i]);
        DrawText(score, (int)r.x + 14, (int)r.y + 192, 12, (Color){ 160, 172, 190, 255 });
    }

    const char *hint = "1 to 8 or click to play  (O S X P straight from the hospital)      G or BACKSPACE back";
    DrawText(hint, SCREEN_W / 2 - MeasureText(hint, 18) / 2, 562, 18, (Color){ 220, 228, 238, 255 });
    EndMode2D();
}

// ---- What's ready, and where ----

int minigames_offers(MiniOffer *out, int max)
{
    int n = 0;
    for (int i = 0; i < MINIGAME_COUNT && n < max; i++) {
        int id = offer_for((MiniGame)i);
        if (id == 0 || INFO[i].rowName == NULL) continue;
        out[n].game = (MiniGame)i;
        out[n].patientId = id;
        n++;
    }
    return n;
}

bool minigames_patient_place(const Sim *sim, int id, int *room, int *slot, int *bed)
{
    *room = -1; *slot = -1; *bed = -1;
    for (int b = 0; b < BED_COUNT; b++) {
        const Patient *p = sim->beds[b].occupant;
        if (p != NULL && p->id == id) { *bed = b; return true; }
    }
    for (int r = 0; r < ROOM_TYPES; r++) {
        for (int s = 0; s < sim->rooms[r].capacity; s++) {
            const Patient *p = sim->rooms[r].in[s];
            if (p != NULL && p->id == id) { *room = r; *slot = s; return true; }
        }
    }
    return false;                                    // in the air, or gone
}

Color minigames_colour(MiniGame g) { return INFO[g].colour; }

// A tiny picture for each game, about 14 pixels across, in white
void minigames_draw_mini_icon(MiniGame g, float cx, float cy, Color c)
{
    switch (g) {
    case MINIGAME_OPERATION:                         // a scalpel
        DrawLineEx((Vector2){ cx - 6, cy + 6 }, (Vector2){ cx + 1, cy - 1 }, 3, c);
        DrawTriangle((Vector2){ cx + 1, cy - 1 }, (Vector2){ cx + 7, cy - 7 }, (Vector2){ cx + 3, cy + 1 }, c);
        DrawTriangle((Vector2){ cx + 1, cy - 1 }, (Vector2){ cx + 3, cy + 1 }, (Vector2){ cx + 7, cy - 7 }, c);
        break;
    case MINIGAME_SHOCK:                             // a lightning bolt
        DrawLineEx((Vector2){ cx + 3, cy - 7 }, (Vector2){ cx - 3, cy + 1 }, 3, c);
        DrawLineEx((Vector2){ cx - 3, cy + 1 }, (Vector2){ cx + 3, cy }, 3, c);
        DrawLineEx((Vector2){ cx + 3, cy }, (Vector2){ cx - 3, cy + 7 }, 3, c);
        break;
    case MINIGAME_SPIN:                              // a drop of blood
        DrawCircleV((Vector2){ cx, cy + 2 }, 5, c);
        DrawTriangle((Vector2){ cx, cy - 7 }, (Vector2){ cx - 5, cy + 1 }, (Vector2){ cx + 5, cy + 1 }, c);
        break;
    case MINIGAME_SCAN:                              // the image frame
        DrawRectangleLinesEx((Rectangle){ cx - 6, cy - 6, 12, 12 }, 2, c);
        DrawCircleV((Vector2){ cx, cy }, 2, c);
        break;
    case MINIGAME_PHARMACY:                          // a capsule
        DrawRectangleRounded((Rectangle){ cx - 7, cy - 3, 14, 7 }, 1.0f, 6, c);
        break;
    case MINIGAME_LANDING:                           // H for the helipad
        DrawLineEx((Vector2){ cx - 4, cy - 6 }, (Vector2){ cx - 4, cy + 6 }, 2, c);
        DrawLineEx((Vector2){ cx + 4, cy - 6 }, (Vector2){ cx + 4, cy + 6 }, 2, c);
        DrawLineEx((Vector2){ cx - 4, cy }, (Vector2){ cx + 4, cy }, 2, c);
        break;
    default:
        DrawCircleV((Vector2){ cx, cy }, 4, c);
        break;
    }
}

void minigames_draw_badge(MiniGame g, float cx, float cy)
{
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
    Color c = INFO[g].colour;
    DrawCircleV((Vector2){ cx, cy }, 11 + 2 * pulse, (Color){ c.r, c.g, c.b, 80 });     // a soft glow that pulses
    DrawCircleV((Vector2){ cx, cy }, 10, c);
    DrawCircleLines((int)cx, (int)cy, 10, WHITE);
    minigames_draw_mini_icon(g, cx, cy, WHITE);
}

void minigames_draw_bubble(MiniGame g, int patientId, Vector2 tip, float maxRight)
{
    float bob = 3.0f * sinf((float)GetTime() * 3.0f);                // floats gently up and down
    const char *text = TextFormat("%s %d", INFO[g].rowName, patientId);
    const char *key = INFO[g].shortKeys;
    int tw = MeasureText(text, 12), kw = MeasureText(key, 12);
    float w = 26 + tw + 10 + kw + 10, h = 22;
    Rectangle r = { tip.x - w / 2, tip.y - 10 - h + bob, w, h };
    if (r.x + w > maxRight) r.x = maxRight - w;                      // slid along to stay clear of the panels
    if (r.x < 8) r.x = 8;
    float tailX = fminf(fmaxf(tip.x, r.x + 10), r.x + w - 10);       // the tail leaves from inside the bubble
    Color c = INFO[g].colour;
    DrawTriangle((Vector2){ tailX - 6, r.y + h - 1 }, (Vector2){ tip.x, tip.y + bob }, (Vector2){ tailX + 6, r.y + h - 1 }, c);   // the tail
    DrawRectangleRounded(r, 0.5f, 8, c);
    DrawRectangleRoundedLines(r, 0.5f, 8, (Color){ 255, 255, 255, 180 });
    minigames_draw_mini_icon(g, r.x + 13, r.y + h / 2, WHITE);
    DrawText(text, (int)r.x + 24, (int)r.y + 5, 12, WHITE);
    Rectangle k = { r.x + 26 + tw + 6, r.y + 3, kw + 8, h - 6 };      // the key to press, in a little box
    DrawRectangleRounded(k, 0.4f, 6, (Color){ 255, 255, 255, 230 });
    DrawText(key, (int)k.x + 4, (int)k.y + 3, 12, c);
}

int minigames_draw_table(float x, float y, float w, int columns, Color bg, Color ink, Color muted)
{
    if (minigames_active()) return 0;
    MiniOffer offers[MINIGAME_COUNT];
    int n = minigames_offers(offers, MINIGAME_COUNT);
    int rows = (n == 0) ? 1 : (n + columns - 1) / columns;
    float h = 20 + rows * 16 + 4;
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.15f, 8, bg);
    DrawText("GAMES READY", (int)x + 10, (int)y + 5, 12, muted);
    const char *g = "G menu";
    DrawText(g, (int)(x + w) - 10 - MeasureText(g, 12), (int)y + 5, 12, muted);
    if (n == 0) {
        DrawText("Nobody ready - practice with G", (int)x + 10, (int)y + 21, 12, muted);
        return (int)h;
    }
    float colW = (w - 12) / columns;
    for (int i = 0; i < n; i++) {
        float cx = x + 8 + (i % columns) * colW, cy = y + 20 + (i / columns) * 16;
        Color c = INFO[offers[i].game].colour;
        DrawCircleV((Vector2){ cx + 7, cy + 7 }, 7, c);
        minigames_draw_mini_icon(offers[i].game, cx + 7, cy + 7, WHITE);
        DrawText(TextFormat("%s %d", INFO[offers[i].game].rowName, offers[i].patientId), (int)cx + 18, (int)cy + 1, 12, ink);
        const char *k = INFO[offers[i].game].shortKeys;
        DrawText(k, (int)(cx + colW) - 8 - MeasureText(k, 12), (int)cy + 1, 12, c);
    }
    return (int)h;
}
