// draw.h - everything that appears on screen.
// These functions only read the simulation; they never change it.

#ifndef DRAW_H
#define DRAW_H

#include <stdbool.h>
#include "sim.h"

#define SCREEN_W 1000
#define SCREEN_H 600

// Draw one complete frame. Call between BeginDrawing() and EndDrawing().
void draw_frame(const Sim *sim, float speed, bool paused);

#endif