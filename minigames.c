// minigames.c - the mini-game menu, and passing input and drawing on to
// whichever game is being played (opgame.c, defibgame.c, veingame.c,
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
#include "veingame.h"
#include "scangame.h"
#include "triagegame.h"
#include "pharmgame.h"
#include "drivegame.h"
#include "heligame.h"
#include "minigames.h"

static bool menuOpen = false;
static bool fromMenu = false;    // the game being played was picked from the menu (go back to it after)

// ---- Each game's details, for the menu and the banners ----
typedef struct {
    const char *name;
    const char *lines[3];        // what it is, on the menu card
    const char *ready;           // the banner: "SURGERY READY"
    const char *keys;            // the banner: how to start it
    const char *rowName;         // ...and in short, for the row of banners in the 2D view
    const char *shortKeys;
    Color colour;
} GameInfo;

static const GameInfo INFO[MINIGAME_COUNT] = {
    { "Operation", { "Take the item out along", "the channel without", "touching the edge." },
      "SURGERY READY", "press O to operate", "SURGERY", "O", { 40, 140, 110, 255 } },
    { "Shock!", { "Charge, then shock as", "the spike passes the", "zone. Three good shocks." },
      "DEFIB NEEDED", "press G, then 2", "DEFIB", "G 2", { 190, 60, 50, 255 } },
    { "Find the vein", { "Feel for the vein, put", "the needle in, and stop", "when blood shows." },
      "BLOODS", "press G, then 3", "BLOODS", "G 3", { 60, 100, 190, 255 } },
    { "Hold still", { "Line up the X-ray or CT", "and take the image when", "the patient is still." },
      "SCAN READY", "press G, then 4", "SCAN", "G 4", { 70, 130, 170, 255 } },
    { "Triage call", { "Three new arrivals: drag", "each to emergency, urgent", "or routine." },
      NULL, NULL, NULL, NULL, { 80, 90, 160, 255 } },
    { "Pharmacy rush", { "Click the right pills", "into the bag before the", "clock runs out." },
      "PRESCRIPTION", "press G, then 6", "PHARMACY", "G 6", { 60, 140, 80, 255 } },
    { "Blue lights", { "Drive the ambulance in,", "weaving through traffic.", "Don't clip the cars!" },
      NULL, NULL, NULL, NULL, { 50, 70, 170, 255 } },
    { "Touchdown", { "Land the rescue", "helicopter on the pad", "in a gusty wind." },
      "HELI INBOUND", "press G, then 8", "HELI", "G 8", { 200, 110, 40, 255 } },
};

// The real patient a game could be played for now, or 0 (a practice round).
// Triage and the ambulance are always real: they bring new patients in.
static int offer_for(MiniGame g)
{
    switch (g) {
    case MINIGAME_OPERATION: return opgame_offer();
    case MINIGAME_SHOCK:     return defibgame_offer();
    case MINIGAME_VEIN:      return veingame_offer();
    case MINIGAME_SCAN:      return scangame_offer();
    case MINIGAME_PHARMACY:  return pharmgame_offer();
    case MINIGAME_LANDING:   return heligame_offer();
    default:                 return 0;
    }
}

static bool game_on(void)
{
    return opgame_active() || defibgame_active() || veingame_active() || scangame_active() ||
           triagegame_active() || pharmgame_active() || drivegame_active() || heligame_active();
}

// ---- The calls from main.c ----

void minigames_watch(const Sim *sim)
{
    opgame_watch(sim);
    defibgame_watch(sim);
    veingame_watch(sim);
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
    case MINIGAME_VEIN:      veingame_start(sim);   break;
    case MINIGAME_SCAN:      scangame_start(sim);   break;
    case MINIGAME_TRIAGE:    triagegame_start(sim); break;
    case MINIGAME_PHARMACY:  pharmgame_start(sim);  break;
    case MINIGAME_DRIVE:     drivegame_start();     break;
    case MINIGAME_LANDING:   heligame_start(sim);   break;
    default: break;
    }
}

bool minigames_rotor(void) { return heligame_flying(); }

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
        veingame_update(sim);
        scangame_update(sim);
        triagegame_update(sim);
        pharmgame_update(sim);
        drivegame_update(sim);
        heligame_update(sim);
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
    Color steel = { 200, 205, 215, 255 }, skin = { 232, 190, 160, 255 };
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
    } else if (g == MINIGAME_VEIN) {                 // an arm with a vein and a needle
        DrawRectangleRounded((Rectangle){ cx - 60, cy - 8, 120, 34 }, 0.8f, 8, skin);
        DrawLineEx((Vector2){ cx - 55, cy + 10 }, (Vector2){ cx + 55, cy + 4 }, 4, (Color){ 90, 110, 190, 200 });
        DrawLineEx((Vector2){ cx - 30, cy - 26 }, (Vector2){ cx + 2, cy + 6 }, 3, steel);
        DrawLineEx((Vector2){ cx - 42, cy - 38 }, (Vector2){ cx - 30, cy - 26 }, 10, (Color){ 230, 80, 80, 255 });
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
    if (veingame_active())  { veingame_draw(fit);  return; }
    if (scangame_active())  { scangame_draw(fit);  return; }
    if (triagegame_active()) { triagegame_draw(fit); return; }
    if (pharmgame_active()) { pharmgame_draw(fit); return; }
    if (drivegame_active()) { drivegame_draw(fit); return; }
    if (heligame_active())  { heligame_draw(fit);  return; }
    if (!menuOpen) return;

    ClearBackground(C_BG);
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 28, 34, 44, 255 });
    DrawText("MINI GAMES", SCREEN_W / 2 - MeasureText("MINI GAMES", 36) / 2, 22, 36, WHITE);
    const char *sub = "The hospital waits while you play. Win for a real patient and they get through quicker.";
    DrawText(sub, SCREEN_W / 2 - MeasureText(sub, 16) / 2, 72, 16, (Color){ 180, 190, 205, 255 });

    // How each game has gone this shift (real patients only): played, and lost
    const ShiftStats *s = &sim->stats;
    int played[MINIGAME_COUNT] = { s->surgeriesPlayed, s->shocksPlayed, s->veinsPlayed, s->scansPlayed,
                                   s->triageGames, s->pharmacyPlayed, s->drivesPlayed, s->landingsPlayed };
    int lost[MINIGAME_COUNT]   = { s->complications, s->shocksFailed, s->veinsFailed, s->scansFailed,
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

    const char *hint = "1 to 8 or click to play      G or BACKSPACE back to the hospital";
    DrawText(hint, SCREEN_W / 2 - MeasureText(hint, 18) / 2, 562, 18, (Color){ 220, 228, 238, 255 });
    EndMode2D();
}

void minigames_draw_offers(int centreX, int y, bool row)
{
    if (minigames_active()) return;
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
    char texts[MINIGAME_COUNT][80];
    Color colours[MINIGAME_COUNT];
    int n = 0;
    for (int i = 0; i < MINIGAME_COUNT; i++) {
        int id = offer_for((MiniGame)i);
        if (id == 0 || INFO[i].ready == NULL) continue;
        // In a row they're short: "SURGERY 38 (O)"; in a column, the full text
        if (row) snprintf(texts[n], sizeof texts[n], "%s %d (%s)", INFO[i].rowName, id, INFO[i].shortKeys);
        else     snprintf(texts[n], sizeof texts[n], "%s: patient %d   -   %s", INFO[i].ready, id, INFO[i].keys);
        colours[n] = INFO[i].colour;
        colours[n].a = (unsigned char)(200 + 55 * pulse);
        n++;
    }
    int size = row ? 14 : 18, height = row ? 24 : 30, gap = 8;
    int total = 0;
    for (int i = 0; i < n; i++) total += MeasureText(texts[i], size) + (row ? 20 : 30) + (i > 0 ? gap : 0);
    int x = centreX - total / 2;
    for (int i = 0; i < n; i++) {
        int w = MeasureText(texts[i], size) + (row ? 20 : 30);
        int bx = row ? x : centreX - w / 2;
        DrawRectangleRounded((Rectangle){ (float)bx, (float)y, (float)w, (float)height }, 0.5f, 8, colours[i]);
        DrawText(texts[i], bx + w / 2 - MeasureText(texts[i], size) / 2, y + (height - size) / 2, size, WHITE);
        if (row) x += w + gap;
        else     y += height + 6;
    }
}
