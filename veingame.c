// veingame.c - "Find the vein", the blood test mini-game.
//
// First the patient's forearm fills the screen, with the vein only faintly
// showing under the skin. Hold the right mouse button to feel for it: the
// ring under your fingers pulses blue when you're over the vein, and the
// vein shows up more clearly nearby. Left-click to put the needle in there.
//
// Then a side view (a cut through the arm) shows the needle going in at an
// angle. Hold the left button to push it deeper. When the tip is inside the
// vein, blood shows in the needle (the "flashback"): let go then and you've
// got it. Push right through and the vein blows; click too far from the vein
// and the needle goes past it. Either way that's one of your three goes, and
// it leaves a little bruise.
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "veingame.h"

// ---- The arm (first view) ----
#define ARM_LEFT   80.0f
#define ARM_RIGHT 920.0f
#define ARM_TOP   200.0f
#define ARM_BOTTOM 430.0f

// ---- The side view ----
#define SKIN_Y      230.0f   // the top of the skin
#define ENTRY_X     220.0f   // where the needle goes in
#define NEEDLE_ANGLE (20.0f * DEG2RAD)
#define PUSH_SPEED  60.0f    // pixels per second the needle goes in while the button is held
#define NEEDLE_MAX 460.0f    // as far as the needle goes
#define FLASH_DELAY 0.05f    // seconds after the tip enters before blood shows

#define ATTEMPTS 3

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_ARM, GAME_NEEDLE, GAME_FAILED_GO, GAME_WON, GAME_LOST } GameState;

static GameState state = GAME_OFF;
static float waveA, waveF, waveP, waveB, waveG, waveQ;   // the shape of the vein along the arm
static float veinHalf;           // half the vein's width: thinner for emergencies
static float veinShow;           // how clearly it shows through the skin (alpha)
static float timeLeft, timeLimit;
static int   goesLeft;
static Vector2 bruises[ATTEMPTS];// where the missed goes were
static int   bruiseCount;
static float offset;             // how far from the vein's middle the needle went in (pixels; + is below)
static float depth;              // how far along its line the vein is (side view)
static float needle;             // how far the needle has gone in
static float insideFor;          // seconds the tip has been in the vein
static bool  entered;            // has the tip been in the vein on this go?
static bool  armed;              // let go of the click that chose the spot? (only then does holding push)
static const char *failText;     // why the last go failed
static float pauseLeft;          // the "missed" message stays up this long
static bool  practice;           // a practice round: changes nothing
static bool  outOfTime;
static float resultDelay;        // a moment before the result screen takes a click
static int   patientId;          // the real patient (0 in practice)
static char  patientText[96];    // "Patient 42 - Chest infection (J22)"

// ---- Offering the game ----
// Someone who has just sat down in bloods.
static int offerId = 0;
#define REMEMBER 8
static int offered[REMEMBER];    // recent patients already offered, so each one only gets offered once
static int offeredNext = 0;

static bool was_offered(int id)
{
    for (int i = 0; i < REMEMBER; i++) if (offered[i] == id) return true;
    return false;
}

static const Patient *in_bloods(const Sim *sim, int id)
{
    const SimRoom *room = &sim->rooms[ROOM_BLOODS];
    for (int s = 0; s < room->capacity; s++) {
        const Patient *p = room->in[s];
        if (p == NULL || p->walkLeft > 0.0f || p->roomLeft <= 0.0f) continue;
        if (id == 0 || p->id == id) return p;
    }
    return NULL;
}

void veingame_watch(const Sim *sim)
{
    if (offerId != 0 && in_bloods(sim, offerId) == NULL) offerId = 0;   // they've gone: stop offering
    if (offerId != 0 || state != GAME_OFF) return;
    const SimRoom *room = &sim->rooms[ROOM_BLOODS];
    for (int s = 0; s < room->capacity; s++) {
        const Patient *p = room->in[s];
        if (p == NULL || in_bloods(sim, p->id) == NULL || was_offered(p->id)) continue;
        offerId = p->id;                             // new: offer them
        offered[offeredNext] = p->id;
        offeredNext = (offeredNext + 1) % REMEMBER;
        return;
    }
}

int veingame_offer(void) { return offerId; }

// ---- Starting ----

// The middle of the vein at x on the arm
static float vein_y(float x)
{
    return (ARM_TOP + ARM_BOTTOM) / 2 + waveA * sinf(x * waveF + waveP) + waveB * sinf(x * waveG + waveQ);
}

static float rnd(float lo, float hi) { return lo + (hi - lo) * GetRandomValue(0, 1000) / 1000.0f; }

void veingame_start(const Sim *sim)
{
    const Patient *p = (offerId != 0) ? in_bloods(sim, offerId) : NULL;
    practice = (p == NULL);
    if (practice) {
        veinHalf = 9.0f;
        veinShow = 40.0f;
        timeLimit = 25.0f;
        patientId = 0;
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
    } else {
        // Emergencies are harder: a thinner vein that hardly shows, and less time
        bool emergency = (p->severity == 1);
        veinHalf = emergency ? 6.5f : 8.5f;
        veinShow = emergency ? 18.0f : 32.0f;
        timeLimit = emergency ? 20.0f : 25.0f;
        patientId = p->id;
        snprintf(patientText, sizeof patientText, "Patient %d  -  %s (%s)", p->id, p->condition->name, p->condition->icd10);
        offerId = 0;
    }
    // A new vein each time
    waveA = rnd(25, 55);  waveF = rnd(0.004f, 0.008f); waveP = rnd(0, 2 * PI);
    waveB = rnd(8, 18);   waveG = rnd(0.015f, 0.025f); waveQ = rnd(0, 2 * PI);
    timeLeft = timeLimit;
    goesLeft = ATTEMPTS;
    bruiseCount = 0;
    failText = NULL;
    outOfTime = false;
    state = GAME_ARM;
}

bool veingame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

static Vector2 needle_dir(void) { return (Vector2){ cosf(NEEDLE_ANGLE), sinf(NEEDLE_ANGLE) }; }

// The middle of the vein in the side view. Going in off the vein's middle
// moves it off the needle's line, so far enough off and the needle misses.
static Vector2 vein_centre(void)
{
    Vector2 d = needle_dir();
    Vector2 across = { -d.y, d.x };                  // at right angles to the needle
    return (Vector2){ ENTRY_X + d.x * depth + across.x * offset * 1.2f,
                      SKIN_Y + d.y * depth + across.y * offset * 1.2f };
}

static Vector2 needle_tip(void)
{
    Vector2 d = needle_dir();
    return (Vector2){ ENTRY_X + d.x * needle, SKIN_Y + d.y * needle };
}

static float vein_radius(void) { return veinHalf * 1.5f; }

static bool tip_in_vein(void)
{
    Vector2 t = needle_tip(), c = vein_centre();
    float dx = t.x - c.x, dy = t.y - c.y;
    return dx * dx + dy * dy < vein_radius() * vein_radius();
}

static void finish(Sim *sim, bool won)
{
    state = won ? GAME_WON : GAME_LOST;
    resultDelay = 0.4f;
    sound_play(won ? SND_SUCCESS : SND_BUZZ);
    if (!practice) sim_vein_result(sim, patientId, won);
}

static void failed_go(Sim *sim, const char *why)
{
    failText = why;
    goesLeft--;
    if (goesLeft <= 0) { finish(sim, false); return; }
    sound_play(SND_BUZZ);
    pauseLeft = 1.4f;
    state = GAME_FAILED_GO;
}

void veingame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();

    if (state == GAME_WON || state == GAME_LOST) {
        // Result screen: carry on, or (practice) try again
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (practice && IsKeyPressed(KEY_R)) { veingame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { failText = "You gave up."; finish(sim, false); return; }

    timeLeft -= dt;
    if (timeLeft <= 0) {
        timeLeft = 0;
        outOfTime = true;
        finish(sim, false);
        return;
    }

    Vector2 m = GetMousePosition();
    if (state == GAME_FAILED_GO) {                   // the "missed" message, then back to the arm
        pauseLeft -= dt;
        if (pauseLeft <= 0) state = GAME_ARM;
        return;
    }
    if (state == GAME_ARM) {
        // Left-click on the arm (not while feeling) to put the needle in there
        bool onArm = m.x > ARM_LEFT + 30 && m.x < ARM_RIGHT - 30 && m.y > ARM_TOP + 10 && m.y < ARM_BOTTOM - 10;
        if (onArm && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            offset = m.y - vein_y(m.x);
            if (bruiseCount < ATTEMPTS) bruises[bruiseCount] = m;    // kept as a bruise if this go misses
            depth = rnd(170, 250);                    // how deep the vein is varies
            needle = 60;                              // the tip just under the skin
            insideFor = 0;
            entered = false;
            armed = false;
            state = GAME_NEEDLE;
        }
        return;
    }

    // GAME_NEEDLE: hold the left button to push the needle in (once the
    // click that chose the spot has been let go)
    if (!armed) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) armed = true;
        return;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) needle += PUSH_SPEED * dt;
    bool inside = tip_in_vein();
    if (inside) { entered = true; insideFor += dt; }
    else insideFor = 0;

    if (entered && !inside) {                        // through the far side
        bruiseCount++;
        failed_go(sim, "Too far: the vein blew!");
        return;
    }
    if (needle >= NEEDLE_MAX) {                      // all the way in and never found it
        bruiseCount++;
        failed_go(sim, "Missed the vein: feel for it first (hold the right button).");
        return;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && inside && insideFor >= FLASH_DELAY) finish(sim, true);
}

// ---- Drawing ----

static void centre_text(const char *text, int y, int size, Color c)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, c);
}

static Color skin = { 232, 190, 160, 255 };

static void draw_arm(void)
{
    // The forearm, lying across the screen, with the hand off to the right
    DrawRectangleRounded((Rectangle){ ARM_LEFT, ARM_TOP, ARM_RIGHT - ARM_LEFT, ARM_BOTTOM - ARM_TOP }, 0.6f, 16, skin);
    DrawRectangleRounded((Rectangle){ ARM_LEFT + 20, ARM_TOP + 12, ARM_RIGHT - ARM_LEFT - 40, 40 }, 1.0f, 12,
                         (Color){ 242, 204, 176, 255 });                         // light along the top
    DrawEllipse((int)ARM_RIGHT + 10, (int)(ARM_TOP + ARM_BOTTOM) / 2, 60, 95, skin);   // the hand

    // The vein, faint unless you're feeling near it
    Vector2 m = GetMousePosition();
    bool feeling = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && state == GAME_ARM;
    Vector2 prev = { 0 };
    for (int i = 0; i <= 120; i++) {
        float x = ARM_LEFT + 40 + (ARM_RIGHT - ARM_LEFT - 80) * i / 120.0f;
        Vector2 pt = { x, vein_y(x) };
        float a = veinShow;
        if (feeling) {
            float dx = x - m.x;
            a += 150.0f * fmaxf(0.0f, 1.0f - fabsf(dx) / 70.0f);    // shows up near your fingers
        }
        // Mixed with the skin colour here rather than see-through, so the
        // overlapping pieces of line don't show as darker blotches
        float k = fminf(a, 255.0f) / 255.0f;
        Color c = { (unsigned char)(skin.r + (90 - skin.r) * k), (unsigned char)(skin.g + (110 - skin.g) * k),
                    (unsigned char)(skin.b + (190 - skin.b) * k), 255 };
        if (i > 0) DrawLineEx(prev, pt, veinHalf * 2, c);
        DrawCircleV(pt, veinHalf, c);                                  // round the joins
        prev = pt;
    }
    // The tourniquet, over the top of the arm
    DrawRectangle((int)ARM_LEFT + 260, (int)ARM_TOP - 26, 60, (int)(ARM_BOTTOM - ARM_TOP) + 52, (Color){ 60, 70, 160, 255 });
    DrawRectangle((int)ARM_LEFT + 260, (int)ARM_TOP - 26, 60, 10, (Color){ 80, 92, 190, 255 });

    // Bruises from the missed goes
    for (int i = 0; i < bruiseCount; i++) {
        DrawCircleV(bruises[i], 13, (Color){ 140, 80, 140, 110 });
        DrawCircleV(bruises[i], 5, (Color){ 120, 40, 60, 200 });
    }

    // Your fingers: a ring that pulses blue over the vein
    if (state == GAME_ARM) {
        float near = fabsf(m.y - vein_y(m.x));
        float s = feeling ? fmaxf(0.0f, 1.0f - near / 40.0f) : 0.0f;
        float pulse = 0.5f + 0.5f * sinf((float)GetTime() * (4.0f + 10.0f * s));
        Color ring = { (unsigned char)(255 - 180 * s), (unsigned char)(255 - 130 * s), 255,
                       (unsigned char)(feeling ? 120 + 120 * pulse * s + 40 : 90) };
        DrawRing(m, 26, 30 + 4 * pulse * s, 0, 360, 32, ring);
        if (!feeling) DrawLineEx((Vector2){ m.x - 10, m.y }, (Vector2){ m.x + 10, m.y }, 2, (Color){ 60, 60, 60, 160 });
        if (!feeling) DrawLineEx((Vector2){ m.x, m.y - 10 }, (Vector2){ m.x, m.y + 10 }, 2, (Color){ 60, 60, 60, 160 });
    }
}

static void draw_side_view(void)
{
    // Layers: skin, fat, muscle
    DrawRectangle(0, (int)SKIN_Y - 6, SCREEN_W, 30, skin);
    DrawRectangle(0, (int)SKIN_Y + 24, SCREEN_W, 120, (Color){ 245, 222, 160, 255 });
    DrawRectangle(0, (int)SKIN_Y + 144, SCREEN_W, SCREEN_H, (Color){ 190, 90, 90, 255 });
    for (int i = 0; i < 8; i++) DrawLine(0, (int)SKIN_Y + 160 + i * 26, SCREEN_W, (int)SKIN_Y + 170 + i * 26, (Color){ 170, 75, 78, 255 });
    DrawText("SKIN", 20, (int)SKIN_Y, 14, (Color){ 140, 100, 80, 255 });
    DrawText("FAT", 20, (int)SKIN_Y + 40, 14, (Color){ 170, 150, 90, 255 });
    DrawText("MUSCLE", 20, (int)SKIN_Y + 160, 14, (Color){ 120, 40, 45, 255 });

    // The vein: only a faint shadow from here
    Vector2 c = vein_centre();
    DrawCircleV(c, vein_radius() + 2, (Color){ 70, 80, 150, (unsigned char)(veinShow * 1.6f) });
    DrawCircleV(c, vein_radius(), (Color){ 110, 40, 60, (unsigned char)(veinShow * 1.6f) });

    // The needle and its hub, with blood in it once the tip is in the vein
    Vector2 d = needle_dir();
    Vector2 tip = needle_tip();
    Vector2 back = { ENTRY_X - d.x * 130, SKIN_Y - d.y * 130 };
    DrawLineEx(back, tip, 4, (Color){ 190, 196, 206, 255 });
    DrawLineEx(back, tip, 1.5f, (Color){ 240, 244, 250, 255 });
    Vector2 hubEnd = { back.x - d.x * 70, back.y - d.y * 70 };
    DrawLineEx(hubEnd, back, 22, (Color){ 210, 230, 240, 220 });              // the clear hub
    if (tip_in_vein() && insideFor >= FLASH_DELAY) {
        Vector2 bloodEnd = { back.x - d.x * 60, back.y - d.y * 60 };
        DrawLineEx(bloodEnd, back, 14, (Color){ 190, 20, 30, 255 });          // flashback!
        centre_text("FLASHBACK! Let go now", 140, 26, (Color){ 255, 120, 120, 255 });
    }
    DrawLineEx((Vector2){ hubEnd.x - d.x * 10, hubEnd.y - d.y * 10 }, hubEnd, 30, (Color){ 230, 80, 80, 255 });  // the red cap
}

void veingame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;

    ClearBackground((Color){ 30, 40, 52, 255 });
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 46, 60, 76, 255 });

    if (state == GAME_NEEDLE) draw_side_view();
    else                      draw_arm();

    // The top bar: what you're doing, who for, the clock and the goes left
    DrawRectangle(0, 0, SCREEN_W, 84, (Color){ 26, 34, 46, 255 });
    DrawText("FIND THE VEIN: take the blood sample", 24, 14, 26, WHITE);
    DrawText(patientText, 24, 50, 16, (Color){ 180, 195, 215, 255 });
    float frac = timeLeft / timeLimit;
    Color timeCol = frac > 0.5f ? (Color){ 90, 220, 140, 255 } : frac > 0.25f ? (Color){ 240, 190, 60, 255 } : (Color){ 240, 80, 80, 255 };
    DrawRectangleRounded((Rectangle){ 700, 54, 270, 12 }, 1.0f, 6, (Color){ 10, 14, 20, 255 });
    DrawRectangleRounded((Rectangle){ 700, 54, 270 * frac, 12 }, 1.0f, 6, timeCol);
    DrawText(TextFormat("%.0f s", ceilf(timeLeft)), 700, 30, 18, timeCol);
    for (int i = 0; i < ATTEMPTS; i++)               // goes left: three needles
        DrawRectangle(870 + i * 32, 24, 8, 22, i < goesLeft ? (Color){ 200, 210, 225, 255 } : (Color){ 70, 80, 95, 255 });

    Color hintCol = { 220, 228, 238, 255 };
    if (state == GAME_ARM) {
        centre_text("Hold the RIGHT button to feel for the vein, then LEFT-click to put the needle in", 470, 18, hintCol);
        centre_text(TextFormat("%d %s left", goesLeft, goesLeft == 1 ? "go" : "goes"), 500, 18, (Color){ 170, 185, 205, 255 });
    } else if (state == GAME_NEEDLE) {
        centre_text("Hold the LEFT button to push the needle in. Let go when blood shows in it.", 556, 18, hintCol);
    } else if (state == GAME_FAILED_GO) {
        centre_text(failText, 120, 24, (Color){ 255, 130, 120, 255 });
        centre_text(TextFormat("%d %s left", goesLeft, goesLeft == 1 ? "go" : "goes"), 500, 18, hintCol);
    }
    if (state == GAME_ARM || state == GAME_NEEDLE) DrawText("Q give up", SCREEN_W - 110, 578, 14, (Color){ 150, 165, 180, 255 });

    // The result
    if (state == GAME_WON || state == GAME_LOST) {
        bool won = (state == GAME_WON);
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, won ? (Color){ 20, 80, 50, 150 } : (Color){ 120, 10, 20, 150 });
        Rectangle box = { 250, 200, 500, 200 };
        DrawRectangleRounded(box, 0.15f, 10, won ? (Color){ 30, 120, 80, 245 } : (Color){ 150, 30, 40, 245 });
        const char *head = won ? "SAMPLE TAKEN" : (outOfTime ? "OUT OF TIME" : "NO SAMPLE");
        centre_text(head, 225, 32, WHITE);
        const char *body;
        if (practice) body = won ? "First time! (practice round)" : "Keep practising. (practice round)";
        else          body = won ? "They'll be through bloods quicker." : "They'll need longer in bloods.";
        centre_text(body, 280, 20, (Color){ 235, 245, 240, 255 });
        if (!won && !outOfTime && failText != NULL) centre_text(failText, 312, 16, (Color){ 255, 210, 200, 255 });
        const char *hint = practice ? "R try again    -    click or ENTER to carry on" : "Click or press ENTER to carry on";
        if (resultDelay <= 0) centre_text(hint, 352, 18, (Color){ 230, 240, 235, 255 });
    }
    EndMode2D();
}
