// sim.c - arrivals, treatment, admission, nurse rounds and the shift clock.
// Each update: new patients arrive, treatment counts down in beds that have
// a nurse, every free bed takes the most urgent waiting patient, then every
// free nurse goes to the most urgent patient lying in a bed without one.

#include <stdio.h>           // snprintf, printf
#include <stdlib.h>
#include "raylib.h"          // GetRandomValue
#include "sim.h"
#include "conditions.h"

static float random_arrival_gap(void)
{
    // 0.6 to 1.8 seconds. Was 0.3 to 1.5, but with 4 nurses that's more
    // work than they can keep up with and the queue grows forever.
    return (float)GetRandomValue(6, 20) / 10.0f; // changed 6 to 5 for testing queue growth
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
    p->assessLeft = ASSESS_TIME;
    p->waitedFor = 0.0f;
    p->needsDoctor = false;
    p->condition = conditions_random(severity);
    p->next = NULL;

    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
    sim->stats.arrived++;
}

void sim_init(Sim *sim, int day)
{
    *sim = (Sim){ 0 };
    sim->day = day;
    sim->shift = SHIFT_DAY;
    sim->nextId = 1;
    sim->nextArrival = random_arrival_gap();
    for (int d = 0; d < NURSE_COUNT; d++) {
        sim->nurses[d].busyWithBed = -1;
    }
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        sim->doctors[d].busyWithBed = -1;
    }
}

int sim_nurse_at(const Sim *sim, int bed)
{
    for (int d = 0; d < NURSE_COUNT; d++) {
        if (sim->nurses[d].busyWithBed == bed) return d;
    }
    return -1;
}

int sim_doctor_at(const Sim *sim, int bed)
{
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed == bed) return d;
    }
    return -1;
}

// The nurse's decision at the end of an assessment.
static bool needs_doctor(const Patient *p)
{
    if (p->severity == 1) return true;                          // emergency: always
    if (p->severity == 2) return GetRandomValue(1, 5) == 1;     // urgent: 1 in 5
    return GetRandomValue(1, 10) == 1;                          // routine: 1 in 10
}

float sim_minutes(float simSeconds)
{
    return simSeconds * (SHIFT_HOURS * 60.0f) / SHIFT_LENGTH;
}

void sim_time_of_day(const Sim *sim, int *hour, int *minute)
{
    int total = DAY_START_HOUR * 60 + (int)sim_minutes(sim->clock);
    *hour = (total / 60) % 24;
    *minute = total % 60;
}

// Most urgent patient in a bed with no nurse (earliest arrival breaks ties),
// or -1 if every occupied bed already has one.
static int bed_needing_nurse(const Sim *sim)
{
    int best = -1;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || p->needsDoctor || sim_nurse_at(sim, i) != -1) continue;

        if (best == -1) { best = i; continue; }
        const Patient *b = sim->beds[best].occupant;
        if (p->severity < b->severity ||
            (p->severity == b->severity && p->arrivalTime < b->arrivalTime)) {
            best = i;
        }
    }
    return best;
}

// Most urgent patient a nurse has sent to a doctor who doesn't have one yet,
// or -1 if there is nobody.
static int bed_needing_doctor(const Sim *sim)
{
    int best = -1;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || !p->needsDoctor || sim_doctor_at(sim, i) != -1) continue;

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

    free(sim->treatedLog);
    sim->treatedLog = NULL;
    sim->treatedLogCount = 0;
    sim->treatedLogCapacity = 0;
}

// Add a discharged patient to the treatment log. When the array is full it
// doubles in size with realloc (starting at 64 records). realloc's result goes
// into a temporary first: if it fails, the old array is still valid and still
// ours to free, so nothing leaks; that one record is just not logged.
static void log_treated(Sim *sim, const Patient *p, const char *treatedBy)
{
    if (sim->treatedLogCount == sim->treatedLogCapacity) {
        int newCapacity = (sim->treatedLogCapacity == 0) ? 64 : sim->treatedLogCapacity * 2;
        TreatedRecord *bigger = realloc(sim->treatedLog, (size_t)newCapacity * sizeof *bigger);
        if (bigger == NULL) return;
        sim->treatedLog = bigger;
        sim->treatedLogCapacity = newCapacity;
    }

    TreatedRecord *r = &sim->treatedLog[sim->treatedLogCount++];
    r->id = p->id;
    r->severity = p->severity;
    r->condition = p->condition;
    snprintf(r->treatedBy, sizeof r->treatedBy, "%s", treatedBy);
    r->sawDoctor = p->needsDoctor;
    r->arrivalTime = p->arrivalTime;
    r->wait = p->waitedFor;
    r->dischargeTime = sim->clock;
}

// Who treated the patient, as text: "Dr <name>" / "Nurse <name>", or
// "Dr 1" / "Nurse 3" while the staff have no names yet.
static void staff_label(char *out, size_t size, bool doctor, const char *name, int index)
{
    const char *role = doctor ? "Dr" : "Nurse";
    if (name != NULL) snprintf(out, size, "%s %s", role, name);
    else              snprintf(out, size, "%s %d", role, index + 1);
}

void sim_start_night(Sim *sim)
{
    sim->dayStats = sim->stats;
    sim->stats = (ShiftStats){ 0 };
    sim->treatedLogCount = 0;        // start a new log; keep the memory to reuse
    sim->shift = SHIFT_NIGHT;
    sim->shiftOver = false;
}

// Clock time at which the current shift ends: 19:00 or 07:00 next morning.
static float shift_end(const Sim *sim)
{
    return (sim->shift == SHIFT_DAY) ? SHIFT_LENGTH : 2.0f * SHIFT_LENGTH;
}

// Called once when the clock reaches the end of the shift.
static void end_shift(Sim *sim)
{
    sim->shiftOver = true;
    printf("Shift log: %d patients recorded (space for %d)\n",
           sim->treatedLogCount, sim->treatedLogCapacity);

    // Patients still waiting count towards the longest wait as well.
    for (const Patient *p = sim->waiting; p != NULL; p = p->next) {
        float wait = sim->clock - p->arrivalTime;
        if (wait > sim->stats.longestWait) sim->stats.longestWait = wait;
    }
}

void sim_update(Sim *sim, float dt)
{
    if (sim->shiftOver) return;

    // Never run past the end of the shift.
    if (sim->clock + dt >= shift_end(sim)) dt = shift_end(sim) - sim->clock;
    sim->clock += dt;

    // 1. Arrivals
    while (sim->clock >= sim->nextArrival) {
        sim_add_patient(sim, random_severity());
        sim->nextArrival += random_arrival_gap();
    }

        // 2. Assessment and treatment.
    //    A nurse assesses every patient first. If they need a doctor the nurse
    //    leaves and a doctor treats them; otherwise the nurse stays and treats
    //    them. When treatment finishes, the bed and whoever treated them are freed.
    for (int i = 0; i < BED_COUNT; i++) {
        Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;

        int n = sim_nurse_at(sim, i);
        int d = sim_doctor_at(sim, i);

        if (p->assessLeft > 0.0f) {
            if (n == -1) continue;          // in a bed, waiting for a nurse
            p->assessLeft -= dt;
            if (p->assessLeft <= 0.0f && needs_doctor(p)) {
                p->needsDoctor = true;      // hand over to a doctor
                sim->nurses[n].busyWithBed = -1;
                sim->stats.sawDoctor++;
            }
            continue;
        }

        if (p->needsDoctor && d == -1) continue;   // waiting for a doctor
        if (!p->needsDoctor && n == -1) continue;  // guard: nurse-treated needs a nurse

        p->treatmentLeft -= dt;
        if (p->treatmentLeft <= 0.0f) {
            sim->stats.treatedBySeverity[p->severity - 1]++;

            char who[TREATED_BY_LEN];
            if (p->needsDoctor) staff_label(who, sizeof who, true,  sim->doctors[d].name, d);
            else                staff_label(who, sizeof who, false, sim->nurses[n].name,  n);
            log_treated(sim, p, who);

            if (p->needsDoctor) sim->doctors[d].busyWithBed = -1;
            else                sim->nurses[n].busyWithBed = -1;
            free(p);
            sim->beds[i].occupant = NULL;
            sim->stats.treated++;
        }
    }

    // 3. Admission: every free bed takes the most urgent waiting patient
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) continue;

        Patient *p = queue_pop(&sim->waiting);
        if (p == NULL) break;

        sim->beds[i].occupant = p;
        sim->waitingCount--;
        sim->stats.admitted++;
        float wait = sim->clock - p->arrivalTime;
        p->waitedFor = wait;
        sim->stats.totalWait += wait;
        if (wait > sim->stats.longestWait) sim->stats.longestWait = wait;
    }

    // 4. Nurse rounds: every free nurse goes to the most urgent untreated bed
    for (int d = 0; d < NURSE_COUNT; d++) {
        if (sim->nurses[d].busyWithBed != -1) continue;

        int bed = bed_needing_nurse(sim);
        if (bed == -1) break;

        sim->nurses[d].busyWithBed = bed;
    }

        // 5. Doctor rounds: every free doctor goes to the most urgent patient
    //    a nurse has sent to a doctor
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed != -1) continue;

        int bed = bed_needing_doctor(sim);
        if (bed == -1) break;

        sim->doctors[d].busyWithBed = bed;
    }

    // 6. Resource usage for the end-of-shift report
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) sim->stats.bedBusyTime += dt;
    }
    for (int d = 0; d < NURSE_COUNT; d++) {
        if (sim->nurses[d].busyWithBed != -1) sim->stats.nurseBusyTime += dt;
    }
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed != -1) sim->stats.doctorBusyTime += dt;
    }

    if (sim->clock >= shift_end(sim)) end_shift(sim);
}