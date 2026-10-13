// sim.h - the simulation state and the functions that advance it.
// Nothing in here draws anything; draw.c reads this state to display it.

#ifndef SIM_H
#define SIM_H

#include "patient.h"

#define BED_COUNT    6
#define DOCTOR_COUNT 4

typedef struct {
    Patient *occupant;       // NULL when the bed is free
} Bed;

typedef struct {
    int busyWithBed;         // index of the bed they're treating, or -1 if free
} Doctor;

typedef struct {
    Patient *waiting;        // head of the waiting queue (most urgent first)
    int waitingCount;
    Bed beds[BED_COUNT];
    Doctor doctors[DOCTOR_COUNT];
    float clock;             // simulated seconds since start
    float nextArrival;       // clock time of the next random arrival
    int nextId;
    int admitted;            // patients who have been given a bed
    int treated;             // patients discharged
    float totalWait;         // sum of waiting times of admitted patients
} Sim;

void sim_init(Sim *sim);
void sim_update(Sim *sim, float dt);
void sim_add_patient(Sim *sim, int severity);
void sim_free(Sim *sim);

// Index of the doctor treating this bed, or -1 if nobody is.
int sim_doctor_at(const Sim *sim, int bed);

#endif