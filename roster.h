// roster.h - who is on duty. Staff come from a pool and rotate, so each shift
// has a different mix of nurses and doctors.

#ifndef ROSTER_H
#define ROSTER_H

#include <stdbool.h>
#include "sim.h"

// Shift number: 0 = day 1, 1 = night 1, 2 = day 2, 3 = night 2, ...
int roster_shift_number(int day, ShiftType shift);

// Name of the k-th nurse or doctor on duty on a given shift.
// (Which staff those are depends on how many are on each shift, in `setup`.)
const char *roster_nurse(const Setup *setup, int shiftNumber, int k);
const char *roster_doctor(const Setup *setup, int shiftNumber, int k);

// Name of the k-th on-call doctor for a shift (k from 0 to ON_CALL_MAX - 1;
// they're called in one at a time when it gets busy).
const char *roster_on_call(int shiftNumber, int k);

// Give every nurse and doctor in the sim their name for the current shift.
void roster_assign(Sim *sim);


// The roster screen, shown before a shift starts: who is on this shift and
// who is on the next one. Call between BeginDrawing() and EndDrawing().
void draw_roster_screen(const Setup *setup, int day, ShiftType shift);

// True on the frame the button to start the shift is clicked.
bool roster_start_clicked(void);
#endif