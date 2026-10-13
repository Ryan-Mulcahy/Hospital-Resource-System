// sim.c - arrivals, treatment and admission.
// Each update: new patients arrive, treatment counts down in occupied beds,
// then every free bed takes the most urgent waiting patient.

#include <stdlib.h>
#include "raylib.h"          // GetRandomValue
#include "sim.h"

static float random_arrival_gap(void)
{
    return (float)GetRandomValue(3, 15) / 10.0f;      // 0.3 to 1.5 seconds
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

    // 2. Treatment: tick each occupied bed, discharge when finished
    for (int i = 0; i < BED_COUNT; i++) {
        Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;

        p->treatmentLeft -= dt;
        if (p->treatmentLeft <= 0.0f) {
            free(p);
            sim->beds[i].occupant = NULL;
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
}