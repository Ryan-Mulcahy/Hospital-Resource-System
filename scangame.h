// scangame.h - "Hold still": the X-ray and CT mini-game.
//
// The patient is on the table, breathing and fidgeting, and the live picture
// moves with them. Line the frame up with the target (a hand, a chest or a
// skull) and click (or press SPACE) to take the image at a moment when
// they're still. Moving, or off the target, and the image is no good.
//
// It's offered for a real patient who has just got onto the table in X-ray
// or CT. A sharp image first time halves the rest of their time there; a
// blurry one means a retake, so they're there longer. With nobody on the
// table, it's a practice round that changes nothing. About 10 seconds.

#ifndef SCANGAME_H
#define SCANGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Call every frame during a shift: notices a patient arriving in X-ray or CT.
void scangame_watch(const Sim *sim);

// The patient who could be scanned, or 0 if nobody.
int scangame_offer(void);

// Start a game: for that patient if there is one, or a practice round.
void scangame_start(const Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool scangame_active(void);

// Input and timing for one frame; applies the result to a real patient.
void scangame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void scangame_draw(Camera2D fit);

#endif
