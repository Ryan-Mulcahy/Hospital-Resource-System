// opgame.c - the operation mini-game.
//
// The screen shows a patient on the table under green drapes, with a winding
// channel cut into the operating field. The item to remove (a piece of
// shrapnel, a kidney stone, an appendix) sits at one end and a steel tray at
// the other. Hold the left mouse button on the item to pick it up with the
// tweezers, then drag it along the channel to the tray. If the item touches
// the metal edge of the channel: BZZZT, complication. Let go and it stays
// where it is, so you can pick it up again. A heart monitor races as time
// runs out.
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "opgame.h"

// ---- The three operations ----
#define MAX_PATH 40

typedef struct {
    const char *title;       // shown at the top
    const char *item;        // what you're taking out
    Vector2 path[MAX_PATH];  // the middle of the channel, from the item to the tray
    int count;
} Level;

enum { LEVEL_STONE, LEVEL_SHRAPNEL, LEVEL_APPENDIX, LEVEL_COUNT };
static Level levels[LEVEL_COUNT];
static bool levelsMade = false;

static void make_levels(void)
{
    // Kidney stone: a long S-shaped channel
    Level *k = &levels[LEVEL_STONE];
    k->title = "Remove the kidney stone";
    k->item = "kidney stone";
    k->count = 0;
    for (int i = 0; i <= 24; i++) {
        float u = i / 24.0f;
        k->path[k->count++] = (Vector2){ 250 + 540 * u, 330 + 120 * sinf(u * 2.6f * PI) };
    }
    k->path[k->count++] = (Vector2){ 880, 330 + 120 * sinf(2.6f * PI) };

    // Shrapnel: sharp zig-zags
    Level *s = &levels[LEVEL_SHRAPNEL];
    s->title = "Remove the shrapnel";
    s->item = "shrapnel";
    Vector2 zig[] = { { 250, 220 }, { 360, 430 }, { 470, 210 }, { 580, 440 }, { 690, 220 }, { 770, 400 }, { 880, 400 } };
    s->count = (int)(sizeof zig / sizeof zig[0]);
    for (int i = 0; i < s->count; i++) s->path[i] = zig[i];

    // Appendix: square turns, like a maze
    Level *a = &levels[LEVEL_APPENDIX];
    a->title = "Remove the appendix";
    a->item = "appendix";
    Vector2 maze[] = { { 250, 450 }, { 250, 200 }, { 420, 200 }, { 420, 440 }, { 590, 440 },
                       { 590, 200 }, { 750, 200 }, { 750, 330 }, { 880, 330 } };
    a->count = (int)(sizeof maze / sizeof maze[0]);
    for (int i = 0; i < a->count; i++) a->path[i] = maze[i];

    levelsMade = true;
}

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_PLAYING, GAME_WON, GAME_LOST } GameState;

static GameState state = GAME_OFF;
static int   level;
static float halfWidth;          // half the channel's width: narrower for emergencies
static float timeLeft, timeLimit;
static Vector2 item;             // where the item is now
static bool  holding = false;    // picked up with the tweezers?
static Vector2 grab;             // where on the item the tweezers took hold (from the mouse)
static bool  practice;           // a practice round: changes nothing
static bool  outOfTime;
static int   patientId;          // the real patient being operated on (0 in practice)
static char  patientText[96];    // "Patient 42 - Burst appendix (K35.2)"
static const char *title;        // "Remove the shrapnel"
static const char *itemName;     // "shrapnel"
static float shake;              // the screen shakes when you touch the edge
static float beepAt;             // next heartbeat
static float heartPhase;

#define ITEM_RADIUS 11.0f
#define TRAY_X      880.0f

// ---- Offering the game when someone reaches theatre ----
static int offerId = 0;          // patient waiting in theatre, or 0
static int lastOffered = 0;      // so each patient is only offered once

void opgame_watch(const Sim *sim)
{
    const Patient *p = sim->rooms[ROOM_THEATRE].in[0];
    if (p == NULL || p->walkLeft > 0.0f) {          // nobody there yet
        if (p == NULL) offerId = 0;
        return;
    }
    if (p->id != lastOffered) {                     // just arrived: offer the game
        offerId = p->id;
        lastOffered = p->id;
    }
    if (p->id != offerId) offerId = 0;
}

void opgame_draw_offer(int centreX, int y)
{
    if (offerId == 0 || state != GAME_OFF) return;
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
    const char *text = TextFormat("SURGERY READY: patient %d   -   press O to operate", offerId);
    int w = MeasureText(text, 18) + 30;
    Color bg = { 40, 140, 110, (unsigned char)(200 + 55 * pulse) };
    DrawRectangleRounded((Rectangle){ (float)(centreX - w / 2), (float)y, (float)w, 30 }, 0.5f, 8, bg);
    DrawText(text, centreX - MeasureText(text, 18) / 2, y + 6, 18, WHITE);
}

// ---- Starting ----

// Which operation suits the patient's condition. Burns use the zig-zag
// channel too, to take out a piece of debris from the fire.
static int level_for(const char *condition)
{
    if (strstr(condition, "ppendix")) return LEVEL_APPENDIX;
    if (strstr(condition, "Kidney"))  return LEVEL_STONE;
    if (strstr(condition, "injur") || strstr(condition, "all") || strstr(condition, "Burn")) return LEVEL_SHRAPNEL;
    return GetRandomValue(0, LEVEL_COUNT - 1);
}

void opgame_start(const Sim *sim)
{
    if (!levelsMade) make_levels();
    const Patient *p = sim->rooms[ROOM_THEATRE].in[0];
    practice = !(offerId != 0 && p != NULL && p->id == offerId);
    if (practice) {
        level = GetRandomValue(0, LEVEL_COUNT - 1);
        halfWidth = 24.0f;
        timeLimit = 30.0f;
        patientId = 0;
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
    } else {
        level = level_for(p->condition->name);
        // Emergencies are harder: a narrower channel and less time
        halfWidth = (p->severity == 1) ? 19.0f : (p->severity == 2) ? 22.0f : 24.0f;
        timeLimit = (p->severity == 1) ? 22.0f : 26.0f;
        patientId = p->id;
        snprintf(patientText, sizeof patientText, "Patient %d  -  %s (%s)", p->id, p->condition->name, p->condition->icd10);
        offerId = 0;
    }
    title = levels[level].title;
    itemName = levels[level].item;
    if (!practice && strstr(p->condition->name, "Burn")) {
        title = "Remove the debris";
        itemName = "debris";
    }
    timeLeft = timeLimit;
    item = levels[level].path[0];
    holding = false;
    outOfTime = false;
    shake = 0;
    beepAt = 0;
    state = GAME_PLAYING;
}

bool opgame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

// How far point p is from the middle of the channel
static float distance_to_path(Vector2 p)
{
    const Level *L = &levels[level];
    float best = 1e9f;
    for (int i = 0; i + 1 < L->count; i++) {
        Vector2 a = L->path[i], b = L->path[i + 1];
        float abx = b.x - a.x, aby = b.y - a.y;
        float t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / (abx * abx + aby * aby);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
        float d = sqrtf(dx * dx + dy * dy);
        if (d < best) best = d;
    }
    return best;
}

static void finish(Sim *sim, bool won)
{
    state = won ? GAME_WON : GAME_LOST;
    holding = false;
    sound_play(won ? SND_SUCCESS : SND_BUZZ);
    if (!won) shake = 0.5f;
    if (!practice) sim_surgery_result(sim, patientId, won);
}

void opgame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();
    if (shake > 0) shake -= dt;

    if (state != GAME_PLAYING) {
        // Result screen: carry on, or (practice) try again
        if (practice && IsKeyPressed(KEY_R)) { opgame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_C) ||
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) {                       // give up
        finish(sim, false);
        return;
    }

    timeLeft -= dt;
    if (timeLeft <= 0) {
        timeLeft = 0;
        outOfTime = true;
        finish(sim, false);
        return;
    }

    // Heartbeat: faster as time runs out
    float rate = 1.1f + 1.4f * (1.0f - timeLeft / timeLimit);   // beats per second
    heartPhase += dt * rate;
    beepAt -= dt;
    if (beepAt <= 0) { sound_play(SND_BEEP); beepAt = 1.0f / rate; }

    Vector2 m = GetMousePosition();
    if (!holding) {
        // Pick the item up by clicking on it
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            float dx = m.x - item.x, dy = m.y - item.y;
            if (dx * dx + dy * dy < 22 * 22) {
                holding = true;
                grab = (Vector2){ item.x - m.x, item.y - m.y };   // so it doesn't jump to the mouse
            }
        }
        return;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {     // let go: it stays where it is
        holding = false;
        return;
    }
    item = (Vector2){ m.x + grab.x, m.y + grab.y };
    if (distance_to_path(item) > halfWidth - ITEM_RADIUS) {   // touched the edge
        finish(sim, false);
        return;
    }
    if (item.x >= TRAY_X - 6) finish(sim, true);     // in the tray
}

// ---- Drawing ----

static void draw_channel(Color c, float half)
{
    const Level *L = &levels[level];
    for (int i = 0; i + 1 < L->count; i++) DrawLineEx(L->path[i], L->path[i + 1], half * 2, c);
    for (int i = 0; i < L->count; i++) DrawCircleV(L->path[i], half, c);      // round the corners
}

static void draw_item(Vector2 p, bool lifted)
{
    float lift = lifted ? -4.0f : 0.0f;
    DrawCircleV((Vector2){ p.x + 3, p.y + 4 }, ITEM_RADIUS, (Color){ 0, 0, 0, 60 });   // shadow
    p.y += lift;
    if (level == LEVEL_STONE) {
        DrawCircleV(p, ITEM_RADIUS, (Color){ 200, 170, 90, 255 });
        DrawCircleV((Vector2){ p.x - 3, p.y - 3 }, 3, (Color){ 230, 205, 140, 255 });
    } else if (level == LEVEL_SHRAPNEL) {
        DrawPoly(p, 5, ITEM_RADIUS + 1, 20, (Color){ 120, 125, 135, 255 });
        DrawPoly(p, 3, ITEM_RADIUS - 2, 50, (Color){ 170, 175, 185, 255 });
    } else {
        DrawEllipse((int)p.x, (int)p.y, ITEM_RADIUS + 3, ITEM_RADIUS - 2, (Color){ 230, 140, 150, 255 });
        DrawEllipse((int)p.x + 4, (int)p.y, 4, 3, (Color){ 200, 100, 115, 255 });
    }
}

static void draw_tweezers(Vector2 p, bool closed)
{
    float open = closed ? 2.0f : 7.0f;
    Color steel = { 200, 205, 215, 255 }, dark = { 110, 115, 125, 255 };
    DrawLineEx((Vector2){ p.x - open, p.y }, (Vector2){ p.x + 30, p.y - 60 }, 4, dark);
    DrawLineEx((Vector2){ p.x + open, p.y }, (Vector2){ p.x + 38, p.y - 56 }, 4, dark);
    DrawLineEx((Vector2){ p.x - open, p.y }, (Vector2){ p.x + 30, p.y - 60 }, 2, steel);
    DrawLineEx((Vector2){ p.x + open, p.y }, (Vector2){ p.x + 38, p.y - 56 }, 2, steel);
}

// A heart trace across the box, beating at the current rate; flat if lost.
static void draw_monitor(Rectangle r)
{
    DrawRectangleRounded(r, 0.15f, 6, (Color){ 12, 24, 18, 255 });
    bool flat = (state == GAME_LOST);
    Color line = flat ? (Color){ 255, 80, 90, 255 } : (Color){ 80, 255, 140, 255 };
    Vector2 prev = { 0 };
    for (int i = 0; i <= 60; i++) {
        float u = i / 60.0f;
        float b = fmodf(u * 3.0f - heartPhase, 1.0f);
        if (b < 0) b += 1.0f;
        float v = 0;
        if (!flat) {
            if (b > 0.40f && b < 0.44f)      v = (b - 0.40f) / 0.04f;
            else if (b > 0.44f && b < 0.50f) v = 1.0f - (b - 0.44f) / 0.06f * 1.4f;
            else if (b > 0.62f && b < 0.78f) v = 0.25f * sinf((b - 0.62f) / 0.16f * PI);
        }
        Vector2 pt = { r.x + 8 + u * (r.width - 16), r.y + r.height * 0.62f - v * r.height * 0.4f };
        if (i > 0) DrawLineEx(prev, pt, 2, line);
        prev = pt;
    }
    float rate = 1.1f + 1.4f * (1.0f - timeLeft / timeLimit);
    DrawText(flat ? "---" : TextFormat("%d bpm", (int)(rate * 60)), (int)r.x + 8, (int)r.y + 6, 14, line);
}

static void button_hint(const char *text, int y)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, 18) / 2, y, 18, (Color){ 230, 240, 235, 255 });
}

void opgame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    const Level *L = &levels[level];

    // The screen shakes for a moment when you touch the edge
    float sx = 0, sy = 0;
    if (shake > 0) { sx = sinf((float)GetTime() * 90.0f) * 8.0f * shake; sy = cosf((float)GetTime() * 70.0f) * 6.0f * shake; }
    Camera2D shakeCam = fit;                      // the usual fit to the window, nudged
    shakeCam.offset.x += sx * fit.zoom;
    shakeCam.offset.y += sy * fit.zoom;

    ClearBackground((Color){ 24, 70, 62, 255 });        // theatre green
    BeginMode2D(shakeCam);

    // Drapes, and the patient's skin showing through the opening
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 40, 112, 98, 255 });
    for (int i = 0; i < 12; i++) DrawLine(0, 90 + i * 45, SCREEN_W, 90 + i * 45, (Color){ 34, 100, 88, 255 });   // folds
    Rectangle field = { 200, 150, 640, 380 };
    DrawRectangleRounded(field, 0.12f, 10, (Color){ 236, 196, 168, 255 });
    DrawRectangleRoundedLines(field, 0.12f, 10, (Color){ 30, 90, 78, 255 });
    DrawRectangleRounded((Rectangle){ field.x + 14, field.y + 14, field.width - 28, field.height - 28 }, 0.1f, 10,
                         (Color){ 242, 206, 180, 255 });

    // The channel: a steel rim, then the opening
    draw_channel((Color){ 150, 156, 168, 255 }, halfWidth + 5);
    draw_channel((Color){ 205, 210, 220, 255 }, halfWidth + 2);
    draw_channel((Color){ 120, 40, 45, 255 }, halfWidth);
    draw_channel((Color){ 150, 55, 60, 255 }, halfWidth * 0.55f);

    // The tray at the end
    DrawRectangleRounded((Rectangle){ TRAY_X - 6, L->path[L->count - 1].y - 34, 90, 68 }, 0.3f, 8, (Color){ 180, 186, 196, 255 });
    DrawRectangleRounded((Rectangle){ TRAY_X, L->path[L->count - 1].y - 28, 78, 56 }, 0.3f, 8, (Color){ 215, 220, 228, 255 });
    DrawText("TRAY", (int)TRAY_X + 18, (int)L->path[L->count - 1].y + 12, 14, (Color){ 120, 125, 135, 255 });

    // The item, and the tweezers at the mouse
    Vector2 m = GetMousePosition();
    draw_item(item, holding);
    if (state == GAME_PLAYING) draw_tweezers(holding ? item : m, holding);

    // The top bar: what you're doing, who for, and the clock
    DrawRectangle(0, 0, SCREEN_W, 84, (Color){ 18, 50, 44, 255 });
    DrawText(TextFormat("OPERATION: %s", title), 24, 14, 26, WHITE);
    DrawText(patientText, 24, 50, 16, (Color){ 170, 210, 200, 255 });
    float frac = timeLeft / timeLimit;
    Color timeCol = frac > 0.5f ? (Color){ 90, 220, 140, 255 } : frac > 0.25f ? (Color){ 240, 190, 60, 255 } : (Color){ 240, 80, 80, 255 };
    DrawRectangleRounded((Rectangle){ 560, 54, 230, 12 }, 1.0f, 6, (Color){ 10, 30, 26, 255 });
    DrawRectangleRounded((Rectangle){ 560, 54, 230 * frac, 12 }, 1.0f, 6, timeCol);
    DrawText(TextFormat("%.0f s", ceilf(timeLeft)), 560, 30, 18, timeCol);
    draw_monitor((Rectangle){ 810, 10, 176, 64 });

    if (state == GAME_PLAYING) {
        const char *hint = holding ? TextFormat("Steady... take the %s to the tray", itemName)
                                   : TextFormat("Click and hold on the %s to pick it up", itemName);
        DrawText(hint, SCREEN_W / 2 - MeasureText(hint, 18) / 2, 550, 18, (Color){ 220, 240, 232, 255 });
        DrawText("Q give up", SCREEN_W - 110, 572, 14, (Color){ 150, 190, 180, 255 });
    }
    EndMode2D();

    // The result
    BeginMode2D(fit);
    if (state == GAME_WON || state == GAME_LOST) {
        bool won = (state == GAME_WON);
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, won ? (Color){ 20, 80, 50, 150 } : (Color){ 120, 10, 20, 150 });
        Rectangle box = { 250, 200, 500, 200 };
        DrawRectangleRounded(box, 0.15f, 10, won ? (Color){ 30, 120, 80, 245 } : (Color){ 150, 30, 40, 245 });
        const char *head = won ? "SURGERY SUCCESSFUL" : (outOfTime ? "OUT OF TIME" : "BZZZT! COMPLICATION");
        DrawText(head, SCREEN_W / 2 - MeasureText(head, 32) / 2, 225, 32, WHITE);
        const char *body;
        if (practice) body = won ? "Steady hands! (practice round)" : "You touched the edge. (practice round)";
        else          body = won ? "The patient will recover sooner." : "The patient will need longer in theatre.";
        if (!won && outOfTime) body = practice ? "The clock ran out. (practice round)" : "The clock ran out: a longer operation.";
        DrawText(body, SCREEN_W / 2 - MeasureText(body, 20) / 2, 280, 20, (Color){ 235, 245, 240, 255 });
        if (practice) button_hint("R try again    -    click or ENTER to carry on", 345);
        else          button_hint("Click or press ENTER to carry on", 345);
    }
    EndMode2D();
}
