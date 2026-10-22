// minigames.h - the mini-game menu, and the three games together.
//
// Press G during a shift for the menu of eight games: the operation, "Shock!"
// (the defibrillator), "Find the vein" (bloods), "Hold still" (X-ray and CT),
// "Triage call", "Pharmacy rush", "Blue lights" (the ambulance) and
// "Touchdown" (the rescue helicopter). Each card says whether a real patient
// is ready for that game or it would be a practice round, and how you've done
// this shift. Triage and the ambulance are always real: they bring new
// patients in. Pick one with 1-8 or a click; after the game you're back at
// the menu. G or BACKSPACE goes back to the hospital.
//
// O still starts the operation straight away, as before. While the menu or a
// game is on screen, the simulation waits.

#ifndef MINIGAMES_H
#define MINIGAMES_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

typedef enum {
    MINIGAME_OPERATION,      // 1 the operation (theatre)
    MINIGAME_SHOCK,          // 2 Shock! (the defibrillator, in A&E)
    MINIGAME_VEIN,           // 3 Find the vein (bloods)
    MINIGAME_SCAN,           // 4 Hold still (X-ray and CT)
    MINIGAME_TRIAGE,         // 5 Triage call (three new arrivals)
    MINIGAME_PHARMACY,       // 6 Pharmacy rush
    MINIGAME_DRIVE,          // 7 Blue lights (the ambulance)
    MINIGAME_LANDING,        // 8 Touchdown (the rescue helicopter)
    MINIGAME_COUNT
} MiniGame;

// Call every frame during a shift: notices patients each game could be played for.
void minigames_watch(const Sim *sim);

// Open the menu (G).
void minigames_open_menu(void);

// Go straight into one game, without the menu (O for the operation).
void minigames_start(Sim *sim, MiniGame game);

// Should the helicopter's rotor sound play? (While the landing game's flying.)
bool minigames_rotor(void);

// Is the menu or a game on screen? (The simulation doesn't run while it is.)
bool minigames_active(void);

// Input for one frame: the menu, or the game being played.
void minigames_update(Sim *sim);

// Draw the menu or the game, laid out for 1000x600 and scaled by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void minigames_draw(const Sim *sim, Camera2D fit);

// The banners for patients who are ready for a game ("SURGERY READY..."),
// centred on centreX with their top at y. In a row (short, side by side: the
// 2D view, where there's little room) or one under another (the 3D view).
void minigames_draw_offers(int centreX, int y, bool row);

#endif
