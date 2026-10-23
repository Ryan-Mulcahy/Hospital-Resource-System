// defibgame.c - "Shock!", the defibrillator mini-game.
//
// The screen is a defibrillator's monitor. The patient's heart trace scrolls
// in from the right: a chaotic, wobbling line (the heart is quivering, not
// beating), with a tall spike every now and then. Near the left there's a
// marked zone. Press C to charge the paddles (a rising whine), then press
// SPACE to shock just as a spike passes through the zone. Three good shocks
// and the trace settles into a normal heartbeat. A shock at the wrong moment
// counts as a miss: three misses, or the clock running out, and you lose.
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "defibgame.h"

// ---- The monitor ----
#define TRACE_LEFT    60.0f      // the trace runs from here...
#define TRACE_RIGHT  940.0f      // ...to here (new readings come in on the right)
#define TRACE_MID    330.0f      // the trace's middle line
#define SCROLL_SPEED 300.0f      // pixels per second the trace moves left
#define ZONE_X       300.0f      // the middle of the shock zone

#define SHOCKS_NEEDED 3
#define MISSES_ALLOWED 3
#define CHARGE_TIME   1.2f       // seconds to charge (the length of the charging sound)

#define MAX_SPIKES 16

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_PLAYING, GAME_WON, GAME_LOST } GameState;

static GameState state = GAME_OFF;
static float clock_;             // seconds since the game started (the trace's time)
static float spikeAt[MAX_SPIKES];// when each spike came in on the right (trace time)
static bool  spikeHit[MAX_SPIKES];// shocked on this spike? (it turns green)
static int   spikeCount;
static float nextSpike;          // trace time of the next spike
static float zoneHalf;           // half the zone's width: narrower for emergencies
static float timeLeft, timeLimit;
static float charge;             // 0 to 1 while charging; 1 = charged
static bool  charging;
static int   good, misses;       // good shocks and missed ones
static float flash;              // the white flash after a shock (seconds left)
static bool  lastHit;            // was the last shock a good one? (for the message)
static float messageLeft;        // how long that message stays up
static float nagLeft;            // how long "charge first!" stays up (SPACE with the paddles not charged)
static bool  practice;           // a practice round: changes nothing
static bool  outOfTime;
static float wonAt;              // trace time the heart came back (for the normal trace)
static float resultDelay;        // a moment before the result screen takes a key
static int   patientId;          // the real patient being shocked (0 in practice)
static char  patientText[96];    // "Patient 42 - Cardiac arrest (I46.9)"
static float beepAt;             // next alarm beep while the heart is quivering

// ---- Offering the game ----
// Someone in an A&E bed with the defibrillator by them, there and not finished.
static int offerId = 0;
#define REMEMBER 8
static int offered[REMEMBER];    // recent patients already offered, so each one only gets offered once
static int offeredNext = 0;

static bool was_offered(int id)
{
    for (int i = 0; i < REMEMBER; i++) if (offered[i] == id) return true;
    return false;
}

static const Patient *shockable(const Sim *sim, int id)
{
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || p->walkLeft > 0.0f || p->doneInAE) continue;
        if (!(p->equipHeld & EQUIP_BIT(EQUIP_DEFIB))) continue;
        if (id == 0 || p->id == id) return p;
    }
    return NULL;
}

void defibgame_watch(const Sim *sim)
{
    // The one on offer has gone (treated, or moved on): stop offering
    if (offerId != 0 && shockable(sim, offerId) == NULL) offerId = 0;
    if (offerId != 0 || state != GAME_OFF) return;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || shockable(sim, p->id) == NULL || was_offered(p->id)) continue;
        offerId = p->id;                             // new: offer them
        offered[offeredNext] = p->id;
        offeredNext = (offeredNext + 1) % REMEMBER;
        return;
    }
}

int defibgame_offer(void) { return offerId; }

// ---- Starting ----
static void add_spike(float at)
{
    if (spikeCount == MAX_SPIKES) {                   // full: drop the oldest
        for (int i = 1; i < MAX_SPIKES; i++) { spikeAt[i - 1] = spikeAt[i]; spikeHit[i - 1] = spikeHit[i]; }
        spikeCount--;
    }
    spikeAt[spikeCount] = at;
    spikeHit[spikeCount] = false;
    spikeCount++;
}

void defibgame_start(const Sim *sim)
{
    const Patient *p = (offerId != 0) ? shockable(sim, offerId) : NULL;
    practice = (p == NULL);
    if (practice) {
        zoneHalf = 34.0f;
        timeLimit = 25.0f;
        patientId = 0;
        snprintf(patientText, sizeof patientText, "Practice round - nothing changes in the hospital");
    } else {
        // Emergencies are harder: a narrower zone and less time
        zoneHalf = (p->severity == 1) ? 26.0f : 32.0f;
        timeLimit = (p->severity == 1) ? 20.0f : 25.0f;
        patientId = p->id;
        snprintf(patientText, sizeof patientText, "Patient %d  -  %s (%s)", p->id, p->condition->name, p->condition->icd10);
        offerId = 0;
    }
    clock_ = 0;
    spikeCount = 0;
    nextSpike = 0.4f;
    timeLeft = timeLimit;
    charge = 0;
    charging = false;
    good = misses = 0;
    flash = 0;
    messageLeft = 0;
    nagLeft = 0;
    outOfTime = false;
    beepAt = 0;
    state = GAME_PLAYING;
}

bool defibgame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

// Where on screen something that came in at trace time t is now
static float x_of(float t) { return TRACE_RIGHT - (clock_ - t) * SCROLL_SPEED; }

static void finish(Sim *sim, bool won)
{
    state = won ? GAME_WON : GAME_LOST;
    charging = false;
    wonAt = clock_;
    resultDelay = 0.6f;
    sound_play(won ? SND_SUCCESS : SND_BUZZ);
    if (!practice) sim_shock_result(sim, patientId, won);
}

static void shock(Sim *sim)
{
    charge = 0;
    flash = 0.35f;
    messageLeft = 0.9f;
    sound_play(SND_ZAP);
    // Is a spike in the zone? (the nearest one that hasn't been used)
    lastHit = false;
    for (int i = 0; i < spikeCount; i++) {
        if (spikeHit[i]) continue;
        if (fabsf(x_of(spikeAt[i]) - ZONE_X) <= zoneHalf) { spikeHit[i] = true; lastHit = true; break; }
    }
    if (lastHit) good++; else misses++;
    if (good >= SHOCKS_NEEDED) finish(sim, true);
    else if (misses >= MISSES_ALLOWED) finish(sim, false);
}

void defibgame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();
    clock_ += dt;                                    // the trace keeps moving, even on the result screen
    if (flash > 0) flash -= dt;
    if (messageLeft > 0) messageLeft -= dt;
    if (nagLeft > 0) nagLeft -= dt;

    if (state != GAME_PLAYING) {
        // Result screen: carry on, or (practice) try again. A short wait first,
        // so a SPACE pressed for the last shock doesn't skip it.
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (practice && IsKeyPressed(KEY_R)) { defibgame_start(sim); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { finish(sim, false); return; }    // give up

    timeLeft -= dt;
    if (timeLeft <= 0) {
        timeLeft = 0;
        outOfTime = true;
        finish(sim, false);
        return;
    }

    // New spikes come in on the right, at uneven gaps
    while (nextSpike <= clock_) {
        add_spike(nextSpike);
        nextSpike += GetRandomValue(65, 150) / 100.0f;
    }

    // The alarm beeps while the heart is quivering
    beepAt -= dt;
    if (beepAt <= 0) { sound_play(SND_BEEP); beepAt = 0.5f; }

    // Charge with C; shock with SPACE once charged
    if (IsKeyPressed(KEY_C) && !charging && charge < 1.0f) {
        charging = true;
        charge = 0;
        sound_play(SND_CHARGE);
    }
    if (charging) {
        charge += dt / CHARGE_TIME;
        if (charge >= 1.0f) { charge = 1.0f; charging = false; }
    }
    if (IsKeyPressed(KEY_SPACE)) {
        if (charge >= 1.0f) shock(sim);
        else nagLeft = 0.9f;                         // not charged yet: "charge first!"
    }
}

// ---- Drawing ----

// The height of the trace (up is positive, in pixels) at trace time t
static float trace_at(float t)
{
    if (state == GAME_WON && t > wonAt + 0.3f) {
        // A normal heartbeat: a small bump, the tall spike, a dip, then a wave
        float b = fmodf(t - wonAt, 0.85f) / 0.85f;
        if (b < 0.10f) return 18.0f * sinf(b / 0.10f * PI);
        if (b > 0.18f && b < 0.21f) return -25.0f * (b - 0.18f) / 0.03f;
        if (b > 0.21f && b < 0.25f) return -25.0f + 175.0f * (b - 0.21f) / 0.04f;
        if (b > 0.25f && b < 0.29f) return 150.0f - 190.0f * (b - 0.25f) / 0.04f;
        if (b > 0.29f && b < 0.32f) return -40.0f + 40.0f * (b - 0.29f) / 0.03f;
        if (b > 0.45f && b < 0.65f) return 35.0f * sinf((b - 0.45f) / 0.20f * PI);
        return 0;
    }
    if (state == GAME_LOST && t > wonAt + 0.3f) return 1.5f * sinf(t * 3.0f);    // nearly flat
    // Quivering: a few uneven waves added together, a little calmer after each good shock
    float size = 1.0f - 0.2f * good;
    float v = 38.0f * sinf(t * 23.0f) + 26.0f * sinf(t * 37.0f + 1.3f) + 16.0f * sinf(t * 61.0f + 0.7f);
    v *= size * (0.7f + 0.3f * sinf(t * 2.1f));
    // ...and the tall spikes
    for (int i = 0; i < spikeCount; i++) {
        float d = (t - spikeAt[i]) / 0.025f;
        if (d > -3 && d < 3) v += 165.0f * expf(-d * d);
    }
    return v;
}

static void centre_text(const char *text, int y, int size, Color c)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, c);
}

void defibgame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    Color green = { 80, 255, 140, 255 };
    Color dim = { 40, 110, 70, 255 };

    ClearBackground((Color){ 18, 20, 24, 255 });
    BeginMode2D(fit);

    // The defibrillator's case and screen
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 40, 44, 52, 255 });
    DrawRectangleRounded((Rectangle){ 30, 96, 940, 400 }, 0.06f, 8, (Color){ 230, 120, 30, 255 });   // orange case
    Rectangle screen = { 44, 110, 912, 372 };
    DrawRectangleRounded(screen, 0.04f, 8, (Color){ 8, 16, 12, 255 });
    for (int gx = (int)TRACE_LEFT; gx <= (int)TRACE_RIGHT; gx += 40)                // the grid
        DrawLine(gx, (int)screen.y + 8, gx, (int)(screen.y + screen.height) - 8, (Color){ 20, 40, 28, 255 });
    for (int gy = (int)screen.y + 20; gy < (int)(screen.y + screen.height); gy += 40)
        DrawLine((int)TRACE_LEFT, gy, (int)TRACE_RIGHT, gy, (Color){ 20, 40, 28, 255 });

    // The shock zone
    if (state == GAME_PLAYING) {
        Color zc = (charge >= 1.0f) ? (Color){ 255, 210, 60, 70 } : (Color){ 120, 160, 255, 40 };
        DrawRectangle((int)(ZONE_X - zoneHalf), (int)screen.y + 8, (int)(zoneHalf * 2), (int)screen.height - 16, zc);
        Color edge = (charge >= 1.0f) ? (Color){ 255, 210, 60, 200 } : (Color){ 120, 160, 255, 140 };
        DrawLine((int)(ZONE_X - zoneHalf), (int)screen.y + 8, (int)(ZONE_X - zoneHalf), (int)(screen.y + screen.height) - 8, edge);
        DrawLine((int)(ZONE_X + zoneHalf), (int)screen.y + 8, (int)(ZONE_X + zoneHalf), (int)(screen.y + screen.height) - 8, edge);
        DrawText("SHOCK ZONE", (int)(ZONE_X - MeasureText("SHOCK ZONE", 14) / 2), (int)screen.y + 14, 14, edge);
    }

    // The trace, one short line per few pixels
    Vector2 prev = { 0 };
    for (int i = 0; i <= 440; i++) {
        float x = TRACE_LEFT + (TRACE_RIGHT - TRACE_LEFT) * i / 440.0f;
        float t = clock_ - (TRACE_RIGHT - x) / SCROLL_SPEED;
        if (t < 0) { prev.x = -1; continue; }                     // nothing recorded yet
        Vector2 pt = { x, TRACE_MID - trace_at(t) };
        Color c = green;
        for (int s = 0; s < spikeCount; s++)                      // spikes you shocked on turn bright
            if (spikeHit[s] && fabsf(t - spikeAt[s]) < 0.06f) c = (Color){ 190, 255, 120, 255 };
        if (state == GAME_LOST && t > wonAt) c = (Color){ 255, 80, 90, 255 };
        if (i > 0 && prev.x >= 0) DrawLineEx(prev, pt, 3, c);
        prev = pt;
    }
    // A bright dot where the newest reading is
    DrawCircleV((Vector2){ TRACE_RIGHT, TRACE_MID - trace_at(clock_) }, 4, WHITE);

    // The readings in the corner
    const char *rhythm = (state == GAME_WON) ? "SINUS RHYTHM  72 bpm" : (state == GAME_LOST) ? "ASYSTOLE" : "VF  - SHOCKABLE";
    DrawText(rhythm, (int)screen.x + 16, (int)(screen.y + screen.height) - 34, 20,
             state == GAME_LOST ? (Color){ 255, 80, 90, 255 } : green);
    for (int i = 0; i < SHOCKS_NEEDED; i++) {        // good shocks so far: three hearts
        Color c = (i < good) ? (Color){ 90, 255, 150, 255 } : dim;
        DrawText("<3", (int)(screen.x + screen.width) - 170 + i * 50, (int)(screen.y + screen.height) - 36, 24, c);
    }

    // The charge bar under the screen
    DrawRectangleRounded((Rectangle){ 300, 512, 400, 22 }, 1.0f, 8, (Color){ 20, 22, 28, 255 });
    Color cc = (charge >= 1.0f) ? (Color){ 255, 210, 60, 255 } : (Color){ 120, 160, 255, 255 };
    if (charge > 0) DrawRectangleRounded((Rectangle){ 300, 512, 400 * charge, 22 }, 1.0f, 8, cc);
    const char *ct = (charge >= 1.0f) ? "CHARGED 200 J - SPACE to shock" : charging ? "CHARGING..." : "C to charge";
    DrawText(ct, SCREEN_W / 2 - MeasureText(ct, 16) / 2, 515, 16, (charge >= 1.0f) ? (Color){ 30, 30, 30, 255 } : WHITE);
    DrawText(TextFormat("Misses %d / %d", misses, MISSES_ALLOWED), 720, 514, 18,
             misses > 0 ? (Color){ 255, 120, 110, 255 } : (Color){ 180, 190, 200, 255 });

    // The top bar: what you're doing, who for, and the clock
    DrawRectangle(0, 0, SCREEN_W, 84, (Color){ 28, 30, 36, 255 });
    DrawText("SHOCK! Bring the heart back", 24, 14, 26, WHITE);
    DrawText(patientText, 24, 50, 16, (Color){ 190, 200, 210, 255 });
    float frac = timeLeft / timeLimit;
    Color timeCol = frac > 0.5f ? (Color){ 90, 220, 140, 255 } : frac > 0.25f ? (Color){ 240, 190, 60, 255 } : (Color){ 240, 80, 80, 255 };
    DrawRectangleRounded((Rectangle){ 700, 54, 270, 12 }, 1.0f, 6, (Color){ 10, 12, 16, 255 });
    DrawRectangleRounded((Rectangle){ 700, 54, 270 * frac, 12 }, 1.0f, 6, timeCol);
    DrawText(TextFormat("%.0f s", ceilf(timeLeft)), 700, 30, 18, timeCol);

    if (state == GAME_PLAYING) {
        centre_text("C charge, then SPACE as a tall spike passes through the zone. Three good shocks.", 552, 16,
                    (Color){ 210, 215, 225, 255 });
        DrawText("Q give up", SCREEN_W - 110, 576, 14, (Color){ 150, 160, 170, 255 });
        // What the last shock did
        if (messageLeft > 0) {
            const char *m = lastHit ? "GOOD SHOCK!" : "MISSED - wrong moment";
            centre_text(m, 150, 28, lastHit ? (Color){ 150, 255, 170, 255 } : (Color){ 255, 120, 110, 255 });
        } else if (nagLeft > 0) {
            centre_text("Charge first! (C)", 150, 28, (Color){ 255, 210, 60, 255 });
        }
    }

    // The flash when the shock goes through
    if (flash > 0) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 255, 255, 255, (unsigned char)(flash / 0.35f * 200) });
        centre_text("STAND CLEAR!", 260, 54, (Color){ 230, 60, 40, (unsigned char)(flash / 0.35f * 255) });
    }

    // The result
    if (state == GAME_WON || state == GAME_LOST) {
        bool won = (state == GAME_WON);
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, won ? (Color){ 20, 80, 50, 120 } : (Color){ 120, 10, 20, 140 });
        Rectangle box = { 250, 200, 500, 200 };
        DrawRectangleRounded(box, 0.15f, 10, won ? (Color){ 30, 120, 80, 245 } : (Color){ 150, 30, 40, 245 });
        const char *head = won ? "HEART RESTARTED" : (outOfTime ? "OUT OF TIME" : "TOO MANY MISSES");
        centre_text(head, 225, 32, WHITE);
        const char *body;
        if (practice) body = won ? "Great timing! (practice round)" : "Keep at it. (practice round)";
        else          body = won ? "Their treatment will be quicker." : "Their treatment in A&E will take longer.";
        centre_text(body, 280, 20, (Color){ 235, 245, 240, 255 });
        const char *hint = practice ? "R try again    -    click or ENTER to carry on" : "Click or press ENTER to carry on";
        if (resultDelay <= 0) centre_text(hint, 345, 18, (Color){ 230, 240, 235, 255 });
    }
    EndMode2D();
}
