// heligame.h - "Touchdown": land the rescue helicopter on the helipad.
//
// Seen from the side, with the wind gusting. LEFT/RIGHT move the helicopter
// across, hold UP for more lift (it sinks slowly on its own) and DOWN to come
// down faster. Touch down gently, on the pad, and not drifting sideways.
//
// It's offered while a rescue helicopter is on its way in. A smooth landing
// means a quick handover, so the patient's treatment in A&E takes 40% less;
// a hard landing (or missing the pad, or taking too long) makes it 25%
// longer. With no helicopter in the air, it's a practice round.

#ifndef HELIGAME_H
#define HELIGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices a rescue helicopter in the air.
void heligame_watch(const Sim *sim);

// The rescue patient on the helicopter that's on its way, or 0 if none.
int heligame_offer(void);

// Start a game: for that rescue if there is one, or a practice round.
void heligame_start(const Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool heligame_active(void);

// Is the helicopter flying? (main.c plays the rotor sound while it is.)
bool heligame_flying(void);

// Input and timing for one frame; applies the result to a real rescue.
void heligame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void heligame_draw(Camera2D fit);

#endif
