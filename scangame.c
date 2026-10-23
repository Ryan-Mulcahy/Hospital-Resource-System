// scangame.c - "Hold still", the X-ray and CT mini-game.
//
// The screen is the live picture from the scanner: bones in pale blue on
// black. The body part drifts as the patient breathes, and every so often
// they fidget and it jolts sideways before settling back. The mouse moves a
// square frame (the area the image will cover); an orange ring shows the
// target. Click or press SPACE to take the image. It's good if the frame is
// on the target and the patient was nearly still at that moment; otherwise
// the picture comes out smeared (or the target's outside it) and it's a retake.
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
#include "gameui.h"
#include "scangame.h"

// ---- What's being scanned ----
typedef enum { PART_HAND, PART_CHEST, PART_SKULL, PART_COUNT } Part;
static const char *PART_NAME[PART_COUNT] = { "hand", "chest", "skull" };
// The target on each, from the middle of the part
static const Vector2 PART_TARGET[PART_COUNT] = { { 12, -78 }, { -70, 40 }, { 48, -70 } };

#define CENTRE_X 500.0f          // the middle of the part when they're lying still
#define CENTRE_Y 330.0f
#define FRAME_HALF 60.0f         // half the size of the image frame
#define EXPOSURE 0.08f           // how long the image takes: movement in that time is the blur
#define MAX_BLUR 3.5f            // pixels of blur that's still a usable image
#define MAX_AIM  24.0f           // how far off the target the frame's middle can be

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_PLAYING, GAME_WON, GAME_LOST } GameState;

static GameState state = GAME_OFF;
static Part  part;
static int   room;               // ROOM_XRAY or ROOM_CT
static float clock_;             // seconds since the start (for the breathing)
static float timeLeft, timeLimit;
static Vector2 fidget, fidgetVel;// the jolt from fidgeting, and how fast it's moving
static float nextFidget;         // seconds until the next fidget
static float fidgetGap;          // ...on average: shorter for emergencies
static Vector2 offset;           // where the part is now, from its still position
static Vector2 velocity;         // how fast it's moving (pixels per second)
static Vector2 aim;              // where the frame was when the image was taken
static Vector2 shotOffset, shotVelocity;   // the part's position and movement then
static const char *why;          // why the image was no good (NULL if it was)
static bool  practice;
static bool  outOfTime;
static float resultDelay;
static int   patientId;
static char  patientText[96];

// ---- Offering the game ----
// Someone who has just got onto the table in X-ray or CT.
static int offerId = 0, offerRoom = ROOM_XRAY;
#define REMEMBER 8
static int offered[REMEMBER];    // recent patients already offered, so each one only gets offered once
static int offeredNext = 0;

static bool was_offered(int id)
{
    for (int i = 0; i < REMEMBER; i++) if (offered[i] == id) return true;
    return false;
}

// The patient with this id on the table in room r (or anyone there, for id 0)
static const Patient *on_table(const Sim *sim, int r, int id)
{
    const SimRoom *rm = &sim->rooms[r];
    for (int s = 0; s < rm->capacity; s++) {
        const Patient *p = rm->in[s];
        if (p == NULL || p->walkLeft > 0.0f || p->roomLeft <= 0.0f) continue;
        if (id == 0 || p->id == id) return p;
    }
    return NULL;
}

void scangame_watch(const Sim *sim)
{
    if (offerId != 0 && on_table(sim, offerRoom, offerId) == NULL) offerId = 0;   // they've gone
    if (offerId != 0 || state != GAME_OFF) return;
    const int rooms[2] = { ROOM_XRAY, ROOM_CT };
    for (int k = 0; k < 2; k++) {
        const SimRoom *rm = &sim->rooms[rooms[k]];
        for (int s = 0; s < rm->capacity; s++) {
            const Patient *p = rm->in[s];
            if (p == NULL || on_table(sim, rooms[k], p->id) == NULL || was_offered(p->id)) continue;
            offerId = p->id;
            offerRoom = rooms[k];
            offered[offeredNext] = p->id;
            offeredNext = (offeredNext + 1) % REMEMBER;
            return;
        }
    }
}

int scangame_offer(void) { return offerId; }

// ---- Starting ----

// Which part suits the condition
static Part part_for(const char *condition)
{
    if (strstr(condition, "wrist") || strstr(condition, "arm") || strstr(condition, "hand") ||
        strstr(condition, "Ankle") || strstr(condition, "knee")) return PART_HAND;
    if (strstr(condition, "Head") || strstr(condition, "Stroke") || strstr(condition, "headache")) return PART_SKULL;
    if (strstr(condition, "neumonia") || strstr(condition, "hest") || strstr(condition, "bleeding")) return PART_CHEST;
    return (Part)GetRandomValue(0, PART_COUNT - 1);
}

void scangame_start(const Sim *sim)
{
    const Patient *p = (offerId != 0) ? on_table(sim, offerRoom, offerId) : NULL;
    practice = (p == NULL);
    if (practice) {
        part = (Part)GetRandomValue(0, PART_COUNT - 1);
        room = GetRandomValue(0, 1) ? ROOM_XRAY : ROOM_CT;
        timeLimit = 10.0f;
        fidgetGap = 2.0f;
        patientId = 0;
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
    } else {
        part = part_for(p->condition->name);
        room = offerRoom;
        // Emergencies are harder: they fidget more, and there's less time
        timeLimit = (p->severity == 1) ? 8.0f : 10.0f;
        fidgetGap = (p->severity == 1) ? 1.3f : (p->severity == 2) ? 1.7f : 2.2f;
        patientId = p->id;
        snprintf(patientText, sizeof patientText, "Patient %d  -  %s (%s)", p->id, p->condition->name, p->condition->icd10);
        offerId = 0;
    }
    clock_ = 0;
    timeLeft = timeLimit;
    fidget = fidgetVel = (Vector2){ 0, 0 };
    nextFidget = fidgetGap * 0.6f;
    offset = velocity = (Vector2){ 0, 0 };
    why = NULL;
    outOfTime = false;
    state = GAME_PLAYING;
}

bool scangame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

static Vector2 target_now(Vector2 off)
{
    return (Vector2){ CENTRE_X + off.x + PART_TARGET[part].x, CENTRE_Y + off.y + PART_TARGET[part].y };
}

static void finish(Sim *sim, bool won)
{
    state = won ? GAME_WON : GAME_LOST;
    resultDelay = 0.4f;
    sound_play(won ? SND_SUCCESS : SND_BUZZ);
    if (!practice) sim_scan_result(sim, room, patientId, won);
}

static void take_image(Sim *sim)
{
    aim = GetMousePosition();
    shotOffset = offset;
    shotVelocity = velocity;
    float speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    Vector2 t = target_now(offset);
    float dx = aim.x - t.x, dy = aim.y - t.y;
    if (sqrtf(dx * dx + dy * dy) > MAX_AIM) why = "The frame wasn't on the target.";
    else if (speed * EXPOSURE > MAX_BLUR)  why = "Blurry: they moved while the image was taken.";
    else                                   why = NULL;
    sound_play(SND_BEEP);
    finish(sim, why == NULL);
}

void scangame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();

    if (state != GAME_PLAYING) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (practice && IsKeyPressed(KEY_R)) { scangame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { why = "You gave up."; finish(sim, false); return; }

    timeLeft -= dt;
    if (timeLeft <= 0) {
        timeLeft = 0;
        outOfTime = true;
        why = "They couldn't keep still any longer.";
        finish(sim, false);
        return;
    }
    clock_ += dt;

    // Fidgeting: a sudden jolt now and then, pulled back by a spring and slowed down
    nextFidget -= dt;
    if (nextFidget <= 0) {
        float a = GetRandomValue(0, 359) * DEG2RAD;
        float kick = (float)GetRandomValue(160, 280);
        fidgetVel.x += cosf(a) * kick;
        fidgetVel.y += sinf(a) * kick * 0.6f;
        nextFidget = fidgetGap * (0.5f + GetRandomValue(0, 100) / 100.0f);
    }
    fidgetVel.x += (-28.0f * fidget.x - 5.0f * fidgetVel.x) * dt;
    fidgetVel.y += (-28.0f * fidget.y - 5.0f * fidgetVel.y) * dt;
    fidget.x += fidgetVel.x * dt;
    fidget.y += fidgetVel.y * dt;

    // Breathing: a slow rise and fall, bigger for the chest
    float breathe = (part == PART_CHEST) ? 9.0f : 5.0f;
    Vector2 now = { fidget.x + 2.0f * sinf(clock_ * 0.9f), fidget.y + breathe * sinf(clock_ * 1.8f) };
    if (dt > 0) velocity = (Vector2){ (now.x - offset.x) / dt, (now.y - offset.y) / dt };
    offset = now;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_SPACE)) take_image(sim);
}

// ---- Drawing ----

// A bone: a thick line with rounded ends
static void bone(Vector2 a, Vector2 b, float w, Color c)
{
    DrawLineEx(a, b, w, c);
    DrawCircleV(a, w * 0.55f, c);
    DrawCircleV(b, w * 0.55f, c);
}

static Vector2 at(Vector2 c, float x, float y) { return (Vector2){ c.x + x, c.y + y }; }

static void draw_hand(Vector2 c, Color b)
{
    bone(at(c, -30, 150), at(c, -22, 60), 16, b);            // the two forearm bones
    bone(at(c, 26, 150), at(c, 22, 62), 18, b);
    for (int i = 0; i < 6; i++) DrawCircleV(at(c, -24 + (i % 3) * 22, 40 + (i / 3) * 16), 9, b);   // wrist bones
    // Four fingers: a long bone in the palm, then three small ones
    for (int f = 0; f < 4; f++) {
        float x0 = -30 + f * 20, x1 = -42 + f * 26;
        Vector2 base = at(c, x0, 26), knuckle = at(c, x1, -40);
        bone(base, knuckle, 9, b);
        float len = (f == 1 || f == 2) ? 34 : 28;
        Vector2 p1 = at(c, x1 + (f - 1.5f) * 4, -40 - len), p2 = at(c, x1 + (f - 1.5f) * 6, -40 - len * 1.7f),
                p3 = at(c, x1 + (f - 1.5f) * 7, -40 - len * 2.2f);
        bone(knuckle, p1, 8, b); bone(p1, p2, 7, b); bone(p2, p3, 6, b);
    }
    // The thumb, off to the side
    bone(at(c, -40, 34), at(c, -70, -2), 10, b);
    bone(at(c, -70, -2), at(c, -92, -30), 8, b);
    bone(at(c, -92, -30), at(c, -104, -52), 7, b);
}

static void draw_chest(Vector2 c, Color b)
{
    Color soft = { b.r, b.g, b.b, (unsigned char)(b.a * 0.25f) };
    DrawEllipse((int)c.x + 20, (int)c.y + 40, 60, 50, soft);  // the heart's shadow
    for (int i = 0; i < 12; i++)                                 // the spine
        DrawRectangleRounded((Rectangle){ c.x - 11, c.y - 150 + i * 26, 22, 20 }, 0.3f, 4, b);
    for (int i = 0; i < 9; i++) {                                // ribs, curving out and down on each side
        float y = c.y - 120 + i * 26;
        float w = 120 + 30 * sinf((i + 1) / 10.0f * PI);
        for (int side = -1; side <= 1; side += 2) {
            Vector2 prev = { c.x + side * 12, y };
            for (int k = 1; k <= 10; k++) {
                float u = k / 10.0f;
                Vector2 pt = { c.x + side * (12 + w * u), y - 18 * sinf(u * PI) + 30 * u * u };
                DrawLineEx(prev, pt, 6, b);
                prev = pt;
            }
        }
    }
    bone(at(c, -10, -150), at(c, -110, -140), 9, b);             // collarbones
    bone(at(c, 10, -150), at(c, 110, -140), 9, b);
}

static void draw_skull(Vector2 c, Color b)
{
    Vector2 prev = { 0 };
    for (int i = 0; i <= 48; i++) {                              // the outline of the head
        float a = i / 48.0f * 2 * PI;
        Vector2 pt = { c.x + 100 * cosf(a), c.y - 20 + 120 * sinf(a) };
        if (i > 0) DrawLineEx(prev, pt, 10, b);
        prev = pt;
    }
    Color dark = { 4, 10, 18, 255 };
    DrawEllipse((int)c.x - 38, (int)c.y - 10, 26, 22, b);        // eye sockets: a rim, dark inside
    DrawEllipse((int)c.x + 38, (int)c.y - 10, 26, 22, b);
    DrawEllipse((int)c.x - 38, (int)c.y - 10, 19, 16, dark);
    DrawEllipse((int)c.x + 38, (int)c.y - 10, 19, 16, dark);
    DrawTriangle(at(c, 0, 14), at(c, -10, 40), at(c, 10, 40), b);   // the nose
    for (int t = 0; t < 8; t++)                                  // teeth
        DrawRectangleRounded((Rectangle){ c.x - 38 + t * 10, c.y + 62, 8, 16 }, 0.4f, 4, b);
}

static void draw_part(Vector2 off, Color b)
{
    Vector2 c = { CENTRE_X + off.x, CENTRE_Y + off.y };
    if (part == PART_HAND)       draw_hand(c, b);
    else if (part == PART_CHEST) draw_chest(c, b);
    else                         draw_skull(c, b);
    // The problem being looked for: a hairline crack (or a shadow on the chest)
    Vector2 t = target_now(off);
    if (part == PART_CHEST) DrawCircleV(t, 16, (Color){ b.r, b.g, b.b, (unsigned char)(b.a * 0.35f) });
    else {
        Color crack = { 4, 10, 18, b.a };
        DrawLineEx(at(t, -6, -5), at(t, 1, 1), 2, crack);
        DrawLineEx(at(t, 1, 1), at(t, 7, 4), 2, crack);
    }
}

static void draw_aim_frame(Vector2 m, Color c)
{
    Rectangle r = { m.x - FRAME_HALF, m.y - FRAME_HALF, FRAME_HALF * 2, FRAME_HALF * 2 };
    DrawRectangleLinesEx(r, 2, c);
    DrawLineEx(at(m, -14, 0), at(m, 14, 0), 2, c);
    DrawLineEx(at(m, 0, -14), at(m, 0, 14), 2, c);
}

void scangame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    Color boneCol = { 190, 220, 245, 255 };

    ClearBackground((Color){ 4, 10, 18, 255 });
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 4, 10, 18, 255 });

    if (state == GAME_PLAYING) {
        draw_part(offset, boneCol);
        Vector2 t = target_now(offset);
        DrawRing(t, 20, 24, 0, 360, 32, (Color){ 255, 160, 40, 200 });     // the target
        draw_aim_frame(GetMousePosition(), (Color){ 120, 255, 160, 230 });

        // How much they're moving: the image is only sharp in the green
        float speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
        float blur = speed * EXPOSURE;
        float k = fminf(blur / (MAX_BLUR * 3), 1.0f);
        Rectangle meter = { 940, 140, 24, 360 };
        DrawRectangleRec(meter, (Color){ 20, 30, 40, 255 });
        float limit = meter.y + meter.height * (1 - MAX_BLUR / (MAX_BLUR * 3));
        DrawRectangle((int)meter.x, (int)limit, (int)meter.width, (int)(meter.y + meter.height - limit), (Color){ 30, 90, 50, 255 });
        Color fill = (blur <= MAX_BLUR) ? (Color){ 90, 230, 130, 255 } : (Color){ 240, 90, 80, 255 };
        DrawRectangle((int)meter.x + 4, (int)(meter.y + meter.height * (1 - k)), (int)meter.width - 8,
                      (int)(meter.height * k), fill);
        DrawText("MOVING", 918, 512, 14, (Color){ 160, 180, 200, 255 });
        DrawText(blur <= MAX_BLUR ? "STILL" : "", 924, 116, 16, fill);

        gameui_centre_text(TextFormat("Put the frame on the orange ring and click (or SPACE) when the %s is still",
                                      PART_NAME[part]), 552, 18, (Color){ 210, 225, 240, 255 });
        gameui_give_up_hint();
    } else {
        // The image that was taken: smeared along the way they were moving,
        // and only what's inside the frame shows clearly
        float blur = fminf(sqrtf(shotVelocity.x * shotVelocity.x + shotVelocity.y * shotVelocity.y) * EXPOSURE, 40.0f);
        int copies = (blur > 1.0f) ? 6 : 1;
        for (int i = 0; i < copies; i++) {
            float u = (copies == 1) ? 0 : (i / (float)(copies - 1) - 0.5f);
            Vector2 o = shotOffset;
            if (blur > 0.01f) {
                float sp = sqrtf(shotVelocity.x * shotVelocity.x + shotVelocity.y * shotVelocity.y);
                o.x += shotVelocity.x / sp * blur * u;
                o.y += shotVelocity.y / sp * blur * u;
            }
            Color c = boneCol;
            c.a = (unsigned char)(copies == 1 ? 255 : 90);
            draw_part(o, c);
        }
        // Darken everything outside the frame
        Rectangle f = { aim.x - FRAME_HALF, aim.y - FRAME_HALF, FRAME_HALF * 2, FRAME_HALF * 2 };
        Color shade = { 4, 10, 18, 200 };
        DrawRectangle(0, 84, SCREEN_W, (int)f.y - 84, shade);
        DrawRectangle(0, (int)(f.y + f.height), SCREEN_W, SCREEN_H, shade);
        DrawRectangle(0, (int)f.y, (int)f.x, (int)f.height, shade);
        DrawRectangle((int)(f.x + f.width), (int)f.y, SCREEN_W, (int)f.height, shade);
        DrawRectangleLinesEx(f, 2, (Color){ 255, 255, 255, 120 });
    }

    const char *title = (room == ROOM_CT) ? "HOLD STILL: CT scan" : "HOLD STILL: X-ray";
    gameui_top_bar(TextFormat("%s of the %s", title, PART_NAME[part]), patientText, timeLeft, timeLimit,
                   (Color){ 16, 26, 38, 255 });

    if (state == GAME_WON || state == GAME_LOST) {
        bool won = (state == GAME_WON);
        const char *head = won ? "SHARP IMAGE" : (outOfTime ? "OUT OF TIME" : "RETAKE NEEDED");
        const char *body;
        if (practice) body = won ? "Perfect shot! (practice round)" : "Not this time. (practice round)";
        else          body = won ? "Done first time: they'll be out sooner." : "A retake: they'll be in the room longer.";
        // Lower down, so the image stays in view
        DrawRectangle(0, 446, SCREEN_W, SCREEN_H - 446, won ? (Color){ 20, 80, 50, 220 } : (Color){ 110, 20, 30, 220 });
        gameui_centre_text(head, 456, 30, WHITE);
        gameui_centre_text(body, 494, 20, (Color){ 235, 245, 240, 255 });
        if (why != NULL) gameui_centre_text(why, 524, 16, (Color){ 255, 225, 205, 255 });
        const char *hint = practice ? "R try again    -    click or ENTER to carry on" : "Click or press ENTER to carry on";
        if (resultDelay <= 0) gameui_centre_text(hint, 560, 18, (Color){ 230, 240, 235, 255 });
    }
    EndMode2D();
}
