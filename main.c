// Hospital Resource Simulator
// C + raylib. Patients arrive over time, wait in a severity-ordered queue,
// and are admitted to a bed whenever one is free.
//
// Files:
//   patient.c/.h  patient record and waiting queue
//   sim.c/.h      simulation state: arrivals, treatment, admission, nurses,
//                 the rooms after A&E, helicopter rescues and the shift clock
//   draw.c/.h     everything drawn on screen during a shift
//   report.c/.h   the end-of-shift report screen
//   view3d.c/.h   the 3D view of A&E (V switches between 2D and 3D)
//   rooms3d.c/.h  the rest of the ground floor in 3D: ward, X-ray, MRI, bloods
//                 and weight, operating theatre, nursery and admin office
//   floors3d.c/.h the first and second floors in 3D (CT, ultrasound,
//                 cardiology, ICU, pharmacy, lab, staff room; maternity,
//                 children's ward, physio, dialysis, chapel, cafe) and the lifts
//   outside3d.c/.h the outside of the building, the car park and the grounds
//   sound.c/.h    sound effects, made in code (no sound files needed)
//   opgame.c/.h   the operation mini-game (O)
//   main.c        window, main loop and keyboard input
//
// A run covers one day: the day shift (07:00 to 19:00), then the night shift
// (19:00 to 07:00). A report is shown at the end of each shift. Patients still
// in the department at 19:00 are handed over to the night shift.
//
// Controls: SPACE pause | UP/DOWN speed | E spawn emergency | R restart the day
//           H helicopter rescue | O operate (the mini-game) | M sound on/off
//           F11 full screen on/off
//           N continue to the next shift, L view patients treated,
//           E equipment and rooms (report screen)
//           wheel or UP/DOWN scroll, B go back (patients-treated page)
//           3D view: drag to turn, right-drag to move, scroll to zoom,
//           0 A&E, 1-7 rooms, 8 whole hospital, 9 helipad and ambulance bay,
//           T cinematic tour, click a patient to follow them,
//           F or PAGE UP/DOWN (or the floor buttons) to change floor or see the outside
//           Operation game: click and hold the item, drag it to the tray
//           without touching the edge. Q give up, R try again (practice)
 
#include <math.h>
#include "raylib.h"
#include "sim.h"
#include "draw.h"
#include "report.h"
#include "roster.h"
#include "view3d.h"
#include "conditions.h"
#include "sound.h"
#include "opgame.h"
 
// The 2D screens are laid out for SCREEN_W x SCREEN_H. In a bigger window
// they're scaled up to fit, centred, with the mouse scaled to match so the
// buttons still work. The 3D view uses the whole window as it is.
static Camera2D fit_screen(void)
{
    float scale = fminf((float)GetScreenWidth() / SCREEN_W, (float)GetScreenHeight() / SCREEN_H);
    Camera2D cam = { 0 };
    cam.zoom = scale;
    cam.offset = (Vector2){ (GetScreenWidth() - SCREEN_W * scale) / 2, (GetScreenHeight() - SCREEN_H * scale) / 2 };
    return cam;
}

int main(void)
{
    // Smooth edges in 3D, and a window that can be any size
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Hospital Resource Simulator");
    SetWindowMinSize(SCREEN_W / 2, SCREEN_H / 2);
    ToggleBorderlessWindowed();          // start full screen; F11 switches back to a window
    SetTargetFPS(60);
    sound_init();                        // after the window; quietly does nothing without a sound device
 
    Image logo = LoadImage("logo.png");
    
    SetWindowIcon(logo); // works cause image background transparent
    UnloadImage(logo);   // the window keeps its own copy of the icon
 
    conditions_load("conditions.csv");   // before any patient arrives
 
    Sim sim;
    sim_init(&sim, 1);   // day 1
 
    bool paused = false;
    bool showRoster = true;  // roster screen before the shift starts
    bool view3d = false;     // V switches between the 2D and 3D views
    float speed = 1.0f;
    bool viewingLog = false;   // on the report screen: showing the patients-treated page
    bool viewingKit = false;   // on the report screen: showing the equipment and rooms page
    int logScroll = 0;         // first row shown on that page
 
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_M)) sound_toggle_mute();

        // The 2D screens are scaled to fit the window; the mouse has to be too.
        // (The operation game is a 2D screen, even from the 3D view.)
        bool operating = !showRoster && !sim.shiftOver && opgame_active();
        bool in3d = !showRoster && !sim.shiftOver && view3d && !operating;
        Camera2D fit = fit_screen();
        if (in3d) {
            SetMouseOffset(0, 0);
            SetMouseScale(1.0f, 1.0f);
        } else {
            SetMouseOffset((int)-fit.offset.x, (int)-fit.offset.y);
            SetMouseScale(1.0f / fit.zoom, 1.0f / fit.zoom);
        }

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
        } else if (sim.shiftOver && viewingKit) {
            // Equipment and rooms page: go back to the report
            if (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_BACKSPACE) || report_equipment_back_clicked()) {
                viewingKit = false;
            }
        } else if (sim.shiftOver) {
            // Report screen: view the log or the equipment page, or start the next shift
            if (IsKeyPressed(KEY_L) || report_view_log_clicked()) {
                viewingLog = true;
                logScroll = 0;
            } else if (IsKeyPressed(KEY_E) || report_view_equipment_clicked()) {
                viewingKit = true;
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
        } else if (operating) {
            // The operation game: the simulation waits until it's over
            opgame_update(&sim);
        } else {
            // Input
            if (IsKeyPressed(KEY_O)) opgame_start(&sim);       // operate: the patient in theatre, or practice
            if (IsKeyPressed(KEY_SPACE)) paused = !paused;
            if (IsKeyPressed(KEY_V)) view3d = !view3d;
            if (view3d) view3d_update_camera();
            if (IsKeyPressed(KEY_UP) && speed < 8.0f) speed *= 2.0f;
            if (IsKeyPressed(KEY_DOWN) && speed > 0.5f) speed /= 2.0f;
            if (IsKeyPressed(KEY_E)) sim_add_patient(&sim, 1);
            if (IsKeyPressed(KEY_H)) sim_add_rescue(&sim);     // search and rescue, by helicopter
            if (IsKeyPressed(KEY_R)) {
                int day = sim.day;
                sim_free(&sim);
                sim_init(&sim, day);
            }
 
            // Update
            if (!paused) sim_update(&sim, GetFrameTime() * speed);
            opgame_watch(&sim);          // someone arrived in theatre: offer the game
            sound_update(&sim, paused);  // siren and monitor alarm
        }

        // The helicopter and rain sounds belong to the 3D view
        if (!in3d) {
            sound_loop(SND_ROTOR, false, 0.0f);
            sound_loop(SND_RAIN, false, 0.0f);
        }
 
        // Draw
        BeginDrawing();
        if (!showRoster && !sim.shiftOver && opgame_active()) {
            opgame_draw(fit_screen());                   // the operation game, scaled to fit
        } else if (!showRoster && !sim.shiftOver && view3d) {
            view3d_draw(&sim, speed, paused);            // the whole window
        } else {
            fit = fit_screen();
            BeginMode2D(fit);                            // 2D screens, scaled to fit
            if (showRoster)                  draw_roster_screen(sim.day, sim.shift);
            else if (sim.shiftOver && viewingLog) draw_patient_log(&sim, logScroll);
            else if (sim.shiftOver && viewingKit) draw_equipment_report(&sim);
            else if (sim.shiftOver)          draw_shift_report(&sim);
            else {
                draw_frame(&sim, speed, paused);
                opgame_draw_offer(SCREEN_W / 2, SCREEN_H - 76);   // "SURGERY READY - press O"
            }
            EndMode2D();
        }
        EndDrawing();
    }
 
    sim_free(&sim);
    view3d_close();
    sound_close();
    CloseWindow();
    return 0;
}