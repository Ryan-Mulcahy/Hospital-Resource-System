// setup.h - the setup screen before each day, and the results of earlier
// shifts to compare against.
//
// Before a day starts you choose what kind of day it is (normal, flu season
// or a major incident), how many nurses and doctors are on each shift, how
// many A&E beds are open, how much equipment there is, and whether the power
// can cut out by itself. Underneath are the results of the shifts played so
// far, with the setup each one had, so you can see what difference one more
// doctor (say) made. R during a shift comes back here to try the day again.

#ifndef SETUP_H
#define SETUP_H

#include <stdbool.h>
#include "sim.h"

// Clicks on the setup screen: the - and + buttons, the kinds of day, the
// power cut switch and "Usual setup" change `setup`. Returns true when the
// day should start (the Start button, or N).
bool setup_screen_update(Setup *setup);

// Draw the setup screen for this day, laid out for 1000x600.
void draw_setup_screen(const Setup *setup, int day);

// Remember how a shift went, for the table on the setup screen. Call once,
// when the shift ends.
void setup_record_shift(const Sim *sim);

// A short summary of a setup, e.g. "4+2 nurses, 2+1 doctors, 8 beds".
const char *setup_summary(const Setup *setup);

#endif
