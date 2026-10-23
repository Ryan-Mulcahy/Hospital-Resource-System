// pharmgame.c - "Pharmacy rush", making up a prescription.
//
// The shelf on the left has eight tubs: tablets and capsules in four colours,
// in a different order each time. The prescription is on the right, with the
// bag under it. Click a tub to put one pill from it in the bag. The
// prescription ticks off as you go, and it's done the moment the bag matches
// it. One pill that isn't on it (or one too many) and it's wrong.
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
#include "pharmgame.h"

// ---- The pills ----
#define COLOURS 4
#define KINDS   (COLOURS * 2)    // each colour as a tablet and as a capsule
static const char *COLOUR_NAME[COLOURS] = { "blue", "white", "red", "yellow" };
static const Color COLOUR[COLOURS] = {
    { 70, 120, 220, 255 }, { 240, 240, 236, 255 }, { 220, 70, 70, 255 }, { 240, 200, 60, 255 }
};
static int  colour_of(int kind) { return kind % COLOURS; }
static bool is_capsule(int kind) { return kind >= COLOURS; }

#define MAX_LINES 3              // different pills on one prescription
#define FAST_TIME 9.0f           // done within this many seconds counts as quick

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_PLAYING, GAME_DONE } GameState;
typedef enum { END_FAST, END_SLOW, END_WRONG, END_TIMEOUT, END_GAVE_UP } Ending;

static GameState state = GAME_OFF;
static Ending ending;
static int   shelf[KINDS];       // which kind is in each tub on the shelf (shuffled)
static int   lineKind[MAX_LINES], lineCount[MAX_LINES], lines;   // the prescription
static int   bag[KINDS];         // how many of each kind are in the bag
static int   wrongKind;          // the pill that was wrong (for the message)
static float timeLeft, timeLimit, taken;
static float resultDelay;
static float flashTub[KINDS];    // a tub lights up for a moment when clicked
static bool  practice;
static int   patientId;
static char  patientText[96];

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

static const Patient *at_pharmacy(const Sim *sim, int id)
{
    const SimRoom *room = &sim->rooms[ROOM_PHARMACY];
    for (int s = 0; s < room->capacity; s++) {
        const Patient *p = room->in[s];
        if (p == NULL || p->walkLeft > 0.0f || p->roomLeft <= 0.0f) continue;
        if (id == 0 || p->id == id) return p;
    }
    return NULL;
}

void pharmgame_watch(const Sim *sim)
{
    if (offerId != 0 && at_pharmacy(sim, offerId) == NULL) offerId = 0;   // they've gone
    if (offerId != 0 || state != GAME_OFF) return;
    const SimRoom *room = &sim->rooms[ROOM_PHARMACY];
    for (int s = 0; s < room->capacity; s++) {
        const Patient *p = room->in[s];
        if (p == NULL || at_pharmacy(sim, p->id) == NULL || was_offered(p->id)) continue;
        offerId = p->id;
        offered[offeredNext] = p->id;
        offeredNext = (offeredNext + 1) % REMEMBER;
        return;
    }
}

int pharmgame_offer(void) { return offerId; }

// ---- Starting ----
void pharmgame_start(const Sim *sim)
{
    const Patient *p = (offerId != 0) ? at_pharmacy(sim, offerId) : NULL;
    practice = (p == NULL);
    if (practice) {
        patientId = 0;
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
        lines = GetRandomValue(2, 3);
    } else {
        patientId = p->id;
        snprintf(patientText, sizeof patientText, "Patient %d  -  %s (%s)", p->id, p->condition->name, p->condition->icd10);
        lines = (p->severity <= 2) ? 3 : 2;          // the sicker ones need more
        offerId = 0;
    }
    // Shuffle the shelf
    for (int k = 0; k < KINDS; k++) shelf[k] = k;
    for (int k = KINDS - 1; k > 0; k--) {
        int j = GetRandomValue(0, k);
        int t = shelf[k]; shelf[k] = shelf[j]; shelf[j] = t;
    }
    // The prescription: different kinds, 1 to 3 of each
    for (int i = 0; i < lines; i++) {
        int k;
        bool used;
        do {
            k = GetRandomValue(0, KINDS - 1);
            used = false;
            for (int j = 0; j < i; j++) if (lineKind[j] == k) used = true;
        } while (used);
        lineKind[i] = k;
        lineCount[i] = GetRandomValue(1, 3);
    }
    for (int k = 0; k < KINDS; k++) { bag[k] = 0; flashTub[k] = 0; }
    timeLimit = 15.0f;
    timeLeft = timeLimit;
    taken = 0;
    state = GAME_PLAYING;
}

bool pharmgame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

static Rectangle tub_rect(int slot)
{
    return (Rectangle){ 50.0f + (slot % 4) * 150.0f, 130.0f + (slot / 4) * 190.0f, 130.0f, 160.0f };
}

static int wanted(int kind)
{
    for (int i = 0; i < lines; i++) if (lineKind[i] == kind) return lineCount[i];
    return 0;
}

static bool bag_matches(void)
{
    for (int k = 0; k < KINDS; k++) if (bag[k] != wanted(k)) return false;
    return true;
}

static void finish(Sim *sim, Ending how)
{
    ending = how;
    state = GAME_DONE;
    resultDelay = 0.4f;
    sound_play(how == END_FAST || how == END_SLOW ? SND_SUCCESS : SND_BUZZ);
    if (practice || how == END_GAVE_UP) return;
    PharmacyResult r = (how == END_FAST) ? PHARMACY_FAST : (how == END_SLOW) ? PHARMACY_SLOW
                     : (how == END_WRONG) ? PHARMACY_WRONG : PHARMACY_TIMEOUT;
    sim_pharmacy_result(sim, patientId, r);
}

void pharmgame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();
    for (int k = 0; k < KINDS; k++) if (flashTub[k] > 0) flashTub[k] -= dt;

    if (state == GAME_DONE) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (practice && IsKeyPressed(KEY_R)) { pharmgame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { finish(sim, END_GAVE_UP); return; }

    timeLeft -= dt;
    taken += dt;
    if (timeLeft <= 0) { timeLeft = 0; finish(sim, END_TIMEOUT); return; }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        for (int slot = 0; slot < KINDS; slot++) {
            if (!CheckCollisionPointRec(m, tub_rect(slot))) continue;
            int k = shelf[slot];
            bag[k]++;
            flashTub[slot] = 0.15f;
            if (bag[k] > wanted(k)) { wrongKind = k; finish(sim, END_WRONG); return; }
            sound_play(SND_BEEP);
            if (bag_matches()) finish(sim, taken <= FAST_TIME ? END_FAST : END_SLOW);
            return;
        }
    }
}

// ---- Drawing ----

static void draw_pill(int kind, float x, float y, float size)
{
    Color c = COLOUR[colour_of(kind)];
    Color edge = { (unsigned char)(c.r * 0.7f), (unsigned char)(c.g * 0.7f), (unsigned char)(c.b * 0.7f), 255 };
    if (is_capsule(kind)) {
        Rectangle r = { x - size, y - size * 0.45f, size * 2, size * 0.9f };
        DrawRectangleRounded(r, 1.0f, 10, c);
        DrawRectangleRounded((Rectangle){ r.x, r.y, r.width / 2, r.height }, 1.0f, 10, edge);   // two halves
        DrawRectangleRoundedLines(r, 1.0f, 10, edge);
    } else {
        DrawCircleV((Vector2){ x, y }, size * 0.7f, edge);
        DrawCircleV((Vector2){ x, y }, size * 0.6f, c);
        DrawLineEx((Vector2){ x - size * 0.4f, y }, (Vector2){ x + size * 0.4f, y }, 2, edge);   // the score line
    }
}

static const char *kind_name(int kind)
{
    return TextFormat("%s %s", COLOUR_NAME[colour_of(kind)], is_capsule(kind) ? "capsule" : "tablet");
}

void pharmgame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    ClearBackground((Color){ 236, 240, 236, 255 });
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 236, 240, 236, 255 });

    // The shelf: two rows of tubs
    DrawRectangle(30, 110, 610, 400, (Color){ 186, 150, 110, 255 });
    DrawRectangle(30, 290, 610, 12, (Color){ 150, 115, 80, 255 });
    DrawRectangle(30, 480, 610, 12, (Color){ 150, 115, 80, 255 });
    Vector2 m = GetMousePosition();
    for (int slot = 0; slot < KINDS; slot++) {
        Rectangle r = tub_rect(slot);
        bool hover = CheckCollisionPointRec(m, r) && state == GAME_PLAYING;
        Color tub = flashTub[slot] > 0 ? (Color){ 255, 255, 220, 255 } : hover ? (Color){ 250, 252, 255, 255 } : (Color){ 228, 234, 240, 255 };
        DrawRectangleRounded(r, 0.12f, 8, tub);
        DrawRectangleRoundedLines(r, 0.12f, 8, (Color){ 140, 150, 160, 255 });
        int k = shelf[slot];
        for (int n = 0; n < 5; n++)                  // a few pills in the tub
            draw_pill(k, r.x + 34 + (n % 3) * 32, r.y + 50 + (n / 3) * 40, 14);
    }

    // The prescription, ticking off as the bag fills
    Rectangle rx = { 670, 110, 300, 220 };
    DrawRectangleRounded(rx, 0.05f, 8, WHITE);
    DrawRectangleRoundedLines(rx, 0.05f, 8, (Color){ 160, 170, 180, 255 });
    DrawText("PRESCRIPTION", (int)rx.x + 18, (int)rx.y + 14, 20, C_INK);
    for (int i = 0; i < lines; i++) {
        int y = (int)rx.y + 56 + i * 52;
        int k = lineKind[i];
        draw_pill(k, rx.x + 40, (float)y + 12, 14);
        DrawText(TextFormat("%d x %s", lineCount[i], kind_name(k)), (int)rx.x + 66, y + 2, 18, C_INK);
        int have = bag[k] < lineCount[i] ? bag[k] : lineCount[i];
        Color tick = (have == lineCount[i]) ? C_ROUTINE : C_MUTED;
        DrawText(TextFormat("%d / %d", have, lineCount[i]), (int)rx.x + 66, y + 24, 14, tick);
    }

    // The bag, with what's in it
    Rectangle b = { 700, 350, 240, 150 };
    DrawRectangleRounded(b, 0.1f, 8, (Color){ 250, 250, 240, 255 });
    DrawRectangleRoundedLines(b, 0.1f, 8, (Color){ 170, 160, 130, 255 });
    DrawText("BAG", (int)b.x + 14, (int)b.y + 10, 16, C_MUTED);
    int n = 0;
    for (int k = 0; k < KINDS; k++)
        for (int c = 0; c < bag[k] && n < 14; c++, n++)
            draw_pill(k, b.x + 30 + (n % 7) * 30, b.y + 60 + (n / 7) * 40, 11);

    gameui_top_bar("PHARMACY RUSH: make up the prescription", patientText, timeLeft, timeLimit, (Color){ 40, 90, 70, 255 });

    if (state == GAME_PLAYING) {
        gameui_centre_text("Click a tub to put one pill in the bag. Tablets are round; capsules are long.", 548, 18, C_INK);
        DrawText(TextFormat("Quick if done in %.0f s", FAST_TIME), 700, 510, 14, C_MUTED);
        gameui_give_up_hint();
    } else {
        bool won = (ending == END_FAST || ending == END_SLOW);
        const char *head = ending == END_FAST ? "SPOT ON, AND QUICK" : ending == END_SLOW ? "RIGHT, BUT SLOW"
                         : ending == END_WRONG ? "WRONG PILL" : ending == END_TIMEOUT ? "OUT OF TIME" : "GAVE UP";
        const char *body;
        if (practice) body = won ? "All the right pills. (practice round)" : "Not quite. (practice round)";
        else if (ending == END_FAST)  body = "Their pharmacy visit will be quicker.";
        else if (ending == END_SLOW)  body = "No time saved, but they have their medicine.";
        else if (ending == END_WRONG) body = "Checked at the counter: back in the queue for them.";
        else if (ending == END_TIMEOUT) body = "They'll be waiting longer for it.";
        else body = "The pharmacist will make it up as usual.";
        const char *extra = (ending == END_WRONG) ? TextFormat("A %s isn't on the prescription (or was one too many).", kind_name(wrongKind))
                          : (won ? TextFormat("Made up in %.1f s", taken) : NULL);
        gameui_result(won, head, body, extra, practice, resultDelay <= 0);
    }
    EndMode2D();
}
