// sim.h - the simulation state and the functions that advance it.
// Nothing in here draws anything; draw.c reads this state to display it.
 
#ifndef SIM_H
#define SIM_H
 
#include <stdbool.h>
#include "patient.h"
 
// 8 A&E beds, plus a resus bay (the last bed) kept for emergencies only
#define BED_COUNT    9
#define RESUS_BED    (BED_COUNT - 1)
#define NURSE_COUNT 4
#define DOCTOR_COUNT 2
 
// Fewer staff on at night. NURSE_COUNT and DOCTOR_COUNT are the day numbers.
#define NIGHT_NURSES  2
#define NIGHT_DOCTORS 1
#define NURSES_ON(shift)  ((shift) == SHIFT_NIGHT ? NIGHT_NURSES  : NURSE_COUNT)
#define DOCTORS_ON(shift) ((shift) == SHIFT_NIGHT ? NIGHT_DOCTORS : DOCTOR_COUNT)
 
// Night arrivals, in tenths of a second: one patient every 2.0 to 4.0 seconds
#define NIGHT_GAP_MIN 20
#define NIGHT_GAP_MAX 40
// A nurse assesses every patient first, then decides if a doctor is needed.
#define ASSESS_TIME       1.0f   // seconds a nurse takes to assess a patient

// Patients have to walk to their bed or room before anything starts there.
// The 3D view times their walks to match these.
#define WALK_TO_BED    3.0f      // seconds from the waiting room to an A&E bed
#define WALK_TO_ROOM   6.0f      // seconds from A&E (or another room) to a room

// Helicopter rescues: 1 or 2 a shift. Rescued patients go to the front of the queue.
#define RESCUE_CHANCE_SECOND 50  // % chance of a second rescue later in the shift
 
 
// A day is two 12-hour shifts: the day shift from 07:00 to 19:00, then the
// night shift from 19:00 to 07:00. At 1x speed each shift lasts SHIFT_LENGTH
// real seconds, so each simulated second is 4 minutes of shift time.
#define SHIFT_LENGTH   180.0f    // simulated seconds per shift
#define SHIFT_HOURS    12
#define DAY_START_HOUR 7
 
typedef enum { SHIFT_DAY, SHIFT_NIGHT } ShiftType;

// The rooms patients can be sent to after A&E. Each takes a set number of
// patients at once for a set time; anyone else waits in that room's queue.
typedef enum { ROOM_XRAY, ROOM_MRI, ROOM_BLOODS, ROOM_THEATRE, ROOM_WARD, ROOM_TYPES } RoomType;
#define ROOM_NONE      -1
#define ROOM_SLOTS_MAX  4        // the most patients any room holds at once

typedef struct {
    const char *name;        // e.g. "X-ray"
    int capacity;            // patients at once
    float duration;          // simulated seconds each patient spends there
    Patient *in[ROOM_SLOTS_MAX]; // who is in each place (NULL = free)
    Patient *queue;          // waiting for this room, most urgent first
    int queueCount;
} SimRoom;
 
typedef struct {
    Patient *occupant;       // NULL when the bed is free
} Bed;
 
typedef struct {
    const char *name;        // staff member's name, set by the roster (NULL until then)
    int busyWithBed;         // index of the bed they're treating, or -1 if free
} Nurse;
 
typedef struct {
    const char *name;        // staff member's name, set by the roster (NULL until then)
    int busyWithBed;         // index of the bed they're treating, or -1 if free
} Doctor;
 
// One line in the treatment log: a patient who was discharged this shift.
#define TREATED_BY_LEN 40
 
typedef struct {
    int id;
    int severity;                    // 1 = emergency, 2 = urgent, 3 = routine
    const Condition *condition;      // points into the conditions table (lives all run)
    char treatedBy[TREATED_BY_LEN];  // e.g. "Nurse Aoife Byrne", or "Nurse 2" before names
    char sentTo[PATHWAY_LEN];        // rooms after A&E, e.g. "X-ray", or "Home"
    bool rescue;                     // came in by helicopter
    int rescueService;               // ...and which service brought them
    bool sawDoctor;                  // true if a nurse sent them to a doctor
    float arrivalTime;               // clock time they arrived
    float wait;                      // seconds waiting for a bed
    float dischargeTime;             // clock time they left
} TreatedRecord;
 
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
    int sentTo[ROOM_TYPES];  // patients sent to each room after A&E
    int leftHospital;        // patients who finished in the rooms and went home
    int rescues;             // patients brought in by helicopter
    int surgeriesPlayed;     // operations done in the operation game (not practice)
    int complications;       // ...of those, the ones where you touched the edge or ran out of time
} ShiftStats;
 
typedef struct {
    Patient *waiting;        // head of the waiting queue (most urgent first)
    int waitingCount;
    Bed beds[BED_COUNT];
    Nurse nurses[NURSE_COUNT];
    Doctor doctors[DOCTOR_COUNT];
    float clock;             // simulated seconds since 07:00 at the start of the day
    float nextArrival;       // clock time of the next random arrival
    float nextRescue;        // clock time of the next helicopter rescue (< 0: none)
    int nextId;

    SimRoom rooms[ROOM_TYPES];   // X-ray, MRI, bloods, theatre and the ward
 
    int day;                 // 1 for the first day, 2 for the next, ...
    ShiftType shift;         // which shift is on
    bool shiftOver;          // true once the current shift has ended
    ShiftStats stats;        // the current shift
    ShiftStats dayStats;     // the finished day shift, kept for the whole-day totals
 
    // Treatment log for the current shift. The array grows with realloc
    // (doubling) as patients are discharged, and is freed by sim_free.
    TreatedRecord *treatedLog;
    int treatedLogCount;     // records in use
    int treatedLogCapacity;  // records allocated
} Sim;
 
// Start a new day: an empty department at 07:00, day shift on.
// Only call on a new Sim or after sim_free, or the treatment log leaks.
void sim_init(Sim *sim, int day);
 
// Hand over from the day shift to the night shift. Patients who are waiting
// or in a bed stay where they are; the shift statistics and the treatment
// log start again (the log keeps its memory to reuse).
void sim_start_night(Sim *sim);
 
void sim_update(Sim *sim, float dt);
void sim_add_patient(Sim *sim, int severity);

// A patient flown in by helicopter (coast guard, search and rescue or the fire
// service, depending on what happened to them): an emergency who goes
// straight to the front of the queue. Happens by itself 1 or 2 times a shift;
// the H key adds one too.
void sim_add_rescue(Sim *sim);

// The result of the operation game for the patient in theatre with this id.
// Won: the rest of their surgery takes half as long. Lost: a complication,
// so it takes 60% longer. Does nothing if they're no longer in theatre.
void sim_surgery_result(Sim *sim, int id, bool won);

// Patients in a room or waiting for one, across every room.
int sim_patients_in_rooms(const Sim *sim);

// The rooms a patient is going to, as text, e.g. "Theatre, Ward" or "Home".
void sim_route_text(const Patient *p, char *out, int size);
// Free every patient and the treatment log.
void sim_free(Sim *sim);
 
// Write this shift's treatment log to a CSV file (opens in Excel).
// Returns false if the file couldn't be written. Called automatically at the
// end of each shift, as log_day<N>_day.csv or log_day<N>_night.csv.
bool sim_save_log_csv(const Sim *sim, const char *filename);
 
// Index of the nurse treating this bed, or -1 if nobody is.
int sim_nurse_at(const Sim *sim, int bed);
 
// Index of the doctor treating this bed, or -1 if nobody is.
int sim_doctor_at(const Sim *sim, int bed);
 
// Convert simulated seconds into minutes of shift time.
float sim_minutes(float simSeconds);
 
// The current time of day on the clock, for example 14:32.
void sim_time_of_day(const Sim *sim, int *hour, int *minute);
 
#endif