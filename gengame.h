// gengame.h - "Start the generator": when the power's gone and the backup
// generator doesn't start by itself.
//
// It opens by itself when that happens (it isn't on the mini-game menu).
// Pump the fuel pressure up with SPACE until the needle is in the green, then
// crank the engine with ENTER (or the START button) while it's there. Too
// little pressure and it won't catch; too much and it floods. Three tries.
// Get it going and the lights come back on in a moment; fail, and the power
// stays off until the electricians fix it (see sim_generator_result).

#ifndef GENGAME_H
#define GENGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Open the game (the generator didn't start).
void gengame_start(void);

// Is the game on screen? (The simulation doesn't run while it is.)
bool gengame_active(void);

// Input and timing for one frame; tells the simulation how it went.
void gengame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void gengame_draw(Camera2D fit);

#endif
