// report.h - the end-of-shift report screen.
// Shown instead of the normal view once the shift clock runs out.

#ifndef REPORT_H
#define REPORT_H

#include <stdbool.h>
#include "sim.h"

// Draw the report. Call between BeginDrawing() and EndDrawing().
void draw_shift_report(const Sim *sim);

// True on the frame the button to continue is clicked.
bool report_new_shift_clicked(void);

#endif