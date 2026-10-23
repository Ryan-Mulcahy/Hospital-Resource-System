// gengame.c - "Start the generator".
//
// The screen is the generator's control panel, lit by the red emergency
// lights. On the left, the fuel pressure gauge: a dial with a green band.
// Each SPACE pumps the needle up a bit, and it slowly sinks back. On the
// right, the big START button (ENTER or click) and three lights for the
// tries. Cranking takes a moment; it catches if the pressure was in the green
// when you pressed START, won't catch if it was too low, and floods if it was
// too high (the needle drops right down again).
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "gameui.h"
#include "gengame.h"

#define GREEN_LOW   0.55f        // the green band on the gauge (0 to 1)
#define GREEN_HIGH  0.75f
#define FLOOD_AT    0.85f        // above this, cranking floods it
#define PUMP        0.08f        // each press of SPACE (five or six presses reach the green)
#define LEAK        0.10f        // the pressure sinks this much a second
#define CRANK_TIME  1.0f         // seconds the engine turns over
#define TRIES       3
#define TIME_LIMIT 15.0f

typedef enum { GAME_OFF, GAME_PUMPING, GAME_CRANKING, GAME_WON, GAME_LOST } GameState;
typedef enum { TRY_NONE, TRY_LOW, TRY_FLOODED } TryResult;

static GameState state = GAME_OFF;
static float pressure;           // 0 to 1
static float crankLeft;          // seconds of cranking left
static float crankPressure;      // the pressure when START was pressed
static int   triesLeft;
static TryResult lastTry;
static float messageLeft;
static float timeLeft;
static float resultDelay;
static bool  outOfTime;

void gengame_start(void)
{
    pressure = 0.2f;
    triesLeft = TRIES;
    lastTry = TRY_NONE;
    messageLeft = 0;
    timeLeft = TIME_LIMIT;
    outOfTime = false;
    state = GAME_PUMPING;
}

bool gengame_active(void) { return state != GAME_OFF; }

static void finish(Sim *sim, bool won)
{
    state = won ? GAME_WON : GAME_LOST;
    resultDelay = 0.6f;
    sound_play(won ? SND_GENERATOR : SND_BUZZ);
    sim_generator_result(sim, won);
}

static bool start_pressed(void)
{
    if (IsKeyPressed(KEY_ENTER)) return true;
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    Vector2 m = GetMousePosition();
    float dx = m.x - 700, dy = m.y - 300;
    return dx * dx + dy * dy < 80 * 80;               // the START button
}

void gengame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();
    if (messageLeft > 0) messageLeft -= dt;

    if (state == GAME_WON || state == GAME_LOST) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { finish(sim, false); return; }
    timeLeft -= dt;
    if (timeLeft <= 0 && state == GAME_PUMPING) { timeLeft = 0; outOfTime = true; finish(sim, false); return; }

    // The pressure sinks back all the time; SPACE pumps it up
    pressure -= LEAK * dt;
    if (pressure < 0) pressure = 0;

    if (state == GAME_CRANKING) {
        crankLeft -= dt;
        if (crankLeft > 0) return;
        if (crankPressure >= GREEN_LOW && crankPressure <= GREEN_HIGH) { finish(sim, true); return; }
        lastTry = (crankPressure > GREEN_HIGH) ? TRY_FLOODED : TRY_LOW;
        if (lastTry == TRY_FLOODED) pressure = 0.1f;                  // flooded: it has to drain
        triesLeft--;
        messageLeft = 1.8f;
        if (triesLeft <= 0) { finish(sim, false); return; }
        state = GAME_PUMPING;
        return;
    }
    if (IsKeyPressed(KEY_SPACE)) {
        pressure += PUMP;
        if (pressure > 1) pressure = 1;
    }
    if (start_pressed()) {
        crankPressure = pressure;
        crankLeft = CRANK_TIME;
        sound_play(SND_CHARGE);                                       // the starter motor whirring
        state = GAME_CRANKING;
    }
}

// A point on the gauge's dial for a pressure from 0 to 1 (left to right over the top)
static Vector2 dial(float v, float radius)
{
    float a = PI + v * PI;
    return (Vector2){ 300 + radius * cosf(a), 360 + radius * sinf(a) };
}

void gengame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    float flicker = 0.85f + 0.15f * sinf((float)GetTime() * 7.0f);   // the emergency lights
    Color room = { (unsigned char)(70 * flicker), 18, 20, 255 };
    ClearBackground(room);
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, room);

    // The panel
    DrawRectangleRounded((Rectangle){ 80, 120, 840, 390 }, 0.06f, 8, (Color){ 60, 64, 72, 255 });
    DrawRectangleRounded((Rectangle){ 96, 136, 808, 358 }, 0.06f, 8, (Color){ 84, 88, 96, 255 });
    DrawText("BACKUP GENERATOR", 120, 150, 22, (Color){ 240, 200, 60, 255 });

    // The fuel pressure gauge
    DrawCircleSector((Vector2){ 300, 360 }, 170, 180, 360, 48, (Color){ 230, 232, 236, 255 });
    DrawCircleSector((Vector2){ 300, 360 }, 160, 180 + GREEN_LOW * 180, 180 + GREEN_HIGH * 180, 24, (Color){ 90, 200, 120, 255 });
    DrawCircleSector((Vector2){ 300, 360 }, 160, 180 + FLOOD_AT * 180, 360, 24, (Color){ 230, 90, 80, 255 });
    DrawCircleSector((Vector2){ 300, 360 }, 120, 180, 360, 48, (Color){ 230, 232, 236, 255 });
    for (int k = 0; k <= 10; k++) DrawLineEx(dial(k / 10.0f, 150), dial(k / 10.0f, 170), 2, (Color){ 60, 60, 70, 255 });
    float shown = (state == GAME_CRANKING) ? crankPressure : pressure;
    DrawLineEx((Vector2){ 300, 360 }, dial(shown, 150), 5, (Color){ 200, 40, 40, 255 });
    DrawCircleV((Vector2){ 300, 360 }, 12, (Color){ 50, 50, 60, 255 });
    DrawText("FUEL PRESSURE", 300 - MeasureText("FUEL PRESSURE", 18) / 2, 380, 18, (Color){ 230, 232, 236, 255 });
    DrawText("tap SPACE to pump", 300 - MeasureText("tap SPACE to pump", 16) / 2, 406, 16, (Color){ 200, 205, 215, 255 });

    // The START button (it lights up while cranking) and the tries
    bool cranking = (state == GAME_CRANKING);
    float shake = cranking ? sinf((float)GetTime() * 60.0f) * 3.0f : 0;
    DrawCircleV((Vector2){ 700 + shake, 300 }, 86, (Color){ 40, 40, 46, 255 });
    DrawCircleV((Vector2){ 700 + shake, 300 }, 78, cranking ? (Color){ 255, 120, 90, 255 } : (Color){ 210, 50, 50, 255 });
    DrawText("START", (int)(700 + shake) - MeasureText("START", 30) / 2, 286, 30, WHITE);
    DrawText("ENTER or click", 700 - MeasureText("ENTER or click", 16) / 2, 396, 16, (Color){ 200, 205, 215, 255 });
    for (int k = 0; k < TRIES; k++)
        DrawCircleV((Vector2){ 660.0f + k * 40, 440 }, 10, k < triesLeft ? (Color){ 240, 200, 60, 255 } : (Color){ 60, 60, 66, 255 });

    // What happened on the last try
    if (cranking) gameui_centre_text("Cranking...", 92, 24, (Color){ 255, 220, 200, 255 });
    else if (messageLeft > 0 && lastTry != TRY_NONE)
        gameui_centre_text(lastTry == TRY_FLOODED ? "FLOODED! Too much fuel: let it drain and try again"
                                                  : "Won't catch: not enough fuel pressure", 92, 22, (Color){ 255, 170, 150, 255 });

    gameui_top_bar("POWER CUT: start the generator", "The generator didn't start by itself. Pump it into the green, then START.",
                   timeLeft, TIME_LIMIT, (Color){ 50, 14, 16, 255 });
    if (state == GAME_PUMPING || state == GAME_CRANKING) gameui_give_up_hint();

    if (state == GAME_WON || state == GAME_LOST) {
        bool won = (state == GAME_WON);
        const char *head = won ? "IT'S RUNNING!" : outOfTime ? "OUT OF TIME" : "IT WON'T START";
        const char *body = won ? "The lights are coming back on." : "The electricians are on their way: the power stays off a while.";
        gameui_result(won, head, body, NULL, false, resultDelay <= 0);
    }
    EndMode2D();
}
