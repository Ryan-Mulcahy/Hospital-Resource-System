// spingame.c - "Balance the centrifuge", the bloods mini-game.
//
// The centrifuge is seen from above: a round rotor with 12 slots round its
// edge. The tubes waiting to go in stand in a rack on the left. Click an
// empty slot to put a tube in it, or a full one to take it out again. Once
// every tube is in, press SPACE (or click Spin) and the rotor spins up.
//
// Balanced means the tubes' pull on the middle cancels out: add up an arrow
// from the middle to each tube, and the total has to come to (almost)
// nothing. Two tubes straight across from each other cancel, and so do three
// evenly spaced (every 4th slot), so any number of tubes from 2 to 10 can be
// balanced by mixing pairs and threes. If it isn't balanced the rotor
// wobbles, the lid alarm goes and it stops; an arrow shows the heavy side.
//
// Every puzzle is made by building a balanced answer first, so there's
// always at least one. Cracked slots are only ever ones that answer doesn't use.
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "gameui.h"
#include "spingame.h"

#define SLOTS      12
#define ROTOR_X    560.0f        // the middle of the rotor on screen
#define ROTOR_Y    330.0f
#define ROTOR_R    190.0f        // the rotor's edge
#define SLOT_RING  145.0f        // how far out the slots are
#define SLOT_R     22.0f         // a slot's size
#define SPIN_TIME  2.4f          // seconds the spin (or the wobble before it stops) lasts
#define SPINS      2             // goes at spinning
#define BALANCED   0.05f         // how small the total pull has to be (one tube's pull is 1)

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_LOADING, GAME_SPINNING, GAME_WON, GAME_LOST } GameState;

static GameState state = GAME_OFF;
static int   tubes;                  // how many tubes to load
static bool  filled[SLOTS];          // which slots have a tube in
static bool  cracked[SLOTS];         // slots that can't be used
static Color cap[SLOTS];             // the colour of the cap on the tube in each slot
static int   patientSlot;            // where this patient's own tube went (-1: still in the rack)
static float spinTime;               // seconds into the spin
static float angle;                  // how far the rotor has turned (for drawing)
static Vector2 pull;                 // the total pull when it was spun (the heavy side)
static bool  wobbling;               // this spin is unbalanced
static int   spinsLeft;
static float timeLeft, timeLimit;
static float resultDelay;
static float messageLeft;            // "Unbalanced!" after a failed spin
static bool  practice, outOfTime, gaveUp;
static int   patientId;
static char  patientText[96];

static const Color CAPS[] = {        // other patients' tubes have these caps
    { 250, 200, 60, 255 }, { 120, 60, 160, 255 }, { 60, 140, 220, 255 }, { 130, 130, 140, 255 }, { 60, 170, 90, 255 },
};
#define PATIENT_CAP (Color){ 220, 50, 60, 255 }   // this patient's tube: red
static Color other_cap(int k) { return CAPS[k % (int)(sizeof CAPS / sizeof CAPS[0])]; }

// ---- Offering the game ----
// Someone who has just sat down in bloods.
static int offerId = 0;
#define REMEMBER 8
static int offered[REMEMBER];        // recent patients already offered, so each one only gets offered once
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

void spingame_watch(const Sim *sim)
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

int spingame_offer(void) { return offerId; }

// ---- The balance ----

// The pull of slot s on the middle: an arrow of length 1 pointing at it
static Vector2 slot_pull(int s)
{
    float a = s * (2 * PI / SLOTS) - PI / 2;         // slot 0 at the top, going clockwise
    return (Vector2){ cosf(a), sinf(a) };
}

static Vector2 total_pull(void)
{
    Vector2 t = { 0, 0 };
    for (int s = 0; s < SLOTS; s++) if (filled[s]) { Vector2 p = slot_pull(s); t.x += p.x; t.y += p.y; }
    return t;
}

static int loaded(void)
{
    int n = 0;
    for (int s = 0; s < SLOTS; s++) if (filled[s]) n++;
    return n;
}

// Build a balanced answer for n tubes out of threes (every 4th slot) and
// pairs (straight across), mark it in `used`, and return true if it fitted.
static bool make_answer(int n, bool *used)
{
    for (int s = 0; s < SLOTS; s++) used[s] = false;
    int threes = (n % 2 == 1) ? 1 : 0;               // an odd number needs a three
    if (n >= 9 && GetRandomValue(0, 1)) threes += 2; // (sometimes two more threes for big loads)
    int pairs = (n - 3 * threes) / 2;
    if (pairs < 0) return false;
    for (int k = 0; k < threes; k++) {
        int start = GetRandomValue(0, 3), tries = 0;
        while ((used[start] || used[start + 4] || used[(start + 8) % SLOTS]) && tries++ < 4) start = (start + 1) % 4;
        if (used[start] || used[start + 4] || used[(start + 8) % SLOTS]) return false;
        used[start] = used[start + 4] = used[(start + 8) % SLOTS] = true;
    }
    for (int k = 0; k < pairs; k++) {
        int start = GetRandomValue(0, 5), tries = 0;
        while ((used[start] || used[start + 6]) && tries++ < 6) start = (start + 1) % 6;
        if (used[start] || used[start + 6]) return false;
        used[start] = used[start + 6] = true;
    }
    return true;
}

// ---- Starting ----
void spingame_start(const Sim *sim)
{
    const Patient *p = (offerId != 0) ? in_bloods(sim, offerId) : NULL;
    practice = (p == NULL);
    int severity = practice ? 3 : p->severity;
    if (practice) {
        patientId = 0;
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
    } else {
        patientId = p->id;
        snprintf(patientText, sizeof patientText, "Patient %d  -  %s (%s)", p->id, p->condition->name, p->condition->icd10);
        offerId = 0;
    }
    // How many tubes, and how many cracked slots: odd numbers (which need a
    // three) and cracked slots make it harder, so emergencies get more of them
    static const int ROUTINE[] = { 2, 3, 4, 6 }, URGENT[] = { 3, 5, 6, 7 }, EMERGENCY[] = { 5, 7, 8, 9 };
    const int *choices = (severity == 1) ? EMERGENCY : (severity == 2) ? URGENT : ROUTINE;
    int crackedCount = (severity == 1) ? 3 : (severity == 2) ? 2 : GetRandomValue(0, 1);
    bool answer[SLOTS];
    do { tubes = choices[GetRandomValue(0, 3)]; } while (!make_answer(tubes, answer));
    for (int s = 0; s < SLOTS; s++) { filled[s] = false; cracked[s] = false; }
    for (int k = 0; k < crackedCount; k++) {         // crack slots the answer doesn't need
        int s, tries = 0;
        do { s = GetRandomValue(0, SLOTS - 1); } while ((answer[s] || cracked[s]) && tries++ < 50);
        if (!answer[s]) cracked[s] = true;
    }
    patientSlot = -1;
    spinsLeft = SPINS;
    timeLimit = (severity == 1) ? 25.0f : 30.0f;
    timeLeft = timeLimit;
    angle = 0;
    messageLeft = 0;
    outOfTime = gaveUp = false;
    state = GAME_LOADING;
}

bool spingame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

static Vector2 slot_centre(int s, float turn)
{
    float a = s * (2 * PI / SLOTS) - PI / 2 + turn;
    return (Vector2){ ROTOR_X + SLOT_RING * cosf(a), ROTOR_Y + SLOT_RING * sinf(a) };
}

static Rectangle spin_button(void) { return (Rectangle){ 60, 470, 200, 46 }; }

static void finish(Sim *sim, bool won)
{
    state = won ? GAME_WON : GAME_LOST;
    resultDelay = 0.4f;
    sound_play(won ? SND_SUCCESS : SND_BUZZ);
    if (!practice && !gaveUp) sim_spin_result(sim, patientId, won);
}

static void start_spin(void)
{
    pull = total_pull();
    wobbling = sqrtf(pull.x * pull.x + pull.y * pull.y) > BALANCED;
    spinTime = 0;
    spinsLeft--;
    sound_play(SND_CHARGE);                          // the motor winding up
    state = GAME_SPINNING;
}

void spingame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();
    if (messageLeft > 0) messageLeft -= dt;

    if (state == GAME_WON || state == GAME_LOST) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (practice && IsKeyPressed(KEY_R)) { spingame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { gaveUp = true; finish(sim, false); return; }

    timeLeft -= dt;
    if (timeLeft <= 0 && state == GAME_LOADING) {
        timeLeft = 0;
        outOfTime = true;
        finish(sim, false);
        return;
    }

    if (state == GAME_SPINNING) {
        spinTime += dt;
        float speed = wobbling ? 6.0f * (1.0f - spinTime / SPIN_TIME) + 2.0f : 4.0f + 14.0f * spinTime;   // turns per second, roughly
        angle += speed * dt;
        if (wobbling && fmodf(spinTime, 0.4f) < dt) sound_play(SND_BEEP);   // the imbalance alarm
        if (spinTime >= SPIN_TIME) {
            if (!wobbling) { finish(sim, true); return; }
            angle = 0;
            if (spinsLeft <= 0) { finish(sim, false); return; }
            messageLeft = 2.5f;                      // "Unbalanced!" and back to loading
            state = GAME_LOADING;
        }
        return;
    }

    // Loading: click a slot to put a tube in or take it out
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        for (int s = 0; s < SLOTS; s++) {
            Vector2 c = slot_centre(s, 0);
            float dx = m.x - c.x, dy = m.y - c.y;
            if (dx * dx + dy * dy > (SLOT_R + 6) * (SLOT_R + 6) || cracked[s]) continue;
            if (filled[s]) {                         // take it out
                filled[s] = false;
                if (patientSlot == s) patientSlot = -1;
            } else if (loaded() < tubes) {           // put the next tube from the rack in
                filled[s] = true;
                if (patientSlot < 0) { patientSlot = s; cap[s] = PATIENT_CAP; }   // theirs goes in first
                else cap[s] = other_cap(loaded() - 1);
                sound_play(SND_CHIME);
            }
            break;
        }
        if (loaded() == tubes && CheckCollisionPointRec(m, spin_button())) start_spin();
    }
    if (loaded() == tubes && IsKeyPressed(KEY_SPACE)) start_spin();
}

// ---- Drawing ----

static void draw_tube_top(Vector2 c, Color capColour)
{
    DrawCircleV(c, SLOT_R - 4, (Color){ 230, 235, 240, 255 });   // the glass
    DrawCircleV(c, SLOT_R - 8, capColour);                       // the cap, seen from above
    DrawCircleV((Vector2){ c.x - 4, c.y - 4 }, 3, (Color){ 255, 255, 255, 120 });
}

// A tube standing in the rack, from the side
static void draw_tube_side(float x, float y, Color capColour)
{
    DrawRectangleRounded((Rectangle){ x - 9, y, 18, 62 }, 0.6f, 6, (Color){ 225, 232, 238, 255 });
    DrawRectangleRounded((Rectangle){ x - 7, y + 22, 14, 38 }, 0.6f, 6, (Color){ 150, 20, 30, 255 });   // blood
    DrawRectangleRounded((Rectangle){ x - 11, y - 8, 22, 14 }, 0.3f, 4, capColour);
}

void spingame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    Color bench = { 214, 220, 228, 255 };
    ClearBackground(bench);

    // The rotor wobbles round when it's unbalanced (more as it speeds up)
    float wob = 0;
    if (state == GAME_SPINNING && wobbling) wob = fminf(spinTime / 0.8f, 1.0f) * 14.0f;
    Vector2 shift = { wob * cosf(angle * 2 * PI), wob * sinf(angle * 2 * PI) };
    Camera2D cam = fit;
    cam.offset.x += shift.x * fit.zoom;
    cam.offset.y += shift.y * fit.zoom;

    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, bench);
    // The centrifuge's body
    DrawRectangleRounded((Rectangle){ ROTOR_X - 230, ROTOR_Y - 230, 460, 460 }, 0.2f, 12, (Color){ 240, 242, 246, 255 });
    DrawRectangleRoundedLines((Rectangle){ ROTOR_X - 230, ROTOR_Y - 230, 460, 460 }, 0.2f, 12, (Color){ 170, 178, 190, 255 });
    DrawCircleV((Vector2){ ROTOR_X, ROTOR_Y }, ROTOR_R + 18, (Color){ 60, 66, 76, 255 });   // the bowl
    EndMode2D();

    BeginMode2D(cam);
    // The rotor, its slots and tubes (turning while it spins)
    float turn = angle * 2 * PI;
    DrawCircleV((Vector2){ ROTOR_X, ROTOR_Y }, ROTOR_R, (Color){ 175, 182, 192, 255 });
    DrawCircleV((Vector2){ ROTOR_X, ROTOR_Y }, ROTOR_R - 70, (Color){ 160, 168, 178, 255 });
    DrawCircleV((Vector2){ ROTOR_X, ROTOR_Y }, 26, (Color){ 120, 126, 136, 255 });        // the hub
    Vector2 m = GetMousePosition();
    for (int s = 0; s < SLOTS; s++) {
        Vector2 c = slot_centre(s, turn);
        if (cracked[s]) {                            // a cracked slot: out of use
            DrawCircleV(c, SLOT_R, (Color){ 120, 70, 70, 255 });
            DrawLineEx((Vector2){ c.x - 10, c.y - 10 }, (Vector2){ c.x + 10, c.y + 10 }, 4, (Color){ 240, 90, 90, 255 });
            DrawLineEx((Vector2){ c.x + 10, c.y - 10 }, (Vector2){ c.x - 10, c.y + 10 }, 4, (Color){ 240, 90, 90, 255 });
            continue;
        }
        DrawCircleV(c, SLOT_R, (Color){ 70, 76, 86, 255 });
        if (filled[s]) draw_tube_top(c, cap[s]);
        else if (state == GAME_LOADING) {            // an empty slot lights up under the mouse
            float dx = m.x - c.x, dy = m.y - c.y;
            if (dx * dx + dy * dy < (SLOT_R + 6) * (SLOT_R + 6)) DrawCircleLines((int)c.x, (int)c.y, SLOT_R + 3, (Color){ 120, 255, 160, 255 });
        }
        if (state == GAME_LOADING)
            DrawText(TextFormat("%d", s + 1), (int)(ROTOR_X + (SLOT_RING + 36) * cosf(s * 2 * PI / SLOTS - PI / 2)) - 6,
                     (int)(ROTOR_Y + (SLOT_RING + 36) * sinf(s * 2 * PI / SLOTS - PI / 2)) - 7, 14, (Color){ 210, 215, 225, 255 });
    }
    EndMode2D();

    BeginMode2D(fit);
    // After an unbalanced spin: an arrow to the heavy side
    if (state == GAME_LOADING && messageLeft > 0) {
        float len = sqrtf(pull.x * pull.x + pull.y * pull.y);
        Vector2 dir = { pull.x / len, pull.y / len };
        Vector2 tip = { ROTOR_X + dir.x * 110, ROTOR_Y + dir.y * 110 };
        DrawLineEx((Vector2){ ROTOR_X, ROTOR_Y }, tip, 6, (Color){ 240, 70, 60, 255 });
        DrawCircleV(tip, 9, (Color){ 240, 70, 60, 255 });
        gameui_centre_text("UNBALANCED! It's heavy on the side the arrow points to.", 98, 22, (Color){ 200, 40, 40, 255 });
    }

    // The rack: the tubes still to load (this patient's has the red cap)
    DrawText("TUBES TO LOAD", 40, 110, 16, C_INK);
    DrawRectangleRounded((Rectangle){ 36, 210, 240, 20 }, 0.5f, 6, (Color){ 150, 156, 166, 255 });
    int left = tubes - loaded();
    for (int k = 0; k < left; k++) {
        bool theirs = (patientSlot < 0 && k == 0);
        draw_tube_side(56.0f + (k % 8) * 28.0f, 142.0f + (k / 8) * 80.0f, theirs ? PATIENT_CAP : other_cap(loaded() + k));
    }
    DrawText(TextFormat("%d of %d in the rotor", loaded(), tubes), 40, 240, 16, C_MUTED);
    DrawText(TextFormat("Spins left: %d", spinsLeft), 40, 262, 16, spinsLeft == 1 ? C_EMERGENCY : C_MUTED);

    // How to balance it
    DrawText("BALANCING", 40, 300, 14, C_MUTED);
    DrawText("Two tubes straight across", 40, 320, 16, C_INK);
    DrawText("from each other balance.", 40, 340, 16, C_INK);
    DrawText("So do three, evenly spaced", 40, 366, 16, C_INK);
    DrawText("(every 4th slot).", 40, 386, 16, C_INK);
    DrawText("Mix them for other numbers.", 40, 412, 16, C_INK);

    // The spin button
    Rectangle b = spin_button();
    bool ready = (loaded() == tubes && state == GAME_LOADING);
    DrawRectangleRounded(b, 0.4f, 8, ready ? C_BRAND : (Color){ 170, 176, 186, 255 });
    const char *bt = ready ? "Spin (SPACE)" : (state == GAME_SPINNING ? "Spinning..." : "Load every tube");
    DrawText(bt, (int)(b.x + b.width / 2) - MeasureText(bt, 18) / 2, (int)b.y + 14, 18, WHITE);

    gameui_top_bar("BALANCE THE CENTRIFUGE", patientText, timeLeft, timeLimit, (Color){ 40, 50, 90, 255 });

    if (state == GAME_LOADING) {
        gameui_centre_text("Click a slot to put a tube in (click again to take it out), then spin", 572, 16, C_INK);
        gameui_give_up_hint();
    } else if (state == GAME_WON || state == GAME_LOST) {
        bool won = (state == GAME_WON);
        const char *head = won ? "SPUN AND BALANCED" : outOfTime ? "OUT OF TIME" : gaveUp ? "GAVE UP" : "STILL UNBALANCED";
        const char *body;
        if (practice) body = won ? "Perfectly balanced! (practice round)" : "Not this time. (practice round)";
        else if (won) body = "Results back quickly: they'll be out of bloods sooner.";
        else if (gaveUp) body = "The lab will sort it out as usual.";
        else body = "The samples will be re-run: they'll be in bloods longer.";
        gameui_result(won, head, body, won ? TextFormat("%d tubes, %d %s", tubes, SPINS - spinsLeft, SPINS - spinsLeft == 1 ? "spin" : "spins") : NULL,
                      practice, resultDelay <= 0);
    }
    EndMode2D();
}
