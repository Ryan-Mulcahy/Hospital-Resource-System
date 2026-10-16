// Hospital Resource Simulator
// C + raylib. Patients arrive over time, wait in a severity-ordered queue,
// and are admitted to a bed whenever one is free.
//
// Files:
//   patient.c/.h  patient record and waiting queue
//   sim.c/.h      simulation state: arrivals, treatment, admission, nurses
//                 and the shift clock
//   draw.c/.h     everything drawn on screen during a shift
//   report.c/.h   the end-of-shift report screen
//   main.c        window, main loop and keyboard input
//
// A run covers one day: the day shift (07:00 to 19:00), then the night shift
// (19:00 to 07:00). A report is shown at the end of each shift. Patients still
// in the department at 19:00 are handed over to the night shift.
//
// Controls: SPACE pause | UP/DOWN speed | E spawn emergency | R restart the day
//           N continue to the next shift, L view patients treated (report screen)
//           wheel or UP/DOWN scroll, B go back (patients-treated page)
 
#include "raylib.h"
#include "sim.h"
#include "draw.h"
#include "report.h"
#include "roster.h"
#include "conditions.h"
 
int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Hospital Resource Simulator");
    SetTargetFPS(60);
 
    Image logo = LoadImage("logo.png");
    
    SetWindowIcon(logo); // works cause image background transparent
    UnloadImage(logo);   // the window keeps its own copy of the icon
 
    conditions_load("conditions.csv");   // before any patient arrives
 
    Sim sim;
    sim_init(&sim, 1);   // day 1
 
    bool paused = false;
    bool showRoster = true;  // roster screen before the shift starts
    float speed = 1.0f;
    bool viewingLog = false;   // on the report screen: showing the patients-treated page
    int logScroll = 0;         // first row shown on that page
 
    while (!WindowShouldClose()) {
        if (showRoster) {
            // Roster screen: wait for the shift to be started
            if (IsKeyPressed(KEY_N) || roster_start_clicked()) showRoster = false;
        } else if (sim.shiftOver && viewingLog) {
            // Treatment log page: scroll, or go back to the report
            int wheel = (int)GetMouseWheelMove();
            logScroll -= wheel * 3;
            if (IsKeyPressed(KEY_DOWN))      logScroll += 1;
            if (IsKeyPressed(KEY_UP))        logScroll -= 1;
            if (IsKeyPressed(KEY_PAGE_DOWN)) logScroll += 15;
            if (IsKeyPressed(KEY_PAGE_UP))   logScroll -= 15;
 
            int maxScroll = report_log_max_scroll(&sim);
            if (logScroll > maxScroll) logScroll = maxScroll;
            if (logScroll < 0)         logScroll = 0;
 
            if (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_BACKSPACE) || report_log_back_clicked()) {
                viewingLog = false;
            }
        } else if (sim.shiftOver) {
            // Report screen: view the log, or start the next shift
            if (IsKeyPressed(KEY_L) || report_view_log_clicked()) {
                viewingLog = true;
                logScroll = 0;
            } else if (IsKeyPressed(KEY_N) || report_new_shift_clicked()) {
                if (sim.shift == SHIFT_DAY) {
                    sim_start_night(&sim);       // same patients, night staff
                } else {
                    int next = sim.day + 1;      // the day is over: start fresh
                    sim_free(&sim);
                    sim_init(&sim, next);
                }
                paused = false;
                showRoster = true;           // show who's on before the shift starts
            }
        } else {
            // Input
            if (IsKeyPressed(KEY_SPACE)) paused = !paused;
            if (IsKeyPressed(KEY_UP) && speed < 8.0f) speed *= 2.0f;
            if (IsKeyPressed(KEY_DOWN) && speed > 0.5f) speed /= 2.0f;
            if (IsKeyPressed(KEY_E)) sim_add_patient(&sim, 1);
            if (IsKeyPressed(KEY_R)) {
                int day = sim.day;
                sim_free(&sim);
                sim_init(&sim, day);
            }
 
            // Update
            if (!paused) sim_update(&sim, GetFrameTime() * speed);
        }
 
        // Draw
        BeginDrawing();
        if (showRoster)                  draw_roster_screen(sim.day, sim.shift);
        else if (sim.shiftOver && viewingLog) draw_patient_log(&sim, logScroll);
        else if (sim.shiftOver)          draw_shift_report(&sim);
        else                             draw_frame(&sim, speed, paused);
        EndDrawing();
    }
 
    sim_free(&sim);
    CloseWindow();
    return 0;
}