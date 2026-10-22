// outside3d.h - the outside of the hospital in the 3D view.
//
// The grounds round the building (a car park with visitors coming and going,
// the main entrance, flower beds, flags, a bus stop and benches), and the
// building itself seen from outside: the floors below the one you're looking
// at, closed up with their windows (lit at night), and, from the "outside"
// view, the whole building with its roof.

#ifndef OUTSIDE3D_H
#define OUTSIDE3D_H

#include "raylib.h"

// Make the signs the first time. Call each frame before BeginMode3D().
void outside3d_prepare(void);

// Draw the grounds, and the building's floors below `viewFloor` closed up
// (all of them, and the roof, when viewFloor is VIEW_OUTSIDE).
// Call inside BeginMode3D().
void outside3d_draw(int viewFloor);

// The main entrance's frame, sign and sliding doors (they open as visitors
// come to them). Drawn by outside3d_draw when the ground floor is closed up;
// view3d.c calls it for the ground floor's own view.
void outside3d_draw_main_doors(void);

// The main entrance's glass. Call after everything else, like other glass.
void outside3d_draw_main_door_glass(void);

// Lamps in the car park and over the entrance. Call inside BeginBlendMode(BLEND_ADDITIVE).
void outside3d_lights(int viewFloor);

// Free the signs. Call once before CloseWindow().
void outside3d_close(void);

#endif
