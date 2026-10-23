// sim.h - the simulation state and the functions that advance it.
// Nothing in here draws anything; draw.c reads this state to display it.
 
#ifndef SIM_H
#define SIM_H
 
#include <stdbool.h>
#include "patient.h"
 
// 8 A&E beds, plus a resus bay (the last bed) kept for emergencies only.
// Some of the 8 can be closed for the day on the setup screen.
#define BED_COUNT    9
#define RESUS_BED    (BED_COUNT - 1)

// How many nurses and doctors are on each shift is chosen on the setup
// screen before each day (see Setup below), up to these numbers.
#define NURSE_MAX    6
#define DOCTOR_MAX   4

// When the waiting room is full, an on-call doctor is called in (see
// WAITING_ROOM_SEATS below). They take the next place after the shift's doctors
// (doctors[sim_shift_doctors(sim)]), and go home once it's quieter.
#define ON_CALL_MAX    3                    // the most on-call doctors (how many is on the setup screen)
#define DOCTOR_SLOTS   (DOCTOR_MAX + ON_CALL_MAX)   // room for the shift's doctors and the on-call ones
// An on-call doctor is called when the waiting room is full (every seat
// taken) for ON_CALL_AFTER seconds without a break. If it's still full for
// that long after they arrive, the next one is called, up to the number on
// the setup screen. They go home again one at a time (the last in first)
// once it's down to half full and they've finished with their patient.
#define WAITING_ROOM_SEATS 20    // seats in the waiting room (the 3D view has this many chairs)
#define ON_CALL_AFTER  1.0f      // seconds (4 minutes) it has to stay full, so a moment's rush doesn't call them
#define ON_CALL_HOME   (WAITING_ROOM_SEATS / 2)   // they go home once this few are waiting (or fewer)
#define ON_CALL_DELAY  3.0f      // seconds (12 minutes) for them to get to the hospital

// A patient waiting for a doctor isn't left alone: a free nurse stays with
// them and starts their treatment at half speed, but can only get it this
// far on their own (the rest needs the doctor).
#define NURSE_HELP_SPEED 0.5f
#define NURSE_HELP_LIMIT 0.5f

// Helicopters: a rescue lands HELI_FLIGHT seconds after it's called, and
// only one can be on the helipad at a time, so landings are at least
// HELI_PAD_GAP apart. The patient is then wheeled in from the helipad.
#define HELI_FLIGHT    7.0f
#define HELI_PAD_GAP  13.0f
#define HELI_WALK_IN   4.0f
 
// Night arrivals, in tenths of a second: one patient every 2.0 to 4.0 seconds
#define NIGHT_GAP_MIN 20
#define NIGHT_GAP_MAX 40
// A nurse assesses every patient first, then decides if a doctor is needed.
#define ASSESS_TIME       1.0f   // seconds a nurse takes to assess a patient

// Patients have to walk to their bed or room before anything starts there.
// The 3D view times their walks to match these.
#define WALK_TO_BED    3.0f      // seconds from the waiting room to an A&E bed
#define WALK_TO_ROOM   6.0f      // seconds from A&E (or another room) to a room on the ground floor
#define WALK_PER_FLOOR 2.0f      // ...and this much longer for each floor up or down (the lift)
#define WALK_OUT       5.0f      // seconds to be pushed out to the front door in a wheelchair

// The hospital has three floors: 0 the ground floor (A&E), 1 and 2 upstairs.
#define FLOORS 3

// Helicopter rescues: 1 or 2 a shift. Rescued patients go to the front of the queue.
#define RESCUE_CHANCE_SECOND 50  // % chance of a second rescue later in the shift
 
 
// A day is two 12-hour shifts: the day shift from 07:00 to 19:00, then the
// night shift from 19:00 to 07:00. At 1x speed each shift lasts SHIFT_LENGTH
// real seconds, so each simulated second is 4 minutes of shift time.
#define SHIFT_LENGTH   180.0f    // simulated seconds per shift
#define SHIFT_HOURS    12
#define DAY_START_HOUR 7
 
typedef enum { SHIFT_DAY, SHIFT_NIGHT } ShiftType;   // (also used as an index: 0 day, 1 night)

// The rooms patients can be sent to after A&E. Each takes a set number of
// patients at once for a set time; anyone else waits in that room's queue.
// The first five are on the ground floor with A&E; the rest are upstairs.
typedef enum {
    ROOM_XRAY, ROOM_MRI, ROOM_BLOODS, ROOM_THEATRE, ROOM_WARD,          // ground floor
    ROOM_CT, ROOM_ULTRASOUND, ROOM_CARDIO, ROOM_ICU, ROOM_PHARMACY,      // first floor
    ROOM_MATERNITY, ROOM_CHILDREN, ROOM_PHYSIO, ROOM_DIALYSIS,          // second floor
    ROOM_TYPES
} RoomType;
#define ROOM_NONE      -1
#define ROOM_SLOTS_MAX  4        // the most patients any room holds at once
#define ROOM_TURNOVER   1.5f     // seconds (6 minutes) to get a place ready for the next
                                 // patient, so the last one has left before the next comes in

// Equipment is shared round the hospital, and there isn't always enough.
// A patient who needs something has to wait for it to be free:
//   heart monitors, ventilators, defibrillators and drip pumps before their
//     treatment in A&E starts (they give them back when it's done);
//   a ventilator for every patient in ICU, for their whole stay;
//   a wheelchair (and a porter) for every trip they make after their A&E
//     treatment: from their A&E bed to their first room, from room to room,
//     and out to the front door at the end. The porter takes it back as soon
//     as they get there. A patient waiting for one stays where they are, so
//     they're still using their A&E bed or their place in the room.
typedef enum { EQUIP_MONITOR, EQUIP_VENTILATOR, EQUIP_DEFIB, EQUIP_DRIP, EQUIP_WHEELCHAIR, EQUIP_TYPES } EquipType;
#define EQUIP_BIT(e) (1u << (e))

typedef struct {
    const char *name;        // "Heart monitor"
    const char *key;         // "monitor": the word used in conditions.csv
    int total;               // how many the hospital has
    int inUse;               // how many are with a patient now
} Equipment;

// ---- The setup for a day, chosen on the setup screen ----
// The kind of day it is.
typedef enum {
    SCENARIO_NORMAL,         // an ordinary day
    SCENARIO_FLU,            // flu season: more patients all day and night
    SCENARIO_MAJOR_INCIDENT, // a bus crash during the day shift: a wave of casualties
    SCENARIO_COUNT
} Scenario;

#define EQUIP_MAX_EACH 7     // the most of any one piece of equipment

typedef struct {
    Scenario scenario;
    int nurses[2];           // nurses on the day shift [SHIFT_DAY] and the night shift [SHIFT_NIGHT]
    int doctors[2];          // ...and doctors (not counting the on-call doctor)
    int beds;                // A&E beds open, not counting the resus bay (up to BED_COUNT - 1)
    int equipment[EQUIP_TYPES]; // how many of each piece of equipment
    bool powerCuts;          // can the power go off by itself (C still cuts it either way)
    bool carryOver;          // start the day with yesterday's patients still in (from day 2 on)
    int onCall;              // on-call doctors who can be called in, one at a time (0 to ON_CALL_MAX)
} Setup;

// The usual setup: 4 nurses and 2 doctors by day, 2 and 1 at night, all 8
// beds, the usual equipment, an ordinary day, power cuts on.
void sim_default_setup(Setup *setup);

// Keep a setup within the limits (so a bad one can't break anything).
void sim_clamp_setup(Setup *setup);

// The smallest and largest number allowed for each thing on the setup screen.
#define SETUP_NURSES_MIN    1
#define SETUP_DOCTORS_MIN   1
#define SETUP_BEDS_MIN      3

typedef struct {
    const char *name;        // e.g. "X-ray"
    int capacity;            // patients at once
    float duration;          // simulated seconds each patient spends there
    Patient *in[ROOM_SLOTS_MAX]; // who is in each place (NULL = free)
    float ready[ROOM_SLOTS_MAX]; // seconds until a place that's just been left is ready again
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
    char sentTo[PATHWAY_LEN + 16];   // rooms after A&E, e.g. "X-ray", or "Home"
    bool rescue;                     // came in by helicopter
    int rescueService;               // ...and which service brought them
    bool sawDoctor;                  // true if a doctor treated them (sent by a nurse, or a free doctor)
    float arrivalTime;               // clock time they arrived
    float wait;                      // seconds waiting for a bed
    float dischargeTime;             // clock time they left
} TreatedRecord;
 
// ---- Power cuts ----
// When the power goes, the emergency lighting comes on and the backup
// generator starts after GENERATOR_START seconds. Sometimes it doesn't start
// (GENERATOR_FAIL_CHANCE %): then someone has to start it by hand (the
// generator game, which main.c opens). If that fails too, the electricians
// get it going after GENERATOR_FIX seconds. While the power is off:
//   the scanners (X-ray, CT and MRI) stop, and their patients wait in them;
//   the lifts stop, so anyone on their way to or from upstairs is stuck;
//   treatment in A&E goes on under the emergency lights, a little slower.
// Ventilators and monitors run on their batteries, so nobody loses those.
typedef enum { POWER_ON, POWER_OUT } PowerState;
#define GENERATOR_START        2.5f   // seconds (10 minutes) for the generator to start
#define GENERATOR_FAIL_CHANCE  35     // % of power cuts where it doesn't start by itself
#define GENERATOR_FIX         10.0f   // seconds (40 minutes) for the electricians, if the game fails
#define POWER_CUT_CHANCE       30     // % chance of a power cut by itself in a shift (if they're on)
#define DARK_TREATMENT_SPEED  0.75f   // treatment speed under the emergency lights

// ---- What happened when, for the graphs on the report ----
// The waiting room is sampled every SAMPLE_EVERY seconds (10 minutes), and
// the things that happened are noted with their time.
#define SAMPLE_EVERY  2.5f
#define SAMPLES       73        // (SHIFT_LENGTH / SAMPLE_EVERY) + 1: from 07:00 to 19:00 every 10 minutes
#define MAX_EVENTS    40
typedef enum {
    EVENT_HELI_LANDED,       // a rescue helicopter landed
    EVENT_ON_CALL,           // the on-call doctor came in
    EVENT_POWER_OFF,         // the power went off
    EVENT_POWER_ON,          // ...and came back (the generator started)
    EVENT_INCIDENT,          // a major incident: the casualties started arriving
    EVENT_TYPES
} EventType;
typedef struct {
    float time;              // seconds since the shift started
    EventType type;
} ShiftEvent;

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
    int onCallCalls;         // times the on-call doctor was called in
    float onCallTime;        // seconds the on-call doctor was in
    float nurseHelpTime;     // seconds nurses spent starting treatment while waiting for a doctor
    int surgeriesPlayed;     // operations done in the operation game (not practice)
    int complications;       // ...of those, the ones where you touched the edge or ran out of time
    int shocksPlayed;        // the defibrillator game, on real patients
    int shocksFailed;
    int spinsPlayed;         // the centrifuge game (bloods), on real patients
    int spinsFailed;
    int scansPlayed;         // the hold-still game in X-ray and CT, on real patients
    int scansFailed;         // ...blurry shots that needed a retake
    int pharmacyPlayed;      // the pharmacy rush game, on real patients
    int pharmacyFailed;      // ...wrong pill or out of time
    int pharmacySentBack;    // ...of those, wrong pills: sent back to the pharmacy's queue
    int triageGames;         // the triage call game (three arrivals each time)
    int triageGamesLost;     // ...games with any call wrong
    int triageRight;         // arrivals given the right priority
    int triageTooLow;        // ...given a lower priority than they needed (seen too late)
    int triageTooHigh;       // ...given a higher one (took a bed sooner than they needed)
    int drivesPlayed;        // the blue lights game (an ambulance brings an emergency in)
    int drivesSlow;          // ...arrived too slowly
    int landingsPlayed;      // the helicopter landing game, on real rescues
    int landingsFailed;      // ...hard landings, or out of time
    int generatorGames;      // the generator wouldn't start and you had a go at it
    int generatorFailed;     // ...and couldn't get it going

    // Power cuts
    int powerCuts;           // times the power went off
    float powerOffTime;      // seconds without power (until the generator started)
    int scansHeld;           // patients stuck in a scanner when the power went
    int liftStuck;           // patients stuck in the lifts when the power went

    // A major incident
    int incidentCasualties;  // casualties brought in

    // Patients already in the hospital when the shift started: handed over
    // from the day shift (at night), or from yesterday (if carryOver is on)
    int carriedOver;

    // The graphs: the waiting room every SAMPLE_EVERY seconds, and what happened when
    int samples;                     // how many taken so far
    int sampleWaiting[SAMPLES];      // patients in the waiting room
    float sampleLongestWait[SAMPLES];// how long the person who'd waited longest had been waiting (seconds; 0 if nobody)
    int sampleInBeds[SAMPLES];       // A&E beds in use
    ShiftEvent events[MAX_EVENTS];
    int eventCount;

    // Equipment
    float equipBusyTime[EQUIP_TYPES];    // item-seconds in use (for the % busy)
    int   equipPeak[EQUIP_TYPES];        // the most in use at once
    int   equipWaits[EQUIP_TYPES];       // patients who had to wait for one
    float equipWaitTotal[EQUIP_TYPES];   // seconds patients spent waiting for one
    float equipLongestWait[EQUIP_TYPES]; // longest anyone waited for one

    // The rooms after A&E
    float roomBusyTime[ROOM_TYPES];      // place-seconds with a patient in them
    int   roomLongestQueue[ROOM_TYPES];  // the longest each room's queue got
} ShiftStats;
 
typedef struct {
    Setup setup;             // staffing, beds, equipment and the kind of day (fixed for the day)
    Patient *waiting;        // head of the waiting queue (most urgent first)
    int waitingCount;
    Bed beds[BED_COUNT];
    Nurse nurses[NURSE_MAX];
    Doctor doctors[DOCTOR_SLOTS];  // the shift's doctors, then a place for one on call
    int onCall;              // on-call doctors in now (0 to setup.onCall)
    float onCallComing;      // seconds until the on-call doctor arrives (< 0: nobody called)
    float onCallPressure;    // seconds the waiting room has been full, without a break
    Patient *inbound;        // helicopter rescues on their way, in landing order
    float lastLanding;       // clock time the last rescue landed (or will)
    float clock;             // simulated seconds since 07:00 at the start of the day
    float nextArrival;       // clock time of the next random arrival
    float nextRescue;        // clock time of the next helicopter rescue (< 0: none)

    // Power
    PowerState power;
    float powerTimer;        // power off: seconds until the generator starts
    bool generatorStuck;     // power off and the generator didn't start: the generator game is needed
    float nextPowerCut;      // clock time of the next power cut by itself (< 0: none)
    float powerBackAt;       // clock time the power last came back (the lights flicker on), or < 0

    // A major incident (the bus crash scenario)
    float incidentAt;        // clock time the casualties start arriving (< 0: none)
    int incidentLeft;        // casualties still to arrive
    float incidentNext;      // clock time of the next one
    float incidentSince;     // clock time it started (for the banner), or < 0
    int nextId;

    SimRoom rooms[ROOM_TYPES];   // X-ray, MRI, bloods, theatre, the ward and the rooms upstairs
    Equipment equipment[EQUIP_TYPES];   // monitors, ventilators, defibrillators, drip pumps, wheelchairs
    Patient *leaving;        // being pushed out to the front door in a wheelchair (they go home after)
 
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
 
// Start a new day: an empty department at 07:00, day shift on, set up as
// `setup` says (NULL for the usual setup).
// Only call on a new Sim or after sim_free, or the treatment log leaks.
void sim_init(Sim *sim, int day, const Setup *setup);

// The next day, keeping everyone still in the hospital (the carryOver
// switch): the clock goes back to 07:00, the new setup and the new day's
// staff take over, and the counts start again. Patients in a bed that's
// closed in the new setup go back to the waiting room. Equipment patients
// have stays with them, even if there's less of it now (it isn't handed out
// again until enough has come back).
void sim_next_day(Sim *sim, const Setup *setup);

// Patients in the hospital now: waiting, in A&E, in the rooms after it,
// being taken out, or in a helicopter on its way.
int sim_patients_in_hospital(const Sim *sim);

// Nurses on this shift, and the shift's own doctors (not the on-call one).
int sim_nurses_on(const Sim *sim);
int sim_shift_doctors(const Sim *sim);

// Is this A&E bed open today? (The resus bay always is.)
bool sim_bed_open(const Sim *sim, int bed);
 
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

// Patients in A&E beds who need a doctor and haven't got one yet.
int sim_waiting_for_doctor(const Sim *sim);

// Doctors on duty now: the shift's doctors, plus the on-call doctor if they're in.
// (Their places in sim->doctors: the shift's doctors first, then the on-call one.)
int sim_doctors_on(const Sim *sim);

// Helicopter rescues on their way in (not landed yet).
int sim_inbound_count(const Sim *sim);

// The result of the defibrillator game for the patient in an A&E bed with
// this id. Won: their heart's back in rhythm, and the rest of their treatment
// takes half as long (so the defibrillator is free sooner). Lost: it takes
// longer, by 30% of their whole treatment.
void sim_shock_result(Sim *sim, int id, bool won);

// The result of the centrifuge game for the patient in bloods with this id.
// Won: balanced, so the results are back quickly and the rest of their time
// there is halved. Lost: the samples are re-run, so it takes longer (half as
// long again as usual).
void sim_spin_result(Sim *sim, int id, bool won);

// The result of the hold-still game for the patient in X-ray or CT (`room`)
// with this id. Won: a sharp image first time, so the rest of their time
// there is halved. Lost: blurry, so a retake: half as long again as usual.
void sim_scan_result(Sim *sim, int room, int id, bool won);

// How the pharmacy rush game went.
typedef enum {
    PHARMACY_FAST,           // the right pills, quickly: their visit is halved
    PHARMACY_SLOW,           // the right pills, but slowly: no change
    PHARMACY_WRONG,          // a wrong pill: they go back to the pharmacy's queue
    PHARMACY_TIMEOUT         // ran out of time: half as long again as usual
} PharmacyResult;

// The result of the pharmacy game for the patient in the pharmacy with this id.
void sim_pharmacy_result(Sim *sim, int id, PharmacyResult result);

// The triage call game. sim_triage_new makes a new arrival (a random
// severity and condition) who isn't in the department yet, for the game to
// show. sim_triage_admit then puts them in the waiting room with the
// priority they were given (1, 2 or 3), which decides where they go in the
// queue; 0 means nobody decided, so they keep their real priority and it
// isn't counted. Every patient from sim_triage_new must be admitted, once.
Patient *sim_triage_new(Sim *sim);
void sim_triage_admit(Sim *sim, Patient *p, int given);

// The triage game is over: count it (lost if any call was wrong).
void sim_triage_game(Sim *sim, bool allRight);

// The blue lights game: an ambulance brings an emergency in. `fast`: they
// got there quickly, so the crew had a head start (40% off their treatment);
// otherwise their treatment takes 20% longer. Returns the new patient's id.
int sim_ambulance_arrival(Sim *sim, bool fast);

// The helicopter landing game, for the rescue with this id that's still in
// the air. Won: a smooth landing and a quick handover, so their treatment in
// A&E takes 40% less. Lost: a rough landing, so it takes 25% longer.
void sim_landing_result(Sim *sim, int id, bool won);

// ---- Power ----
// Cut the power now (the C key, or by itself during a shift). Nothing
// happens if it's already off.
void sim_power_cut(Sim *sim);

// Is the generator waiting to be started by hand? (main.c opens the generator game.)
bool sim_generator_needed(const Sim *sim);

// The result of the generator game. Won: it starts in a moment. Lost: the
// power stays off until the electricians fix it (GENERATOR_FIX seconds).
void sim_generator_result(Sim *sim, bool won);

// The lights for the frame: 1 normal, 0 the power's off (emergency lights),
// and flickering in between for a moment when it comes back on.
float sim_lights(const Sim *sim);

// Seconds since the start of the current shift.
float sim_shift_time(const Sim *sim);

// Names for the scenarios and the events, e.g. "Flu season", "Power off".
const char *sim_scenario_name(Scenario s);
const char *sim_event_name(EventType e);

// Patients in a room or waiting for one, across every room.
int sim_patients_in_rooms(const Sim *sim);

// Which floor a room is on (0 ground, 1 first, 2 second).
int sim_room_floor(int room);

// The equipment in a set of bits (EQUIP_BIT), as text: "monitor, drip pump",
// or "" for none.
void sim_equipment_text(unsigned bits, char *out, int size);

// A short name for one piece of equipment, e.g. "Monitor", "Ventilator".
const char *sim_equipment_short(int e);

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