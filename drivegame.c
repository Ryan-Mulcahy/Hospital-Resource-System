// drivegame.c - "Blue lights", the ambulance drive.
//
// Seen from above: the road runs up the screen with three lanes, and the
// ambulance stays near the bottom while the road scrolls past. Cars go the
// same way, slower. The ambulance speeds up on its own; LEFT/RIGHT steer and
// DOWN brakes. Touching a car slows the ambulance almost to a stop (and the
// car pulls over). A bar on the right shows how far there is to go, and the
// target time to beat.
//
// Distances are in pixels of road. Everything is drawn in the same 1000x600
// layout as the other 2D screens (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "gameui.h"
#include "drivegame.h"

#define ROAD_X      350.0f       // the road's left edge
#define LANE_W      100.0f
#define LANES       3
#define CAR_W       46.0f
#define CAR_H       80.0f
#define AMB_Y       440.0f       // where the ambulance is on screen (its top)
#define TOP_SPEED   620.0f       // pixels a second
#define ACCEL       260.0f
#define BRAKE       900.0f
#define STEER       330.0f       // sideways pixels a second
#define DISTANCE    16000.0f     // to the hospital
#define TARGET_TIME 34.0f        // there within this: fast
#define TIME_LIMIT  60.0f        // the clock on screen (after this they're brought in anyway, slowly)

#define MAX_CARS 24

typedef struct {
    bool  used;
    float x;                     // its left edge on screen
    float road;                  // how far along the road its front is
    float speed;
    Color colour;
    float pullOver;              // > 0: hit, and moving off to the side
} Car;

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_PLAYING, GAME_DONE } GameState;

static GameState state = GAME_OFF;
static Car   cars[MAX_CARS];
static float travelled;          // how far the ambulance has gone
static float speed;
static float ambX;               // the ambulance's left edge on screen
static float nextRow;            // road distance of the next row of traffic
static float taken;              // seconds so far
static int   clips;
static float bump;               // the screen shakes briefly after a clip
static bool  fast, gaveUp;
static int   patientId;
static float resultDelay;

static const Color CAR_COLOURS[] = {
    { 60, 90, 160, 255 }, { 180, 50, 50, 255 }, { 70, 70, 80, 255 }, { 220, 200, 70, 255 },
    { 50, 130, 90, 255 }, { 200, 200, 205, 255 }, { 120, 70, 140, 255 },
};

static float lane_x(int lane) { return ROAD_X + lane * LANE_W + (LANE_W - CAR_W) / 2; }

// A row of traffic ahead: one or two cars, never all three lanes, so there's always a way through
static void add_row(float at)
{
    int first = GetRandomValue(0, LANES - 1);
    int count = GetRandomValue(1, 10) <= 7 ? 1 : 2;
    for (int n = 0; n < count; n++) {
        int lane = (first + n) % LANES;
        for (int i = 0; i < MAX_CARS; i++) {
            if (cars[i].used) continue;
            cars[i] = (Car){ true, lane_x(lane), at + GetRandomValue(0, 60), (float)GetRandomValue(150, 300),
                             CAR_COLOURS[GetRandomValue(0, (int)(sizeof CAR_COLOURS / sizeof CAR_COLOURS[0]) - 1)], 0 };
            break;
        }
    }
}

void drivegame_start(void)
{
    for (int i = 0; i < MAX_CARS; i++) cars[i].used = false;
    travelled = 0;
    speed = 200;
    ambX = lane_x(1);
    nextRow = 500;
    taken = 0;
    clips = 0;
    bump = 0;
    gaveUp = false;
    sound_play(SND_SIREN);
    state = GAME_PLAYING;
}

bool drivegame_active(void) { return state != GAME_OFF; }

// Where something at this road distance is on screen (its top)
static float screen_y(float road) { return AMB_Y - (road - travelled); }

static void finish(Sim *sim)
{
    fast = !gaveUp && taken <= TARGET_TIME;
    patientId = sim_ambulance_arrival(sim, fast);
    sound_play(fast ? SND_SUCCESS : SND_BUZZ);
    resultDelay = 0.4f;
    state = GAME_DONE;
}

void drivegame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();
    if (bump > 0) bump -= dt;

    if (state == GAME_DONE) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { gaveUp = true; finish(sim); return; }

    taken += dt;
    if (taken >= TIME_LIMIT) { finish(sim); return; }

    // Driving: it speeds up by itself; DOWN brakes; LEFT/RIGHT steer
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) speed -= BRAKE * dt;
    else speed += ACCEL * dt;
    if (speed > TOP_SPEED) speed = TOP_SPEED;
    if (speed < 60) speed = 60;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  ambX -= STEER * dt;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) ambX += STEER * dt;
    if (ambX < ROAD_X + 4) ambX = ROAD_X + 4;
    if (ambX > ROAD_X + LANES * LANE_W - CAR_W - 4) ambX = ROAD_X + LANES * LANE_W - CAR_W - 4;
    travelled += speed * dt;

    // Traffic ahead, until near the hospital
    while (nextRow < travelled + 700 && nextRow < DISTANCE - 600) {
        add_row(nextRow);
        nextRow += GetRandomValue(260, 420);
    }
    for (int i = 0; i < MAX_CARS; i++) {
        Car *c = &cars[i];
        if (!c->used) continue;
        for (int j = 0; j < MAX_CARS; j++) {         // don't drive into the car in front
            const Car *f = &cars[j];
            if (j == i || !f->used || fabsf(f->x - c->x) > 10) continue;
            float gap = f->road - c->road;
            if (gap > 0 && gap < CAR_H + 40 && c->speed > f->speed) c->speed = f->speed;
        }
        c->road += c->speed * dt;
        if (c->pullOver > 0) {                       // drifting to the nearest side and stopping
            c->pullOver -= dt;
            c->speed *= 0.97f;
            c->x += (c->x < ROAD_X + LANES * LANE_W / 2 ? -1 : 1) * 120 * dt;
        }
        if (screen_y(c->road) > SCREEN_H + 50) { c->used = false; continue; }   // gone behind
        // Touching the ambulance? (a little smaller than the drawings, to be fair)
        if (c->pullOver <= 0 && fabsf(c->x - ambX) < CAR_W - 8) {
            float cy = screen_y(c->road);
            if (cy < AMB_Y + CAR_H - 10 && cy + CAR_H - 10 > AMB_Y) {
                clips++;
                speed = 70;
                bump = 0.3f;
                c->pullOver = 1.2f;
                sound_play(SND_BUZZ);
            }
        }
    }
    if (travelled >= DISTANCE) finish(sim);
}

// ---- Drawing ----

static void draw_vehicle(float x, float y, Color body, bool ambulance)
{
    DrawRectangleRounded((Rectangle){ x + 3, y + 5, CAR_W, CAR_H }, 0.3f, 6, (Color){ 0, 0, 0, 60 });   // shadow
    DrawRectangleRounded((Rectangle){ x, y, CAR_W, CAR_H }, 0.3f, 6, body);
    Color glass = { 40, 50, 70, 255 };
    DrawRectangleRounded((Rectangle){ x + 6, y + 12, CAR_W - 12, 14 }, 0.4f, 4, glass);    // windscreen (front is up)
    if (!ambulance) {
        DrawRectangleRounded((Rectangle){ x + 7, y + CAR_H - 20, CAR_W - 14, 10 }, 0.4f, 4, glass);
        return;
    }
    DrawRectangle((int)x, (int)y + 40, (int)CAR_W, 8, (Color){ 210, 40, 40, 255 });         // the red stripe
    DrawRectangle((int)x + (int)CAR_W / 2 - 3, (int)y + 54, 6, 18, (Color){ 210, 40, 40, 255 });   // the cross on the roof
    DrawRectangle((int)x + (int)CAR_W / 2 - 9, (int)y + 60, 18, 6, (Color){ 210, 40, 40, 255 });
    bool flip = fmodf((float)GetTime(), 0.3f) < 0.15f;                                        // the blue lights
    DrawRectangle((int)x + 4, (int)y + 30, 16, 6, flip ? (Color){ 60, 140, 255, 255 } : (Color){ 30, 50, 110, 255 });
    DrawRectangle((int)(x + CAR_W) - 20, (int)y + 30, 16, 6, flip ? (Color){ 30, 50, 110, 255 } : (Color){ 60, 140, 255, 255 });
    DrawCircleV((Vector2){ x + CAR_W / 2, y + 33 }, 40, (Color){ 60, 140, 255, (unsigned char)(flip ? 30 : 15) });
}

void drivegame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    Camera2D cam = fit;                              // a jolt when you clip a car
    if (bump > 0) cam.offset.x += sinf((float)GetTime() * 80.0f) * 8.0f * bump / 0.3f * fit.zoom;
    ClearBackground((Color){ 70, 120, 60, 255 });
    BeginMode2D(cam);

    // Grass, the pavements and the road, with lane lines moving down
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 74, 128, 64, 255 });
    DrawRectangle((int)ROAD_X - 24, 0, (int)(LANES * LANE_W) + 48, SCREEN_H, (Color){ 170, 170, 165, 255 });
    DrawRectangle((int)ROAD_X, 0, (int)(LANES * LANE_W), SCREEN_H, (Color){ 60, 62, 68, 255 });
    float dashOffset = fmodf(travelled, 80.0f);
    for (int l = 1; l < LANES; l++)
        for (float y = -80 + dashOffset; y < SCREEN_H; y += 80)
            DrawRectangle((int)(ROAD_X + l * LANE_W) - 2, (int)y, 4, 40, (Color){ 230, 230, 230, 255 });
    // Trees along the sides
    for (int side = 0; side < 2; side++)
        for (float y = -120 + fmodf(travelled, 120.0f); y < SCREEN_H; y += 120) {
            float x = side == 0 ? ROAD_X - 90 : ROAD_X + LANES * LANE_W + 90;
            DrawCircleV((Vector2){ x, y }, 30, (Color){ 46, 96, 50, 255 });
            DrawCircleV((Vector2){ x - 8, y - 8 }, 18, (Color){ 64, 120, 62, 255 });
        }
    // The hospital comes into view at the end
    float hy = screen_y(DISTANCE) - 160;
    if (hy > -200) {
        DrawRectangle((int)ROAD_X - 60, (int)hy, (int)(LANES * LANE_W) + 120, 160, (Color){ 225, 228, 235, 255 });
        DrawRectangle((int)ROAD_X - 60, (int)hy + 120, (int)(LANES * LANE_W) + 120, 40, (Color){ 200, 60, 60, 255 });
        DrawText("A&E", (int)(ROAD_X + LANES * LANE_W / 2) - MeasureText("A&E", 40) / 2, (int)hy + 40, 40, (Color){ 200, 60, 60, 255 });
    }

    for (int i = 0; i < MAX_CARS; i++)
        if (cars[i].used) draw_vehicle(cars[i].x, screen_y(cars[i].road), cars[i].colour, false);
    draw_vehicle(ambX, AMB_Y, (Color){ 245, 245, 240, 255 }, true);

    // How far, and how fast
    Rectangle bar = { 920, 120, 20, 380 };
    DrawRectangleRec(bar, (Color){ 30, 50, 30, 200 });
    float done = fminf(travelled / DISTANCE, 1.0f);
    DrawRectangle((int)bar.x, (int)(bar.y + bar.height * (1 - done)), (int)bar.width, (int)(bar.height * done), (Color){ 90, 160, 255, 255 });
    DrawText("A&E", 908, 96, 18, WHITE);
    DrawText(TextFormat("%d km/h", (int)(speed / TOP_SPEED * 110)), 40, 120, 24, WHITE);
    DrawText(TextFormat("Clipped: %d", clips), 40, 156, 20, clips ? (Color){ 255, 170, 160, 255 } : (Color){ 220, 235, 220, 255 });
    Color tc = taken <= TARGET_TIME ? (Color){ 220, 255, 220, 255 } : (Color){ 255, 200, 120, 255 };
    DrawText(TextFormat("Target: under %.0f s", TARGET_TIME), 40, 190, 18, tc);

    // The top bar: the clock counts up here (there's a target to beat)
    gameui_top_bar("BLUE LIGHTS: get the emergency to A&E", TextFormat("%.1f s on the road", taken),
                   TIME_LIMIT - taken, TIME_LIMIT, (Color){ 30, 40, 80, 255 });

    if (state == GAME_PLAYING) {
        gameui_centre_text("LEFT / RIGHT to steer, DOWN to brake. Don't clip the cars!", 566, 18, WHITE);
        gameui_give_up_hint();
    } else {
        const char *head = gaveUp ? "HANDED OVER TO ANOTHER CREW" : fast ? "QUICK RUN!" : "THEY'RE IN, BUT IT WAS SLOW";
        const char *body = fast ? "A head start: their treatment will be shorter." : "Their treatment will take longer.";
        const char *extra = TextFormat("Patient %d arrived in %.1f s, %d %s clipped", patientId, taken, clips, clips == 1 ? "car" : "cars");
        gameui_result(fast, head, body, extra, false, resultDelay <= 0);
    }
    EndMode2D();
}
