// triagegame.c - "Triage call", sorting three new arrivals.
//
// Three cards across the top, one per arrival. Three boxes below them:
// EMERGENCY, URGENT and ROUTINE. Hold the left button on a card and drag it
// into a box; drag it again to change your mind. With all three in a box,
// click "Send them through" (or press ENTER). The result shows each call next
// to the right answer.
//
// Everything is drawn in the same 1000x600 layout as the other 2D screens
// (main.c scales it up to the window).

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "raylib.h"
#include "draw.h"            // colours and screen size
#include "sound.h"
#include "gameui.h"
#include "triagegame.h"

#define ARRIVALS 3

// ---- How each condition looks when someone walks in ----
// What the triage nurse sees and hears, rather than the diagnosis. Anything
// not listed (a condition added to conditions.csv later) shows its name.
static const struct { const char *condition, *looks; } LOOKS[] = {
    { "Cardiac arrest",            "collapsed in the car park, not breathing" },
    { "Sepsis",                    "fever and shaking, confused, mottled skin" },
    { "Stroke",                    "face drooping, can't lift one arm" },
    { "Burst appendix",            "rigid stomach, screaming with pain" },
    { "Head injury (car crash)",   "car crash, drowsy, bleeding from the head" },
    { "Heart attack",              "crushing chest pain, grey and sweaty" },
    { "Overdose",                  "found unresponsive, empty pill packets" },
    { "Internal bleeding",         "fell off a ladder, pale, stomach swelling" },
    { "Labour with complications", "in labour, heavy bleeding" },
    { "Chest pain",                "tight chest on and off since this morning" },
    { "Asthma attack",             "wheezing, can only say a few words" },
    { "Broken wrist",              "fell on an outstretched hand, wrist bent" },
    { "Kidney stone",              "pain in the side that comes in waves" },
    { "Severe headache",           "worst headache of their life" },
    { "Pneumonia",                 "cough, fever, short of breath" },
    { "Broken hip",                "fell at home, leg turned out, can't stand" },
    { "Kidney failure",            "missed dialysis, swollen ankles, tired" },
    { "Child with high fever",     "child, 40 degree fever, floppy" },
    { "Labour",                    "contractions every 4 minutes" },
    { "Gallstones",                "pain under the right ribs after eating" },
    { "Irregular heartbeat",       "heart racing and fluttering, dizzy" },
    { "Child with asthma",         "child, wheezy, using their inhaler a lot" },
    { "Dehydration",               "vomiting for two days, can't keep water down" },
    { "Ankle sprain",              "rolled their ankle playing football" },
    { "Cut hand",                  "cut hand on a knife, bleeding stopped" },
    { "Ear infection",             "earache for three days" },
    { "Back pain",                 "sore back after lifting boxes" },
    { "Dizziness",                 "a bit dizzy when standing up" },
    { "Chest infection",           "chesty cough for a week, otherwise well" },
    { "Skin rash",                 "itchy rash on the arms" },
    { "Sprained knee",             "twisted knee, walking on it" },
    { "Child with a broken arm",   "child, fell off a trampoline, sore arm" },
    { "Pregnancy check",           "pregnant, worried, baby moving well" },
    { "Child with a temperature",  "child, mild temperature, eating and playing" },
};

static const char *looks_for(const char *condition)
{
    for (size_t i = 0; i < sizeof LOOKS / sizeof LOOKS[0]; i++)
        if (strcmp(LOOKS[i].condition, condition) == 0) return LOOKS[i].looks;
    return condition;
}

// ---- The game in progress ----
typedef enum { GAME_OFF, GAME_PLAYING, GAME_DONE } GameState;

static GameState state = GAME_OFF;
static Patient *arrival[ARRIVALS];   // the new arrivals (handed to the simulation at the end)
static int   ids[ARRIVALS];
static int   realSeverity[ARRIVALS]; // the right answer
static int   call[ARRIVALS];         // the box each one is in: 1, 2, 3 (0 = not sorted yet)
static char  looks[ARRIVALS][80];    // "crushing chest pain, grey and sweaty"
static char  vitals[ARRIVALS][40];   // "Age 58   HR 132   BP 86/50"
static int   dragging = -1;          // the card being dragged, or -1
static Vector2 dragGrab;             // where on the card it was picked up
static float timeLeft, timeLimit;
static bool  outOfTime, gaveUp;
static float resultDelay;

// ---- Where things are ----
static Rectangle home_rect(int i) { return (Rectangle){ 40.0f + i * 315.0f, 100.0f, 290.0f, 120.0f }; }
static Rectangle box_rect(int b) { return (Rectangle){ 40.0f + (b - 1) * 315.0f, 250.0f, 290.0f, 262.0f }; }
static Rectangle done_button(void) { return (Rectangle){ 360, 528, 280, 40 }; }

// Where card i is drawn: at home, or small in its box
static Rectangle card_rect(int i)
{
    if (call[i] == 0) return home_rect(i);
    int k = 0;                                       // how many before it in the same box
    for (int j = 0; j < i; j++) if (call[j] == call[i]) k++;
    Rectangle b = box_rect(call[i]);
    return (Rectangle){ b.x + 10, b.y + 44 + k * 72, b.width - 20, 64 };
}

static const char *PRIORITY[4] = { "-", "EMERGENCY", "URGENT", "ROUTINE" };
static Color priority_colour(int p)
{
    return p == 1 ? C_EMERGENCY : p == 2 ? C_URGENT : p == 3 ? C_ROUTINE : (Color){ 120, 128, 140, 255 };
}

// ---- Starting ----
void triagegame_start(Sim *sim)
{
    for (int i = 0; i < ARRIVALS; i++) {
        Patient *p = sim_triage_new(sim);
        arrival[i] = p;
        call[i] = 0;
        if (p == NULL) { ids[i] = 0; realSeverity[i] = 3; looks[i][0] = 0; vitals[i][0] = 0; continue; }
        ids[i] = p->id;
        realSeverity[i] = p->severity;
        snprintf(looks[i], sizeof looks[i], "%s", looks_for(p->condition->name));
        looks[i][0] = (char)toupper((unsigned char)looks[i][0]);   // "Crushing chest pain..."
        // Their age and observations: worse numbers for the sicker ones (with some overlap)
        const char *name = p->condition->name;
        int age = strstr(name, "Child") ? GetRandomValue(2, 12)
                : (strstr(name, "abour") || strstr(name, "regnan")) ? GetRandomValue(21, 39)
                : GetRandomValue(18, 88);
        int hr = p->severity == 1 ? GetRandomValue(118, 150) : p->severity == 2 ? GetRandomValue(92, 120) : GetRandomValue(64, 96);
        int sys = p->severity == 1 ? GetRandomValue(78, 100) : p->severity == 2 ? GetRandomValue(98, 140) : GetRandomValue(112, 138);
        snprintf(vitals[i], sizeof vitals[i], "Age %d    HR %d    BP %d/%d", age, hr, sys, sys * 6 / 10);
    }
    dragging = -1;
    timeLimit = 25.0f;
    timeLeft = timeLimit;
    outOfTime = gaveUp = false;
    state = GAME_PLAYING;
}

bool triagegame_active(void) { return state != GAME_OFF; }

// ---- Playing ----

// Send everyone through: with the calls made (unsorted ones go in as routine
// when time ran out; giving up lets the triage nurse sort them properly)
static void finish(Sim *sim)
{
    bool allRight = true;
    for (int i = 0; i < ARRIVALS; i++) {
        if (outOfTime && call[i] == 0) call[i] = 3;
        if (call[i] != realSeverity[i]) allRight = false;
        sim_triage_admit(sim, arrival[i], gaveUp ? 0 : call[i]);
        arrival[i] = NULL;                           // the simulation has them now
    }
    if (!gaveUp) sim_triage_game(sim, allRight);
    sound_play(allRight && !gaveUp ? SND_SUCCESS : SND_BUZZ);
    dragging = -1;
    resultDelay = 0.4f;
    state = GAME_DONE;
}

void triagegame_update(Sim *sim)
{
    if (state == GAME_OFF) return;
    float dt = GetFrameTime();

    if (state == GAME_DONE) {
        if (resultDelay > 0) { resultDelay -= dt; return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) state = GAME_OFF;
        return;
    }
    if (IsKeyPressed(KEY_Q)) { gaveUp = true; finish(sim); return; }

    timeLeft -= dt;
    if (timeLeft <= 0) {
        timeLeft = 0;
        outOfTime = true;
        finish(sim);
        return;
    }

    Vector2 m = GetMousePosition();
    bool allSorted = true;
    for (int i = 0; i < ARRIVALS; i++) if (call[i] == 0) allSorted = false;

    if (dragging < 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (allSorted && CheckCollisionPointRec(m, done_button())) { finish(sim); return; }
        for (int i = ARRIVALS - 1; i >= 0; i--) {   // pick up a card
            Rectangle r = card_rect(i);
            if (CheckCollisionPointRec(m, r)) {
                dragging = i;
                dragGrab = (Vector2){ m.x - r.x, m.y - r.y };
                if (call[i] != 0) dragGrab = (Vector2){ 40, 20 };   // a small card: hold it by its corner
                break;
            }
        }
    }
    if (dragging >= 0 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {   // dropped: into a box, or back home
        int into = 0;
        for (int b = 1; b <= 3; b++) if (CheckCollisionPointRec(m, box_rect(b))) into = b;
        call[dragging] = into;
        if (into != 0) sound_play(SND_BEEP);
        dragging = -1;
    }
    if (allSorted && IsKeyPressed(KEY_ENTER)) finish(sim);
}

// ---- Drawing ----

static void draw_card(int i, Rectangle r, bool small)
{
    Color edge = (call[i] != 0) ? priority_colour(call[i]) : (Color){ 90, 100, 120, 255 };
    DrawRectangleRounded(r, 0.12f, 8, (Color){ 250, 250, 252, 255 });
    DrawRectangleRoundedLines(r, 0.12f, 8, edge);
    DrawRectangle((int)r.x, (int)r.y + 8, 6, (int)r.height - 16, edge);
    Color ink = { 30, 36, 48, 255 }, muted = { 100, 108, 122, 255 };
    if (small) {
        DrawText(TextFormat("Patient %d", ids[i]), (int)r.x + 16, (int)r.y + 8, 16, ink);
        DrawText(TextFormat("%.34s%s", looks[i], strlen(looks[i]) > 34 ? "..." : ""), (int)r.x + 16, (int)r.y + 30, 14, muted);
        return;
    }
    DrawText(TextFormat("Patient %d", ids[i]), (int)r.x + 16, (int)r.y + 12, 20, ink);
    // The description, over two lines if it's long: as many words on the
    // first line as fit
    const char *t = looks[i];
    int len = (int)strlen(t), split = len;
    if (MeasureText(t, 16) > (int)r.width - 32) {
        for (int k = len - 1; k > 0; k--) {
            if (t[k] != ' ') continue;
            if (MeasureText(TextFormat("%.*s", k, t), 16) <= (int)r.width - 32) { split = k + 1; break; }
        }
    }
    DrawText(TextFormat("%.*s", split, t), (int)r.x + 16, (int)r.y + 42, 16, ink);
    if (split < len) DrawText(t + split, (int)r.x + 16, (int)r.y + 62, 16, ink);
    DrawText(vitals[i], (int)r.x + 16, (int)r.y + 92, 14, muted);
}

void triagegame_draw(Camera2D fit)
{
    if (state == GAME_OFF) return;
    ClearBackground((Color){ 230, 234, 240, 255 });
    BeginMode2D(fit);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 230, 234, 240, 255 });

    // The three boxes
    for (int b = 1; b <= 3; b++) {
        Rectangle r = box_rect(b);
        Color c = priority_colour(b);
        DrawRectangleRounded(r, 0.06f, 8, (Color){ c.r, c.g, c.b, 40 });
        DrawRectangleRoundedLines(r, 0.06f, 8, c);
        DrawText(PRIORITY[b], (int)(r.x + r.width / 2) - MeasureText(PRIORITY[b], 22) / 2, (int)r.y + 12, 22, c);
    }
    // Empty places where unsorted cards started
    for (int i = 0; i < ARRIVALS; i++) {
        if (call[i] != 0 || dragging == i) DrawRectangleRoundedLines(home_rect(i), 0.12f, 8, (Color){ 190, 196, 206, 255 });
    }
    // The cards (the one being dragged last, on top, full size at the mouse)
    for (int i = 0; i < ARRIVALS; i++) if (i != dragging) draw_card(i, card_rect(i), call[i] != 0);
    if (dragging >= 0) {
        Vector2 m = GetMousePosition();
        Rectangle r = home_rect(dragging);
        r.x = m.x - dragGrab.x;
        r.y = m.y - dragGrab.y;
        DrawRectangleRounded((Rectangle){ r.x + 6, r.y + 8, r.width, r.height }, 0.12f, 8, (Color){ 0, 0, 0, 40 });   // shadow
        draw_card(dragging, r, false);
    }

    gameui_top_bar("TRIAGE CALL: three new arrivals", "Drag each one to how soon they need to be seen",
                   timeLeft, timeLimit, (Color){ 30, 50, 80, 255 });

    if (state == GAME_PLAYING) {
        bool allSorted = true;
        for (int i = 0; i < ARRIVALS; i++) if (call[i] == 0) allSorted = false;
        Rectangle d = done_button();
        Color dc = allSorted ? C_BRAND : (Color){ 170, 176, 186, 255 };
        DrawRectangleRounded(d, 0.4f, 8, dc);
        const char *dt = allSorted ? "Send them through (ENTER)" : "Sort all three first";
        DrawText(dt, (int)(d.x + d.width / 2) - MeasureText(dt, 18) / 2, (int)d.y + 11, 18, WHITE);
        gameui_give_up_hint();
    } else {
        // The result: each call next to the right one
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 20, 30, 45, 170 });
        Rectangle box = { 150, 130, 700, 330 };
        DrawRectangleRounded(box, 0.08f, 10, (Color){ 245, 247, 250, 255 });
        int right = 0;
        for (int i = 0; i < ARRIVALS; i++) if (call[i] == realSeverity[i]) right++;
        const char *head = gaveUp ? "THE TRIAGE NURSE SORTED THEM"
                         : right == ARRIVALS ? "ALL THREE RIGHT" : TextFormat("%d OF 3 RIGHT", right);
        gameui_centre_text(head, 150, 30, gaveUp ? (Color){ 60, 70, 90, 255 } : right == ARRIVALS ? C_ROUTINE : C_EMERGENCY);
        if (outOfTime) gameui_centre_text("Out of time: anyone not sorted went in as routine.", 186, 16, (Color){ 100, 108, 122, 255 });
        for (int i = 0; i < ARRIVALS; i++) {
            int y = 220 + i * 62;
            Color ink = { 30, 36, 48, 255 };
            DrawText(TextFormat("Patient %d: %s", ids[i], looks[i]), 180, y, 16, ink);
            if (gaveUp) {
                DrawText(TextFormat("Sorted as %s", PRIORITY[realSeverity[i]]), 180, y + 22, 16, priority_colour(realSeverity[i]));
                continue;
            }
            DrawText(TextFormat("You: %s", PRIORITY[call[i]]), 180, y + 22, 16, priority_colour(call[i]));
            const char *verdict;
            if (call[i] == realSeverity[i])      verdict = "Right";
            else if (call[i] > realSeverity[i])  verdict = TextFormat("Should be %s: seen too late", PRIORITY[realSeverity[i]]);
            else                                 verdict = TextFormat("Should be %s: takes a bed too soon", PRIORITY[realSeverity[i]]);
            DrawText(verdict, 400, y + 22, 16, call[i] == realSeverity[i] ? C_ROUTINE : C_EMERGENCY);
        }
        if (resultDelay <= 0) gameui_centre_text("They're in the waiting room now. Click or press ENTER to carry on", 420, 16,
                                                  (Color){ 60, 70, 90, 255 });
    }
    EndMode2D();
}
