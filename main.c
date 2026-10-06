// Hospital Resource Simulator - starter
// C + raylib. Patients arrive over time, wait in a severity-ordered queue,
// and are admitted to a bed whenever one is free.
//
// Controls: SPACE pause | UP/DOWN speed | E spawn emergency | R reset

#include "raylib.h"
#include <stdlib.h>

#define SCREEN_W   1000
#define SCREEN_H   600
#define BED_COUNT  6

// ---------------------------------------------------------------- data

typedef struct Patient {
    int id;
    int severity;            // 1 = emergency, 2 = urgent, 3 = routine
    float arrivalTime;       // simulation clock when they arrived
    float treatmentTotal;    // seconds of treatment needed
    float treatmentLeft;     // seconds of treatment remaining
    struct Patient *next;    // next patient in the waiting queue
} Patient;

typedef struct {
    Patient *occupant;       // NULL when the bed is free
} Bed;

typedef struct {
    Patient *waiting;        // head of the waiting queue (most urgent first)
    int waitingCount;
    Bed beds[BED_COUNT];
    float clock;             // simulated seconds since start
    float nextArrival;       // clock time of the next random arrival
    int nextId;
    int admitted;            // patients who have been given a bed
    int treated;             // patients discharged
    float totalWait;         // sum of waiting times of admitted patients
} Sim;

// ---------------------------------------------------------------- queue

// Insert behind everyone of the same or higher urgency,
// so ties are served in arrival order.
static void queue_insert(Patient **head, Patient *p)
{
    while (*head != NULL && (*head)->severity <= p->severity) {
        head = &(*head)->next;
    }
    p->next = *head;
    *head = p;
}

// Remove and return the most urgent patient, or NULL if nobody is waiting.
static Patient *queue_pop(Patient **head)
{
    Patient *p = *head;
    if (p != NULL) {
        *head = p->next;
        p->next = NULL;
    }
    return p;
}

// ---------------------------------------------------------------- simulation

static float random_arrival_gap(void)
{
    return (float)GetRandomValue(3, 15) / 10.0f;      // 0.3 to 1.5 seconds
}

static void sim_add_patient(Sim *sim, int severity)
{
    Patient *p = malloc(sizeof(Patient));
    if (p == NULL) return;

    float base = (severity == 1) ? 8.0f : (severity == 2) ? 5.0f : 3.0f;

    p->id = sim->nextId++;
    p->severity = severity;
    p->arrivalTime = sim->clock;
    p->treatmentTotal = base + (float)GetRandomValue(-10, 10) / 10.0f;
    p->treatmentLeft = p->treatmentTotal;
    p->next = NULL;

    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
}

static int random_severity(void)
{
    int roll = GetRandomValue(1, 10);
    if (roll <= 2) return 1;      // 20% emergency
    if (roll <= 5) return 2;      // 30% urgent
    return 3;                     // 50% routine
}

static void sim_free(Sim *sim)
{
    Patient *p;
    while ((p = queue_pop(&sim->waiting)) != NULL) free(p);
    for (int i = 0; i < BED_COUNT; i++) {
        free(sim->beds[i].occupant);
        sim->beds[i].occupant = NULL;
    }
}

static void sim_init(Sim *sim)
{
    *sim = (Sim){ 0 };
    sim->nextId = 1;
    sim->nextArrival = random_arrival_gap();
}

static void sim_update(Sim *sim, float dt)
{
    sim->clock += dt;

    // 1. Arrivals
    while (sim->clock >= sim->nextArrival) {
        sim_add_patient(sim, random_severity());
        sim->nextArrival += random_arrival_gap();
    }

    // 2. Treatment: tick each occupied bed, discharge when finished
    for (int i = 0; i < BED_COUNT; i++) {
        Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;

        p->treatmentLeft -= dt;
        if (p->treatmentLeft <= 0.0f) {
            free(p);
            sim->beds[i].occupant = NULL;
            sim->treated++;
        }
    }

    // 3. Admission: every free bed takes the most urgent waiting patient
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) continue;

        Patient *p = queue_pop(&sim->waiting);
        if (p == NULL) break;

        sim->beds[i].occupant = p;
        sim->waitingCount--;
        sim->admitted++;
        sim->totalWait += sim->clock - p->arrivalTime;
    }
}

// ---------------------------------------------------------------- drawing

static Color severity_color(int severity)
{
    if (severity == 1) return RED;
    if (severity == 2) return ORANGE;
    return GREEN;
}

static void draw_waiting_room(const Sim *sim)
{
    const int x0 = 40, y0 = 110, step = 34, perRow = 16, maxShown = 48;

    DrawText("WAITING ROOM", x0, 60, 20, DARKGRAY);
    DrawRectangleLines(x0 - 15, y0 - 25, perRow * step + 15, 3 * step + 15, LIGHTGRAY);

    int i = 0;
    for (const Patient *p = sim->waiting; p != NULL && i < maxShown; p = p->next, i++) {
        int cx = x0 + (i % perRow) * step;
        int cy = y0 + (i / perRow) * step;
        DrawCircle(cx, cy, 13, severity_color(p->severity));
        DrawText(TextFormat("%d", p->id), cx - 8, cy - 5, 10, WHITE);
    }

    if (sim->waitingCount > maxShown) {
        DrawText(TextFormat("+%d more", sim->waitingCount - maxShown),
                 x0, y0 + 3 * step, 16, MAROON);
    }
}

static void draw_beds(const Sim *sim)
{
    const int x0 = 40, y0 = 300, w = 80, h = 130, gap = 16;

    DrawText("WARD", x0, y0 - 35, 20, DARKGRAY);

    for (int i = 0; i < BED_COUNT; i++) {
        int x = x0 + i * (w + gap);
        const Patient *p = sim->beds[i].occupant;

        DrawRectangle(x, y0, w, h, (Color){ 235, 240, 246, 255 });
        DrawRectangleLines(x, y0, w, h, GRAY);
        DrawText(TextFormat("Bed %d", i + 1), x + 8, y0 + 8, 14, DARKGRAY);

        if (p == NULL) {
            DrawText("free", x + 24, y0 + 60, 14, LIGHTGRAY);
            continue;
        }

        DrawCircle(x + w / 2, y0 + 62, 20, severity_color(p->severity));
        DrawText(TextFormat("%d", p->id), x + w / 2 - 8, y0 + 56, 12, WHITE);

        // Treatment progress bar
        float done = 1.0f - p->treatmentLeft / p->treatmentTotal;
        DrawRectangle(x + 8, y0 + h - 20, w - 16, 10, LIGHTGRAY);
        DrawRectangle(x + 8, y0 + h - 20, (int)((w - 16) * done), 10, DARKBLUE);
    }
}

static void draw_stats(const Sim *sim, float speed, bool paused)
{
    const int x = 680, y = 60, line = 30;
    int inBeds = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) inBeds++;
    }
    float avgWait = (sim->admitted > 0) ? sim->totalWait / (float)sim->admitted : 0.0f;

    DrawRectangle(x - 20, y - 20, 300, 400, (Color){ 245, 247, 250, 255 });
    DrawRectangleLines(x - 20, y - 20, 300, 400, LIGHTGRAY);

    DrawText("STATISTICS", x, y, 20, DARKGRAY);
    DrawText(TextFormat("Clock:      %.0f s", sim->clock),            x, y + 1 * line + 10, 18, BLACK);
    DrawText(TextFormat("Waiting:    %d", sim->waitingCount),         x, y + 2 * line + 10, 18, BLACK);
    DrawText(TextFormat("Beds used:  %d / %d", inBeds, BED_COUNT),    x, y + 3 * line + 10, 18, BLACK);
    DrawText(TextFormat("Treated:    %d", sim->treated),              x, y + 4 * line + 10, 18, BLACK);
    DrawText(TextFormat("Avg wait:   %.1f s", avgWait),               x, y + 5 * line + 10, 18, BLACK);
    DrawText(TextFormat("Speed:      %.1fx", speed),                  x, y + 6 * line + 10, 18, BLACK);

    DrawCircle(x + 8, y + 265, 8, RED);     DrawText("Emergency", x + 24, y + 258, 16, DARKGRAY);
    DrawCircle(x + 8, y + 290, 8, ORANGE);  DrawText("Urgent",    x + 24, y + 283, 16, DARKGRAY);
    DrawCircle(x + 8, y + 315, 8, GREEN);   DrawText("Routine",   x + 24, y + 308, 16, DARKGRAY);

    if (paused) DrawText("PAUSED", x, y + 345, 22, MAROON);
}

// ---------------------------------------------------------------- main

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Hospital Resource Simulator");
    SetTargetFPS(60);

    Sim sim;
    sim_init(&sim);

    bool paused = false;
    float speed = 1.0f;

    while (!WindowShouldClose()) {
        // Input
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_UP) && speed < 8.0f) speed *= 2.0f;
        if (IsKeyPressed(KEY_DOWN) && speed > 0.5f) speed /= 2.0f;
        if (IsKeyPressed(KEY_E)) sim_add_patient(&sim, 1);
        if (IsKeyPressed(KEY_R)) { sim_free(&sim); sim_init(&sim); }

        // Update
        if (!paused) sim_update(&sim, GetFrameTime() * speed);

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText("Hospital Resource Simulator", 40, 18, 26, DARKBLUE);
        draw_waiting_room(&sim);
        draw_beds(&sim);
        draw_stats(&sim, speed, paused);
        DrawText("SPACE pause   UP/DOWN speed   E emergency   R reset",
                 40, SCREEN_H - 40, 16, GRAY);

        EndDrawing();
    }

    sim_free(&sim);
    CloseWindow();
    return 0;
}