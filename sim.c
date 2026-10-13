// sim.c - arrivals, treatment, admission and doctor rounds.
// Each update: new patients arrive, treatment counts down in beds that have
// a doctor, every free bed takes the most urgent waiting patient, then every
// free doctor goes to the most urgent patient lying in a bed without one.

#include <stdlib.h>
#include "raylib.h"          // GetRandomValue
#include "sim.h"

static float random_arrival_gap(void)
{
    // 0.6 to 1.8 seconds. Was 0.3 to 1.5, but with 4 doctors that's more
    // work than they can keep up with and the queue grows forever.
    return (float)GetRandomValue(6, 18) / 10.0f;
}

static int random_severity(void)
{
    int roll = GetRandomValue(1, 10);
    if (roll <= 2) return 1;      // 20% emergency
    if (roll <= 5) return 2;      // 30% urgent
    return 3;                     // 50% routine
}

void sim_add_patient(Sim *sim, int severity)
{
    Patient *p = malloc(sizeof(Patient));
    if (p == NULL) return;

    float base = (severity == 1) ? 8.0f : (severity == 2) ? 5.0f : 3.0f;

    p->id = sim->nextId++;
    p->severity = severity;
    p->arrivalTime = sim->clock;
    p->treatmentTotal = base + (float)GetRandomValue(-10, 10) / 10.0f;
    p->treatmentLeft = p->treatmentTotal;
    p->next = NULL;

    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
}

void sim_init(Sim *sim)
{
    *sim = (Sim){ 0 };
    sim->nextId = 1;
    sim->nextArrival = random_arrival_gap();
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        sim->doctors[d].busyWithBed = -1;
    }
}

int sim_doctor_at(const Sim *sim, int bed)
{
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed == bed) return d;
    }
    return -1;
}

// Most urgent patient in a bed with no doctor (earliest arrival breaks ties),
// or -1 if every occupied bed already has one.
static int bed_needing_doctor(const Sim *sim)
{
    int best = -1;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || sim_doctor_at(sim, i) != -1) continue;

        if (best == -1) { best = i; continue; }
        const Patient *b = sim->beds[best].occupant;
        if (p->severity < b->severity ||
            (p->severity == b->severity && p->arrivalTime < b->arrivalTime)) {
            best = i;
        }
    }
    return best;
}

void sim_free(Sim *sim)
{
    Patient *p;
    while ((p = queue_pop(&sim->waiting)) != NULL) free(p);
    for (int i = 0; i < BED_COUNT; i++) {
        free(sim->beds[i].occupant);
        sim->beds[i].occupant = NULL;
    }
}

void sim_update(Sim *sim, float dt)
{
    sim->clock += dt;

    // 1. Arrivals
    while (sim->clock >= sim->nextArrival) {
        sim_add_patient(sim, random_severity());
        sim->nextArrival += random_arrival_gap();
    }

    // 2. Treatment: only beds with a doctor tick down.
    //    When treatment finishes, the bed and the doctor are both freed.
    for (int i = 0; i < BED_COUNT; i++) {
        Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;

        int d = sim_doctor_at(sim, i);
        if (d == -1) continue;          // in a bed, but waiting for a doctor

        p->treatmentLeft -= dt;
        if (p->treatmentLeft <= 0.0f) {
            free(p);
            sim->beds[i].occupant = NULL;
            sim->doctors[d].busyWithBed = -1;
            sim->treated++;
        }
    }

    // 3. Admission: every free bed takes the most urgent waiting patient
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) continue;

        Patient *p = queue_pop(&sim->waiting);
        if (p == NULL) break;

        sim->beds[i].occupant = p;
        sim->waitingCount--;
        sim->admitted++;
        sim->totalWait += sim->clock - p->arrivalTime;
    }

    // 4. Doctor rounds: every free doctor goes to the most urgent untreated bed
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed != -1) continue;

        int bed = bed_needing_doctor(sim);
        if (bed == -1) break;

        sim->doctors[d].busyWithBed = bed;
    }
}
