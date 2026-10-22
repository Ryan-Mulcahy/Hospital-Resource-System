// minigames.c - the mini-game menu, and passing input and drawing on to
// whichever game is being played (opgame.c, defibgame.c, veingame.c).
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
#include "minigames.h"

static bool menuOpen = false;
static bool fromMenu = false;    // the game being played was picked from the menu (go back to it after)

// ---- Each game's details, for the menu and the banners ----
typedef struct {
    const char *name;
    const char *lines[3];        // what it is, on the menu card
    const char *ready;           // the banner: "SURGERY READY"
    const char *keys;            // the banner: how to start it
    const char *shortKeys;       // ...and in short, for the row of banners in the 2D view
    Color colour;
} GameInfo;

static const GameInfo INFO[MINIGAME_COUNT] = {
    { "Operation", { "Take the item out with the", "tweezers, along the channel,", "without touching the edge." },
      "SURGERY READY", "press O to operate", "O", { 40, 140, 110, 255 } },
    { "Shock!", { "Charge the defibrillator and", "shock as the spike passes", "the zone. Three good shocks." },
      "DEFIB NEEDED", "press G, then 2", "G, 2", { 190, 60, 50, 255 } },
    { "Find the vein", { "Feel for the vein, put the", "needle in, and stop as soon", "as blood shows." },
      "BLOODS", "press G, then 3", "G, 3", { 60, 100, 190, 255 } },
};

// The real patient a game could be played for now, or 0 (a practice round)
static int offer_for(MiniGame g)
{
    switch (g) {
    case MINIGAME_OPERATION: return opgame_offer();
    case MINIGAME_SHOCK:     return defibgame_offer();
    case MINIGAME_VEIN:      return veingame_offer();
    default:                 return 0;
    }
}

static bool game_on(void) { return opgame_active() || defibgame_active() || veingame_active(); }

// ---- The calls from main.c ----

void minigames_watch(const Sim *sim)
{
    opgame_watch(sim);
    defibgame_watch(sim);
    veingame_watch(sim);
}

void minigames_open_menu(void) { menuOpen = true; }

static void start_game(const Sim *sim, MiniGame g)
{
    switch (g) {
    case MINIGAME_OPERATION: opgame_start(sim);    break;
    case MINIGAME_SHOCK:     defibgame_start(sim); break;
    case MINIGAME_VEIN:      veingame_start(sim);  break;
    default: break;
    }
}

void minigames_start(const Sim *sim, MiniGame game)
{
    menuOpen = false;
    fromMenu = false;
    start_game(sim, game);
}

bool minigames_active(void) { return menuOpen || game_on(); }

// Where each card is on the menu
static Rectangle card_rect(int i) { return (Rectangle){ 50.0f + i * 310.0f, 150.0f, 280.0f, 300.0f }; }

void minigames_update(Sim *sim)
{
    if (game_on()) {
        opgame_update(sim);
        defibgame_update(sim);
        veingame_update(sim);
        if (!game_on() && fromMenu) menuOpen = true;   // finished: back to the menu
        return;
    }
    if (!menuOpen) return;

    // The menu: 1-3 or a click picks a game; G or BACKSPACE goes back to the hospital
    if (IsKeyPressed(KEY_G) || IsKeyPressed(KEY_BACKSPACE)) { menuOpen = false; return; }
    int pick = -1;
    if (IsKeyPressed(KEY_ONE)   || IsKeyPressed(KEY_KP_1)) pick = MINIGAME_OPERATION;
    if (IsKeyPressed(KEY_TWO)   || IsKeyPressed(KEY_KP_2)) pick = MINIGAME_SHOCK;
    if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) pick = MINIGAME_VEIN;
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
    if (g == MINIGAME_OPERATION) {                   // tweezers over a channel
        DrawLineEx((Vector2){ cx - 50, cy + 18 }, (Vector2){ cx + 50, cy + 18 }, 14, (Color){ 200, 205, 215, 255 });
        DrawLineEx((Vector2){ cx - 50, cy + 18 }, (Vector2){ cx + 50, cy + 18 }, 9, (Color){ 120, 40, 45, 255 });
        DrawCircleV((Vector2){ cx - 8, cy + 18 }, 5, (Color){ 200, 170, 90, 255 });
        DrawLineEx((Vector2){ cx - 12, cy + 12 }, (Vector2){ cx + 20, cy - 32 }, 4, (Color){ 200, 205, 215, 255 });
        DrawLineEx((Vector2){ cx - 4, cy + 12 }, (Vector2){ cx + 28, cy - 28 }, 4, (Color){ 200, 205, 215, 255 });
    } else if (g == MINIGAME_SHOCK) {                // a heart trace with a lightning bolt
        Vector2 pts[] = { { cx - 55, cy + 10 }, { cx - 25, cy + 10 }, { cx - 15, cy - 30 }, { cx - 5, cy + 30 },
                          { cx + 5, cy + 10 }, { cx + 55, cy + 10 } };
        for (int i = 0; i + 1 < 6; i++) DrawLineEx(pts[i], pts[i + 1], 3, (Color){ 80, 255, 140, 255 });
        DrawTriangle((Vector2){ cx + 30, cy - 38 }, (Vector2){ cx + 18, cy - 8 }, (Vector2){ cx + 32, cy - 12 }, (Color){ 255, 210, 60, 255 });
        DrawTriangle((Vector2){ cx + 32, cy - 16 }, (Vector2){ cx + 20, cy - 12 }, (Vector2){ cx + 26, cy + 14 }, (Color){ 255, 210, 60, 255 });
    } else {                                         // an arm with a vein and a needle
        DrawRectangleRounded((Rectangle){ cx - 60, cy - 14, 120, 36 }, 0.8f, 8, (Color){ 232, 190, 160, 255 });
        DrawLineEx((Vector2){ cx - 55, cy + 6 }, (Vector2){ cx + 55, cy }, 4, (Color){ 90, 110, 190, 200 });
        DrawLineEx((Vector2){ cx - 30, cy - 34 }, (Vector2){ cx + 2, cy + 2 }, 3, (Color){ 210, 215, 225, 255 });
        DrawLineEx((Vector2){ cx - 46, cy - 52 }, (Vector2){ cx - 30, cy - 34 }, 10, (Color){ 230, 80, 80, 255 });
    }
}

void minigames_draw(const Sim *sim, Camera2D fit)
{
    if (opgame_active())    { opgame_draw(fit);    return; }
    if (defibgame_active()) { defibgame_draw(fit); return; }
    if (veingame_active())  { veingame_draw(fit);  return; }
    if (!menuOpen) return;

    ClearBackground(C_BG);
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 28, 34, 44, 255 });
    DrawText("MINI GAMES", SCREEN_W / 2 - MeasureText("MINI GAMES", 40) / 2, 40, 40, WHITE);
    const char *sub = "The hospital waits while you play. Win for a real patient and they get through quicker.";
    DrawText(sub, SCREEN_W / 2 - MeasureText(sub, 16) / 2, 96, 16, (Color){ 180, 190, 205, 255 });

    // How each game has gone this shift (real patients only): won, played
    const ShiftStats *s = &sim->stats;
    int played[MINIGAME_COUNT] = { s->surgeriesPlayed, s->shocksPlayed, s->veinsPlayed };
    int lost[MINIGAME_COUNT]   = { s->complications, s->shocksFailed, s->veinsFailed };

    Vector2 m = GetMousePosition();
    for (int i = 0; i < MINIGAME_COUNT; i++) {
        Rectangle r = card_rect(i);
        bool hover = CheckCollisionPointRec(m, r);
        Color c = INFO[i].colour;
        DrawRectangleRounded(r, 0.08f, 10, hover ? (Color){ 52, 62, 78, 255 } : (Color){ 40, 48, 62, 255 });
        DrawRectangleRounded((Rectangle){ r.x, r.y, r.width, 46 }, 0.4f, 10, c);
        DrawRectangle((int)r.x, (int)r.y + 30, (int)r.width, 16, c);           // square off the bottom of the header
        if (hover) DrawRectangleRoundedLines(r, 0.08f, 10, (Color){ 255, 255, 255, 160 });
        DrawText(TextFormat("%d", i + 1), (int)r.x + 14, (int)r.y + 10, 28, (Color){ 255, 255, 255, 200 });
        DrawText(INFO[i].name, (int)r.x + 44, (int)r.y + 12, 24, WHITE);

        draw_icon((MiniGame)i, r.x + r.width / 2, r.y + 100);
        for (int k = 0; k < 3; k++)
            DrawText(INFO[i].lines[k], (int)r.x + 18, (int)r.y + 158 + k * 20, 16, (Color){ 210, 218, 230, 255 });

        // A real patient ready for it, or a practice round
        int id = offer_for((MiniGame)i);
        Rectangle tag = { r.x + 14, r.y + 226, r.width - 28, 28 };
        if (id != 0) {
            float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
            DrawRectangleRounded(tag, 0.5f, 8, (Color){ 40, 150, 90, (unsigned char)(190 + 65 * pulse) });
            const char *t = TextFormat("Patient %d is ready", id);
            DrawText(t, (int)(tag.x + tag.width / 2) - MeasureText(t, 16) / 2, (int)tag.y + 6, 16, WHITE);
        } else {
            DrawRectangleRounded(tag, 0.5f, 8, (Color){ 70, 78, 92, 255 });
            const char *t = "Practice round";
            DrawText(t, (int)(tag.x + tag.width / 2) - MeasureText(t, 16) / 2, (int)tag.y + 6, 16, (Color){ 200, 205, 215, 255 });
        }
        const char *score = played[i] == 0 ? "This shift: none played yet"
                                           : TextFormat("This shift: %d won of %d", played[i] - lost[i], played[i]);
        DrawText(score, (int)r.x + 18, (int)r.y + 268, 14, (Color){ 160, 172, 190, 255 });
    }

    const char *hint = "1, 2, 3 or click to play      G or BACKSPACE back to the hospital";
    DrawText(hint, SCREEN_W / 2 - MeasureText(hint, 18) / 2, 500, 18, (Color){ 220, 228, 238, 255 });
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
        if (id == 0) continue;
        // In a row they're short: "SURGERY READY: 38 (O)"; in a column, the full text
        if (row) snprintf(texts[n], sizeof texts[n], "%s: %d (%s)", INFO[i].ready, id, INFO[i].shortKeys);
        else     snprintf(texts[n], sizeof texts[n], "%s: patient %d   -   %s", INFO[i].ready, id, INFO[i].keys);
        colours[n] = INFO[i].colour;
        colours[n].a = (unsigned char)(200 + 55 * pulse);
        n++;
    }
    int size = row ? 16 : 18, height = row ? 26 : 30, gap = 12;
    int total = 0;
    for (int i = 0; i < n; i++) total += MeasureText(texts[i], size) + 24 + (i > 0 ? gap : 0);
    int x = centreX - total / 2;
    for (int i = 0; i < n; i++) {
        int w = MeasureText(texts[i], size) + (row ? 24 : 30);
        int bx = row ? x : centreX - w / 2;
        DrawRectangleRounded((Rectangle){ (float)bx, (float)y, (float)w, (float)height }, 0.5f, 8, colours[i]);
        DrawText(texts[i], bx + w / 2 - MeasureText(texts[i], size) / 2, y + (height - size) / 2, size, WHITE);
        if (row) x += w + gap;
        else     y += height + 6;
    }
}
