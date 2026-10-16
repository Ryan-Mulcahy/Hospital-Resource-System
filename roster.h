// roster.h - who is on duty. Staff come from a pool and rotate, so each shift
// has a different mix of nurses and doctors.

#ifndef ROSTER_H
#define ROSTER_H

#include "sim.h"

// Shift number: 0 = day 1, 1 = night 1, 2 = day 2, 3 = night 2, ...
int roster_shift_number(int day, ShiftType shift);

// Name of the k-th nurse or doctor on duty on a given shift.
const char *roster_nurse(int shiftNumber, int k);
const char *roster_doctor(int shiftNumber, int k);

// Give every nurse and doctor in the sim their name for the current shift.
void roster_assign(Sim *sim);

#endif