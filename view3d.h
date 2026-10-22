// view3d.h - a 3D view of the department, as an alternative to the 2D screen.
// Like draw.c it only reads the simulation; it never changes it.
// Press V during a shift to switch between the 2D and 3D views.

#ifndef VIEW3D_H
#define VIEW3D_H

#include <stdbool.h>
#include "sim.h"

// Camera controls: drag with the left mouse button to turn, drag with the
// right button to move, scroll to zoom. Number keys fly to a room: 0 A&E,
// 1-7 the other rooms (see rooms3d.h), 8 the whole hospital, 9 the helipad
// and ambulance bays. F or PAGE UP / PAGE DOWN (or the floor buttons) go up
// and down the floors, and out to the view of the whole building. T starts
// the tour; clicking a patient follows them.
// Call once per frame while the 3D view is showing, before drawing.
void view3d_update_camera(void);

// Draw one complete frame in 3D. Call between BeginDrawing() and EndDrawing().
void view3d_draw(const Sim *sim, float speed, bool paused);

// Free the signs' textures. Call once before CloseWindow().
void view3d_close(void);

#endif