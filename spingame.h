// spingame.h - "Balance the centrifuge": the bloods mini-game.
//
// The patient's blood tubes (and some from other patients) have to be spun
// in the lab's centrifuge before they can be tested. Click the slots in the
// rotor to load the tubes, then spin it (SPACE or the Spin button). As in a
// real lab, the rotor has to be balanced: every tube needs another one
// straight across from it, or three tubes evenly spaced round the rotor.
// Unbalanced, it wobbles and stops, and you rearrange and try again (you get
// two spins). Some slots are cracked and can't be used.
//
// It's offered for a real patient who has just sat down in bloods. Balanced
// first time (or second) halves the rest of their time there; failing, or
// running out of time, means the samples are re-run, so they're there longer.
// With nobody in bloods, it's a practice round that changes nothing.

#ifndef SPINGAME_H
#define SPINGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices a patient arriving in bloods.
void spingame_watch(const Sim *sim);

// The patient whose bloods could be spun, or 0 if nobody.
int spingame_offer(void);

// Start a game: for that patient if there is one, or a practice round.
void spingame_start(const Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool spingame_active(void);

// Input and timing for one frame; applies the result to a real patient.
void spingame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void spingame_draw(Camera2D fit);

#endif
