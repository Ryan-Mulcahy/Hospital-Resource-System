// minigames.h - the mini-game menu, the eight games together, and showing
// which patients are ready for a game.
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

// ---- Showing which patients are ready for a game ----
// Rather than banners in the middle of the screen: a little table in a
// corner listing them all, a badge on the room's (or bed's) card in the 2D
// view, and a speech bubble over the room in the 3D view.

// One game that's ready for a real patient.
typedef struct {
    MiniGame game;
    int patientId;
} MiniOffer;

// The games ready for a real patient now (Triage and Blue lights are left
// out: they're always there). Fills `out` (up to `max`) and returns how many.
int minigames_offers(MiniOffer *out, int max);

// Where a patient is: an A&E bed (*bed), or a place in a room (*room and
// *slot). The others are set to -1. False if they're in neither (a rescue
// still in the air, say).
bool minigames_patient_place(const Sim *sim, int id, int *room, int *slot, int *bed);

// A game's colour.
Color minigames_colour(MiniGame g);

// A tiny picture for a game (a scalpel, a lightning bolt...), about 14
// pixels across, centred on (cx, cy).
void minigames_draw_mini_icon(MiniGame g, float cx, float cy, Color c);

// A round badge with the game's picture, pulsing gently: for a card in the 2D view.
void minigames_draw_badge(MiniGame g, float cx, float cy);

// A speech bubble ("SURGERY 38  O") whose tail points down at `tip`: for a
// room in the 3D view. The bubble slides left if needed so its right edge
// stays left of maxRight (clear of the panels on the right).
void minigames_draw_bubble(MiniGame g, int patientId, Vector2 tip, float maxRight);

// The "GAMES READY" table, top-left corner at (x, y), `w` wide, with the
// games in `columns` columns, in these colours. Returns its height (0 if not
// drawn, while the menu or a game is on screen).
int minigames_draw_table(float x, float y, float w, int columns, Color bg, Color ink, Color muted);

#endif
