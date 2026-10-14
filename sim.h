// sim.h - the simulation state and the functions that advance it.
// Nothing in here draws anything; draw.c reads this state to display it.

#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include "patient.h"

#define BED_COUNT    6
#define NURSE_COUNT 4
#define DOCTOR_COUNT 2
// A nurse assesses every patient first, then decides if a doctor is needed.
#define ASSESS_TIME       1.0f   // seconds a nurse takes to assess a patient
#define URGENT_WAIT_LIMIT 15.0f  // an urgent patient who has waited this long needs a doctor (1 hour)


// A day is two 12-hour shifts: the day shift from 07:00 to 19:00, then the
// night shift from 19:00 to 07:00. At 1x speed each shift lasts SHIFT_LENGTH
// real seconds, so each simulated second is 4 minutes of shift time.
#define SHIFT_LENGTH   180.0f    // simulated seconds per shift
#define SHIFT_HOURS    12
#define DAY_START_HOUR 7

typedef enum { SHIFT_DAY, SHIFT_NIGHT } ShiftType;

typedef struct {
    Patient *occupant;       // NULL when the bed is free
} Bed;

typedef struct {
    int busyWithBed;         // index of the bed they're treating, or -1 if free
} Nurse;


typedef struct {
    int busyWithBed;         // index of the bed they're treating, or -1 if free
} Doctor;

// The numbers for one shift, shown on the end-of-shift report.
typedef struct {
    int arrived;             // patients who came in during the shift
    int admitted;            // patients given a bed during the shift
    int treated;             // patients discharged during the shift
    int treatedBySeverity[3];// discharged, by severity (index 0 = severity 1)
    float totalWait;         // total waiting time of the patients admitted
    float longestWait;       // longest time any patient waited for a bed
    float bedBusyTime;       // bed-seconds occupied
    float nurseBusyTime;    // nurse-seconds spent treating
    float doctorBusyTime;    // doctor-seconds spent treating
    int sawDoctor;           // patients a nurse sent to a doctor

} ShiftStats;

typedef struct {
    Patient *waiting;        // head of the waiting queue (most urgent first)
    int waitingCount;
    Bed beds[BED_COUNT];
    Nurse nurses[NURSE_COUNT];
    Doctor doctors[DOCTOR_COUNT];
    float clock;             // simulated seconds since 07:00 at the start of the day
    float nextArrival;       // clock time of the next random arrival
    int nextId;

    int day;                 // 1 for the first day, 2 for the next, ...
    ShiftType shift;         // which shift is on
    bool shiftOver;          // true once the current shift has ended
    ShiftStats stats;        // the current shift
    ShiftStats dayStats;     // the finished day shift, kept for the whole-day totals
} Sim;

// Start a new day: an empty department at 07:00, day shift on.
void sim_init(Sim *sim, int day);

// Hand over from the day shift to the night shift. Patients who are waiting
// or in a bed stay where they are; only the shift statistics start again.
void sim_start_night(Sim *sim);

void sim_update(Sim *sim, float dt);
void sim_add_patient(Sim *sim, int severity);
void sim_free(Sim *sim);

// Index of the nurse treating this bed, or -1 if nobody is.
int sim_nurse_at(const Sim *sim, int bed);

// Index of the doctor treating this bed, or -1 if nobody is.
int sim_doctor_at(const Sim *sim, int bed);

// Convert simulated seconds into minutes of shift time.
float sim_minutes(float simSeconds);

// The current time of day on the clock, for example 14:32.
void sim_time_of_day(const Sim *sim, int *hour, int *minute);

#endif