// draw.h - everything that appears on screen.
// These functions only read the simulation; they never change it.

#ifndef DRAW_H
#define DRAW_H

// Colours. Blue is taken from the logo.
#define C_BG        (Color){ 244, 247, 251, 255 }
#define C_CARD      (Color){ 255, 255, 255, 255 }
#define C_EDGE      (Color){ 218, 225, 235, 255 }
#define C_LIGHT     (Color){ 200, 208, 220, 255 }
#define C_INK       (Color){  33,  45,  64, 255 }
#define C_MUTED     (Color){ 125, 137, 155, 255 }
#define C_BRAND     (Color){  62, 116, 196, 255 }
#define C_EMERGENCY (Color){ 214,  69,  80, 255 }
#define C_URGENT    (Color){ 238, 154,  58, 255 }
#define C_ROUTINE   (Color){  76, 175, 125, 255 }

#include <stdbool.h>
#include "sim.h"

#define SCREEN_W 1000
#define SCREEN_H 600

// Draw one complete frame. Call between BeginDrawing() and EndDrawing().
void draw_frame(const Sim *sim, float speed, bool paused);

#endif

