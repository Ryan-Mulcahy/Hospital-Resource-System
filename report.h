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

// True on the frame the "Patients treated" button is clicked.
bool report_view_log_clicked(void);

// The treatment log page: every patient discharged this shift, one per row.
// scroll is the index of the first row shown.
void draw_patient_log(const Sim *sim, int scroll);

// The largest useful scroll value for this shift's log (0 if it all fits).
int report_log_max_scroll(const Sim *sim);

// True on the frame the "Back to report" button on the log page is clicked.
bool report_log_back_clicked(void);

// True on the frame the "Equipment & rooms" button is clicked.
bool report_view_equipment_clicked(void);

// The equipment and rooms page: how busy each piece of equipment and each
// room was during the shift, and how long patients waited for them.
void draw_equipment_report(const Sim *sim);

// True on the frame the "Back to report" button on the equipment page is clicked.
bool report_equipment_back_clicked(void);

#endif