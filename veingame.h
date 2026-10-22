// veingame.h - "Find the vein": the blood test mini-game.
//
// A patient's arm, with the vein only faintly showing under the skin. Hold
// the right mouse button to feel for it (the ring pulses blue over the vein),
// then left-click to put the needle in there. A side view then shows the
// needle going in: hold the left button to push it deeper, and let go as
// soon as blood shows in the needle. Too far and the vein "blows". You get
// three goes.
//
// It's offered for a real patient who has just sat down in bloods. A clean
// go halves the rest of their time there; three misses make it longer. With
// nobody in bloods, it's a practice round that changes nothing.

#ifndef VEINGAME_H
#define VEINGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices a patient arriving in bloods.
void veingame_watch(const Sim *sim);

// The patient who could have their blood taken, or 0 if nobody.
int veingame_offer(void);

// Start a game: for that patient if there is one, or a practice round.
void veingame_start(const Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool veingame_active(void);

// Input and timing for one frame; applies the result to a real patient.
void veingame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void veingame_draw(Camera2D fit);

#endif
