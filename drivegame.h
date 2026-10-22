// drivegame.h - "Blue lights": drive the ambulance in.
//
// A top-down road with three lanes of traffic. Steer the ambulance with
// LEFT/RIGHT (or A/D), brake with DOWN (or S), and weave through to the
// hospital. Clip a car and you slow right down.
//
// This is a real emergency: when you get there, they join the waiting room.
// Arrive quickly and the crew had a head start, so their treatment in A&E is
// 40% shorter; slowly, and it's 20% longer.

#ifndef DRIVEGAME_H
#define DRIVEGAME_H

#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

// Start a drive: an emergency call comes in.
void drivegame_start(void);

// Is the game on screen? (The simulation doesn't run while it is.)
bool drivegame_active(void);

// Input and timing for one frame; brings the patient in at the end.
void drivegame_update(Sim *sim);

// Draw the game, laid out for 1000x600 and scaled to the window by `fit`.
// Call between BeginDrawing() and EndDrawing(), not inside BeginMode2D().
void drivegame_draw(Camera2D fit);

#endif
