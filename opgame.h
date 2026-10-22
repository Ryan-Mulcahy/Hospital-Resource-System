// opgame.h - the operation mini-game: a steady-hand test.
//
// Grab the item with the tweezers and drag it along the channel to the tray
// without touching the edge. Touch the edge, or run out of time, and you lose.
//
// When a real patient gets to theatre, a "SURGERY READY" banner offers the
// game (press O). The simulation waits while you play: win and the patient's
// surgery is quicker; lose and there's a complication and it takes longer.
// Pressing O with nobody waiting is a practice round that changes nothing.

#ifndef OPGAME_H
#define OPGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices when a patient arrives in theatre.
void opgame_watch(const Sim *sim);

// Start a game: for the patient in theatre if there is one waiting, or a
// practice round if not.
void opgame_start(const Sim *sim);

// Is a game on screen? (The simulation doesn't run while it is.)
bool opgame_active(void);

// Input and timing for one frame. Applies the result to the simulation when
// a real patient's game ends.
void opgame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`
// (the same camera as the other 2D screens). Call between BeginDrawing() and
// EndDrawing(), not inside BeginMode2D().
void opgame_draw(Camera2D fit);

// The "SURGERY READY - press O" banner, while a patient is waiting in theatre.
// Centred on centreX with its top at y.
void opgame_draw_offer(int centreX, int y);

#endif
