// defibgame.h - "Shock!": the defibrillator mini-game.
//
// A patient's heart has gone into a chaotic rhythm. Their trace scrolls
// across the monitor; charge the defibrillator (C), then shock (SPACE) just
// as one of the tall spikes passes through the marked zone. Three good
// shocks bring the heart back into a normal rhythm. Shock at the wrong
// moment three times, or run out of time, and you lose.
//
// It's offered for a real patient who has the defibrillator by their A&E bed
// (cardiac arrests and heart attacks). Win and the rest of their treatment
// takes half as long, so the defibrillator is free sooner; lose and it takes
// longer. With nobody to shock, it's a practice round that changes nothing.

#ifndef DEFIBGAME_H
#define DEFIBGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices a patient who has the defibrillator.
void defibgame_watch(const Sim *sim);

// The patient who could be shocked, or 0 if nobody.
int defibgame_offer(void);

// Start a game: for that patient if there is one, or a practice round.
void defibgame_start(const Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool defibgame_active(void);

// Input and timing for one frame; applies the result to a real patient.
void defibgame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void defibgame_draw(Camera2D fit);

#endif
