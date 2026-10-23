// heligame.c - "Touchdown", landing the rescue helicopter.
//
// Seen from the side: sky, the hospital on the right, and the helipad (a
// painted H) on the ground in front of it. The helicopter comes in from the
// top left. It sinks slowly unless you hold UP; DOWN brings it down faster;
// LEFT/RIGHT push it across. The wind pushes it too, in gusts (the windsock
// and the arrow show which way). A landing counts as good if it's on the
// pad, coming down slowly and not sliding sideways.
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "conditions.h"      // rescue service names
#include "gameui.h"
#include "heligame.h"

#define GROUND_Y   500.0f        // where the skids touch down
#define PAD_LEFT   600.0f
#define PAD_RIGHT  760.0f
#define SINK       50.0f         // downward pull (pixels per second, per second)
#define LIFT       115.0f        // UP pushes back this much
#define DIVE       80.0f         // DOWN adds this much
#define PUSH       220.0f        // LEFT/RIGHT
#define SAFE_DOWN  65.0f         // the fastest a good landing can be coming down
#define SAFE_SIDE  45.0f         // ...and sliding sideways
#define TIME_LIMIT 30.0f

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_FLYING, GAME_DONE } GameState;
typedef enum { LANDED_WELL, LANDED_HARD, LANDED_OFF_PAD, LANDED_SIDEWAYS, LANDED_LATE, LANDED_GAVE_UP } Landing;

static GameState state = GAME_OFF;
static Landing landing;
static Vector2 pos;              // the middle of the skids
static Vector2 vel;
static float wind, windTarget;   // sideways push from the wind, and what it's heading to
static float nextGust;
static float timeLeft;
static float rotor;              // the blades' angle, for drawing
static float resultDelay;
static bool  practice;
static int   patientId;
static int   service;            // which rescue service (for the colours)
static char  patientText[96];
static Vector2 touchVel;         // how fast it was going when it touched down

// ---- Offering the game ----
static int offerId = 0;
#define REMEMBER 8
static int offered[REMEMBER];    // recent patients already offered, so each one only gets offered once
static int offeredNext = 0;

static bool was_offered(int id)
{
    for (int i = 0; i < REMEMBER; i++) if (offered[i] == id) return true;
    return false;
}

static const Patient *in_the_air(const Sim *sim, int id)
{
    for (const Patient *p = sim->inbound; p != NULL; p = p->next)
        if (id == 0 || p->id == id) return p;
    return NULL;
}

void heligame_watch(const Sim *sim)
{
    if (offerId != 0 && in_the_air(sim, offerId) == NULL) offerId = 0;   // it's landed
    if (offerId != 0 || state != GAME_OFF) return;
    for (const Patient *p = sim->inbound; p != NULL; p = p->next) {
        if (was_offered(p->id)) continue;
        offerId = p->id;
        offered[offeredNext] = p->id;
        offeredNext = (offeredNext + 1) % REMEMBER;
        return;
    }
}

int heligame_offer(void) { return offerId; }

// ---- Starting ----
void heligame_start(const Sim *sim)
{
    const Patient *p = (offerId != 0) ? in_the_air(sim, offerId) : NULL;
    practice = (p == NULL);
    if (practice) {
        patientId = 0;
        service = GetRandomValue(0, SERVICE_COUNT - 1);
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
    } else {
        patientId = p->id;
        service = p->rescueService;
        snprintf(patientText, sizeof patientText, "%s rescue: patient %d  -  %s",
                 rescue_service_name((RescueService)service), p->id, p->condition->name);
        offerId = 0;
    }
    pos = (Vector2){ 120, 140 };
    vel = (Vector2){ 70, 0 };
    wind = 0;
    windTarget = (float)GetRandomValue(-60, 60);
    nextGust = 1.5f;
    timeLeft = TIME_LIMIT;
    rotor = 0;
    state = GAME_FLYING;
}

bool heligame_active(void) { return state != GAME_OFF; }
bool heligame_flying(void) { return state == GAME_FLYING; }

// ---- Flying ----
static void finish(Sim *sim, Landing how)
{
    landing = how;
    state = GAME_DONE;
    resultDelay = 0.5f;
    bool won = (how == LANDED_WELL);
    sound_play(won ? SND_SUCCESS : SND_BUZZ);
    if (!practice && how != LANDED_GAVE_UP) sim_landing_result(sim, patientId, won);
}

void heligame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();

    if (state == GAME_DONE) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (practice && IsKeyPressed(KEY_R)) { heligame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { finish(sim, LANDED_GAVE_UP); return; }

    timeLeft -= dt;
    if (timeLeft <= 0) { timeLeft = 0; finish(sim, LANDED_LATE); return; }
    rotor += dt * 30.0f;

    // The wind: a new gust every few seconds, eased into
    nextGust -= dt;
    if (nextGust <= 0) {
        windTarget = (float)GetRandomValue(-90, 90);
        nextGust = GetRandomValue(15, 35) / 10.0f;
    }
    wind += (windTarget - wind) * fminf(1.0f, dt * 1.5f);

    // The controls, then the helicopter moves
    float ax = wind, ay = SINK;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  ax -= PUSH;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) ax += PUSH;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    ay -= LIFT;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  ay += DIVE;
    vel.x += ax * dt;
    vel.y += ay * dt;
    vel.x -= vel.x * 0.8f * dt;                      // air slows it down
    vel.y -= vel.y * 0.5f * dt;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (pos.x < 60) { pos.x = 60; vel.x = 0; }
    if (pos.x > 940) { pos.x = 940; vel.x = 0; }
    if (pos.y < 110) { pos.y = 110; vel.y = 0; }

    // Touchdown
    if (pos.y >= GROUND_Y) {
        pos.y = GROUND_Y;
        touchVel = vel;
        if (pos.x < PAD_LEFT + 10 || pos.x > PAD_RIGHT - 10) finish(sim, LANDED_OFF_PAD);
        else if (vel.y > SAFE_DOWN)                      finish(sim, LANDED_HARD);
        else if (fabsf(vel.x) > SAFE_SIDE)               finish(sim, LANDED_SIDEWAYS);
        else                                             finish(sim, LANDED_WELL);
        vel = (Vector2){ 0, 0 };
    }
}

// ---- Drawing ----

static void draw_helicopter(Vector2 skid)
{
    static const Color BODY[SERVICE_COUNT] = { { 220, 60, 50, 255 }, { 240, 190, 40, 255 }, { 200, 40, 40, 255 } };
    Color body = BODY[service % SERVICE_COUNT];
    Color dark = { 40, 44, 52, 255 };
    float tilt = vel.x * 0.002f;                     // leans into the way it's going
    Vector2 c = { skid.x, skid.y - 38 };
    // Skids
    DrawLineEx((Vector2){ skid.x - 40, skid.y }, (Vector2){ skid.x + 40, skid.y }, 4, dark);
    DrawLineEx((Vector2){ skid.x - 22, skid.y }, (Vector2){ skid.x - 18, skid.y - 18 }, 3, dark);
    DrawLineEx((Vector2){ skid.x + 22, skid.y }, (Vector2){ skid.x + 18, skid.y - 18 }, 3, dark);
    // Tail boom and tail rotor
    Vector2 tail = { c.x - 90 * cosf(tilt), c.y - 6 - 90 * sinf(tilt) };
    DrawLineEx((Vector2){ c.x - 30, c.y - 4 }, tail, 9, body);
    DrawCircleV(tail, 13, (Color){ 200, 205, 210, 90 });
    DrawLineEx((Vector2){ tail.x, tail.y - 13 * cosf(rotor) }, (Vector2){ tail.x, tail.y + 13 * cosf(rotor) }, 3, dark);
    // The cabin, with its window
    DrawEllipse((int)c.x, (int)c.y, 46, 22, body);
    DrawEllipse((int)c.x + 20, (int)c.y - 4, 20, 12, (Color){ 150, 200, 230, 255 });
    DrawRectangle((int)c.x - 22, (int)c.y - 2, 30, 6, WHITE);
    // The main rotor: a blur, with a blade showing as it turns
    Vector2 hub = { c.x, c.y - 24 };
    DrawLineEx((Vector2){ hub.x, hub.y + 2 }, (Vector2){ hub.x, hub.y + 10 }, 4, dark);
    DrawEllipse((int)hub.x, (int)hub.y, 85, 5, (Color){ 60, 64, 70, 70 });
    float b = 85 * cosf(rotor);
    DrawLineEx((Vector2){ hub.x - b, hub.y }, (Vector2){ hub.x + b, hub.y }, 3, dark);
}

void heligame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    ClearBackground((Color){ 130, 180, 225, 255 });
    BeginMode2D(fit);
    DrawRectangleGradientV(0, 0, SCREEN_W, (int)GROUND_Y, (Color){ 110, 160, 215, 255 }, (Color){ 190, 215, 235, 255 });

    // Clouds drifting with the wind
    float t = (float)GetTime();
    for (int i = 0; i < 5; i++) {
        float x = fmodf(i * 230.0f + t * (20 + wind * 0.3f) + 2000.0f, SCREEN_W + 300.0f) - 150;
        float y = 120.0f + (i % 3) * 70;
        DrawEllipse((int)x, (int)y, 70, 22, (Color){ 250, 252, 255, 200 });
        DrawEllipse((int)x + 40, (int)y - 12, 46, 20, (Color){ 250, 252, 255, 200 });
    }

    // The ground, the hospital and the helipad
    DrawRectangle(0, (int)GROUND_Y, SCREEN_W, SCREEN_H, (Color){ 90, 130, 80, 255 });
    DrawRectangle(800, 250, 200, (int)GROUND_Y - 250, (Color){ 225, 228, 235, 255 });
    for (int r = 0; r < 4; r++) for (int k = 0; k < 3; k++)
        DrawRectangle(820 + k * 60, 275 + r * 55, 40, 30, (Color){ 120, 160, 200, 255 });
    DrawRectangle(800, 250, 200, 16, (Color){ 200, 60, 60, 255 });
    DrawRectangle((int)PAD_LEFT, (int)GROUND_Y, (int)(PAD_RIGHT - PAD_LEFT), 14, (Color){ 70, 74, 80, 255 });
    DrawText("H", (int)(PAD_LEFT + PAD_RIGHT) / 2 - 10, (int)GROUND_Y - 2, 18, (Color){ 250, 250, 250, 255 });
    DrawRectangle((int)PAD_LEFT, (int)GROUND_Y, 8, 14, (Color){ 250, 200, 60, 255 });
    DrawRectangle((int)PAD_RIGHT - 8, (int)GROUND_Y, 8, 14, (Color){ 250, 200, 60, 255 });

    // The windsock: points the way the wind blows, straighter when it's stronger
    Vector2 pole = { 520, GROUND_Y };
    DrawLineEx(pole, (Vector2){ pole.x, pole.y - 80 }, 3, (Color){ 80, 80, 80, 255 });
    float strength = fminf(fabsf(wind) / 90.0f, 1.0f);
    float dir = (wind >= 0) ? 1.0f : -1.0f;
    for (int k = 0; k < 4; k++) {
        float x0 = pole.x + dir * k * 12, x1 = pole.x + dir * (k + 1) * 12;
        float droop = (1 - strength) * 6.0f;
        DrawLineEx((Vector2){ x0, pole.y - 76 + droop * k }, (Vector2){ x1, pole.y - 76 + droop * (k + 1) }, 10,
                   k % 2 ? WHITE : (Color){ 240, 110, 40, 255 });
    }

    draw_helicopter(pos);

    // The gauges: coming down, sliding, and the wind
    Color ok = { 40, 160, 80, 255 }, bad = { 210, 60, 50, 255 };
    DrawRectangleRounded((Rectangle){ 20, 100, 230, 110 }, 0.15f, 8, (Color){ 255, 255, 255, 200 });
    DrawText(TextFormat("Coming down: %3.0f", fmaxf(vel.y, 0)), 34, 112, 18, vel.y <= SAFE_DOWN ? ok : bad);
    DrawText(TextFormat("Sliding:     %3.0f", fabsf(vel.x)), 34, 140, 18, fabsf(vel.x) <= SAFE_SIDE ? ok : bad);
    DrawText("Wind", 34, 170, 18, C_INK);
    float wl = wind * 0.8f;
    DrawLineEx((Vector2){ 150, 180 }, (Vector2){ 150 + wl, 180 }, 5, C_BRAND);
    if (fabsf(wl) > 4) {                             // the arrow's head
        Vector2 tip = { 150 + wl + dir * 8, 180 };
        DrawLineEx(tip, (Vector2){ tip.x - dir * 10, 172 }, 4, C_BRAND);
        DrawLineEx(tip, (Vector2){ tip.x - dir * 10, 188 }, 4, C_BRAND);
    }

    gameui_top_bar("TOUCHDOWN: land the rescue helicopter", patientText, timeLeft, TIME_LIMIT, (Color){ 30, 60, 100, 255 });

    if (state == GAME_FLYING) {
        gameui_centre_text("LEFT / RIGHT to move, hold UP to hover, DOWN to come down faster. Land gently on the H.", 560, 18, WHITE);
        gameui_give_up_hint();
    } else {
        bool won = (landing == LANDED_WELL);
        const char *head = won ? "SMOOTH LANDING" : landing == LANDED_HARD ? "HARD LANDING"
                         : landing == LANDED_OFF_PAD ? "MISSED THE PAD" : landing == LANDED_SIDEWAYS ? "SKIDDED ON LANDING"
                         : landing == LANDED_LATE ? "OUT OF TIME" : "THE PILOT TOOK OVER";
        const char *body;
        if (practice) body = won ? "Textbook! (practice round)" : "Try another approach. (practice round)";
        else if (won) body = "A quick handover: their treatment will be shorter.";
        else if (landing == LANDED_GAVE_UP) body = "A normal landing: nothing changes.";
        else body = "A rough arrival: their treatment will take longer.";
        const char *extra = (landing == LANDED_GAVE_UP || landing == LANDED_LATE) ? NULL
                          : TextFormat("Touched down coming down at %.0f, sliding at %.0f (safe: under %.0f and %.0f)",
                                       fmaxf(touchVel.y, 0), fabsf(touchVel.x), SAFE_DOWN, SAFE_SIDE);
        gameui_result(won, head, body, extra, practice, resultDelay <= 0);
    }
    EndMode2D();
}
