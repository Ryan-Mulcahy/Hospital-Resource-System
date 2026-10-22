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
//                 children's ward, physio, dialysis, waiting area, cafe) and the lifts
//   outside3d.c/.h the outside of the building, the car park and the grounds
//   sound.c/.h    sound effects, made in code (no sound files needed)
//   minigames.c/.h the mini-game menu (G), and the eight games:
//   opgame.c/.h   the operation (O)
//   defibgame.c/.h "Shock!", the defibrillator
//   spingame.c/.h "Balance the centrifuge", spinning the blood samples
//   scangame.c/.h "Hold still", X-ray and CT
//   triagegame.c/.h "Triage call", sorting new arrivals
//   pharmgame.c/.h "Pharmacy rush", making up a prescription
//   drivegame.c/.h "Blue lights", driving the ambulance in
//   heligame.c/.h "Touchdown", landing the rescue helicopter
//   gameui.c/.h   drawing the games share (the top bar, the result box)
//   main.c        window, main loop and keyboard input
//
// A run covers one day: the day shift (07:00 to 19:00), then the night shift
// (19:00 to 07:00). A report is shown at the end of each shift. Patients still
// in the department at 19:00 are handed over to the night shift.
//
// Controls: SPACE pause | UP/DOWN speed | E spawn emergency | R restart the day
//           H helicopter rescue | G mini games | M sound on/off
//           Straight into a game: O operation, S shock, X hold still (X-ray
//           and CT), P pharmacy rush
//           F11 full screen on/off (the window has a title bar to minimise it;
//           in full screen, F11 first)
//           N continue to the next shift, L view patients treated,
//           E equipment and rooms (report screen)
//           wheel or UP/DOWN scroll (patients-treated page); on those two
//           pages, L or E again (or B) goes back to the report
//           3D view: drag to turn, right-drag to move, scroll to zoom,
//           0 whole floor, 1-7 rooms, 8 A&E, 9 helipad and ambulance bay,
//           T cinematic tour, click a patient to follow them,
//           F or PAGE UP/DOWN (or the floor buttons) to change floor or see the outside
//           Mini games menu: 1-8 or click to play, G or BACKSPACE back.
//           Operation: click and hold the item, drag it to the tray
//           without touching the edge. Shock!: C charge, SPACE shock as a
//           spike passes the zone. Centrifuge: click the slots to load the
//           tubes so the rotor balances, then SPACE to spin.
//           Hold still: frame on the ring, click when still.
//           Triage call: drag each arrival to a box, ENTER to send them.
//           Pharmacy rush: click the tubs. Blue lights: LEFT/RIGHT steer,
//           DOWN brake. Touchdown: LEFT/RIGHT, hold UP to hover, DOWN to
//           drop. In every game: Q give up, R try again (practice)
 
#include <math.h>
#include "raylib.h"
#include "sim.h"
#include "draw.h"
#include "report.h"
#include "roster.h"
#include "view3d.h"
#include "conditions.h"
#include "sound.h"
#include "minigames.h"
 
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
    // Smooth edges in 3D, and a window that can be any size. It starts hidden,
    // so the small starting size is never seen: it's made to fill the screen
    // first (as a normal window, with its title bar, so it can be minimised),
    // and only shown once it has been drawn at its full size.
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN);
    InitWindow(SCREEN_W, SCREEN_H, "Hospital Resource Simulator");
    SetWindowMinSize(SCREEN_W / 2, SCREEN_H / 2);
    MaximizeWindow();                    // fill the screen; F11 switches to full screen and back
    SetTargetFPS(60);
    for (int i = 0; i < 3; i++) {        // let the new size take effect before showing the window
        BeginDrawing();
        ClearBackground(C_BG);
        EndDrawing();
    }
    view3d_prepare();                    // the 3D view's signs and screens, made now so V is instant
    ClearWindowState(FLAG_WINDOW_HIDDEN);
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
        // (The mini games are 2D screens, even from the 3D view.)
        bool operating = !showRoster && !sim.shiftOver && minigames_active();
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
 
            // L (the key that opened it), B or BACKSPACE, or the button
            if (IsKeyPressed(KEY_L) || IsKeyPressed(KEY_B) || IsKeyPressed(KEY_BACKSPACE) || report_log_back_clicked()) {
                viewingLog = false;
            }
        } else if (sim.shiftOver && viewingKit) {
            // Equipment and rooms page: go back to the report
            // E (the key that opened it), B or BACKSPACE, or the button
            if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_B) || IsKeyPressed(KEY_BACKSPACE) || report_equipment_back_clicked()) {
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
            // The mini-game menu or a game: the simulation waits until it's closed
            minigames_update(&sim);
        } else {
            // Input
            if (IsKeyPressed(KEY_G)) minigames_open_menu();    // the mini-game menu
            // Quick keys for four games (the patient who's ready, or practice):
            // O operate, S shock, X X-ray/CT (hold still), P pharmacy rush
            if (IsKeyPressed(KEY_O)) minigames_start(&sim, MINIGAME_OPERATION);
            if (IsKeyPressed(KEY_S)) minigames_start(&sim, MINIGAME_SHOCK);
            if (IsKeyPressed(KEY_X)) minigames_start(&sim, MINIGAME_SCAN);
            if (IsKeyPressed(KEY_P)) minigames_start(&sim, MINIGAME_PHARMACY);
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
            minigames_watch(&sim);       // someone ready for a game: offer it
            sound_update(&sim, paused);  // siren and monitor alarm
        }

        // The helicopter and rain sounds belong to the 3D view (and the
        // helicopter's to the landing game)
        if (!in3d) {
            sound_loop(SND_ROTOR, minigames_rotor(), 0.5f);
            sound_loop(SND_RAIN, false, 0.0f);
        }
 
        // Draw
        BeginDrawing();
        if (!showRoster && !sim.shiftOver && minigames_active()) {
            minigames_draw(&sim, fit_screen());          // the mini-game menu or a game, scaled to fit
        } else if (!showRoster && !sim.shiftOver && view3d) {
            view3d_draw(&sim, speed, paused);            // the whole window
        } else {
            fit = fit_screen();
            BeginMode2D(fit);                            // 2D screens, scaled to fit
            if (showRoster)                  draw_roster_screen(sim.day, sim.shift);
            else if (sim.shiftOver && viewingLog) draw_patient_log(&sim, logScroll);
            else if (sim.shiftOver && viewingKit) draw_equipment_report(&sim);
            else if (sim.shiftOver)          draw_shift_report(&sim);
            else draw_frame(&sim, speed, paused);   // (with the games ready to play in a corner)
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