// Hospital Resource Simulator
// C + raylib. Patients arrive over time, wait in a severity-ordered queue,
// and are admitted to a bed whenever one is free.
//
// Files:
//   patient.c/.h  patient record and waiting queue
//   doctor.c/.h   doctor treatment and bed management
//   sim.c/.h      simulation state: arrivals, treatment, admission
//   draw.c/.h     everything drawn on screen
//   main.c        window, main loop and keyboard input
//
// Controls: SPACE pause | UP/DOWN speed | E spawn emergency | R reset

#include "raylib.h"
#include "sim.h"
#include "draw.h"

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
        draw_frame(&sim, speed, paused);
        EndDrawing();
    }

    sim_free(&sim);
    CloseWindow();
    return 0;
}