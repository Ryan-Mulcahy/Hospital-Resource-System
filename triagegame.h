// triagegame.h - "Triage call": sort three new arrivals.
//
// Three people have just come in, each with a short description ("crushing
// chest pain, grey and sweaty, 58", with their heart rate and blood
// pressure). Drag each one to EMERGENCY, URGENT or ROUTINE before the clock
// runs out, then send them through.
//
// These are real arrivals: they join the waiting room with the priority you
// gave them, and that decides where they are in the queue. Call an emergency
// routine and they're seen too late; call a routine case an emergency and
// they take a bed ahead of people who needed it more. The report counts the
// calls that were right, too low and too high.

#ifndef TRIAGEGAME_H
#define TRIAGEGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Start a game: three new arrivals at the door.
void triagegame_start(Sim *sim);

// Is the game on screen? (The simulation doesn't run while it is.)
bool triagegame_active(void);

// Input and timing for one frame; sends the arrivals in when it's over.
void triagegame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void triagegame_draw(Camera2D fit);

#endif
