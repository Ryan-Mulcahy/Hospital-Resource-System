// pharmgame.h - "Pharmacy rush": make up a prescription against the clock.
//
// A prescription comes up ("2 x blue tablet, 1 x white capsule") and you
// click the right pills off the shelf into the bag. Watch out: tablets and
// capsules come in the same colours.
//
// It's offered for a real patient who has just got to the pharmacy. The
// right pills quickly halves the rest of their visit; the right pills slowly
// changes nothing; a wrong pill sends them back to the pharmacy's queue; and
// running out of time makes their visit longer. With nobody there, it's a
// practice round that changes nothing.

#ifndef PHARMGAME_H
#define PHARMGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices a patient arriving at the pharmacy.
void pharmgame_watch(const Sim *sim);

// The patient waiting for their prescription, or 0 if nobody.
int pharmgame_offer(void);

// Start a game: for that patient if there is one, or a practice round.
void pharmgame_start(const Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool pharmgame_active(void);

// Input and timing for one frame; applies the result to a real patient.
void pharmgame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void pharmgame_draw(Camera2D fit);

#endif
