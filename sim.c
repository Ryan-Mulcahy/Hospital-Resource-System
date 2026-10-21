// sim.c - arrivals, treatment, admission, nurse rounds and the shift clock.
// Each update: new patients arrive, treatment counts down in beds that have
// a nurse, every free bed takes the most urgent waiting patient, then every
// free nurse goes to the most urgent patient lying in a bed without one.
//
// After A&E, some patients are sent on to other rooms (X-ray, MRI, bloods,
// theatre, the ward), depending on their condition. Each room takes a few
// patients at a time; the rest queue for it. When they're done in their last
// room they go home.
 
#include <stdio.h>           // snprintf, printf
#include <stdlib.h>
#include <string.h>          // strncmp
#include "raylib.h"          // GetRandomValue
#include "sim.h"
#include "roster.h"
#include "conditions.h"
 
static float random_arrival_gap(const Sim *sim)
{
    // Day: 0.6 to 2.0 seconds. Night: NIGHT_GAP_MIN to NIGHT_GAP_MAX, because
    // fewer patients come in at night and only 1 doctor is on.
    if (sim->shift == SHIFT_NIGHT) return (float)GetRandomValue(NIGHT_GAP_MIN, NIGHT_GAP_MAX) / 10.0f;
    return (float)GetRandomValue(6, 20) / 10.0f;
}
 
static int random_severity(void)
{
    int roll = GetRandomValue(1, 10);
    if (roll <= 2) return 1;      // 20% emergency
    if (roll <= 5) return 2;      // 30% urgent
    return 3;                     // 50% routine
}
 
// ---- Rooms after A&E ----

// Name, places, and simulated seconds per patient (1 second = 4 minutes)
static const struct { const char *name, *key; int capacity; float duration; } ROOM_INFO[ROOM_TYPES] = {
    [ROOM_XRAY]    = { "X-ray",   "xray",    1,  3.0f },   // 12 minutes
    [ROOM_MRI]     = { "MRI",     "mri",     1,  7.0f },   // about half an hour
    [ROOM_BLOODS]  = { "Bloods",  "bloods",  2,  2.0f },   //  8 minutes
    [ROOM_THEATRE] = { "Theatre", "theatre", 1, 15.0f },   // an hour
    [ROOM_WARD]    = { "Ward",    "ward",    4, 30.0f },   // two hours on the ward
};

static void rooms_init(Sim *sim)
{
    for (int r = 0; r < ROOM_TYPES; r++) {
        sim->rooms[r] = (SimRoom){ 0 };
        sim->rooms[r].name = ROOM_INFO[r].name;
        sim->rooms[r].capacity = ROOM_INFO[r].capacity;
        sim->rooms[r].duration = ROOM_INFO[r].duration;
    }
}

// Read a pathway like "theatre+ward" into the patient's route. Unknown words
// are skipped; "home" (or nothing) means no rooms at all.
static void set_route(Patient *p, const char *pathway)
{
    p->route[0] = p->route[1] = ROOM_NONE;
    p->routeStep = 0;
    int n = 0;
    const char *s = pathway;
    while (*s != '\0' && n < 2) {
        for (int r = 0; r < ROOM_TYPES; r++) {
            size_t len = strlen(ROOM_INFO[r].key);
            if (strncmp(s, ROOM_INFO[r].key, len) == 0) { p->route[n++] = r; break; }
        }
        const char *plus = strchr(s, '+');
        if (plus == NULL) break;
        s = plus + 1;
    }
}

void sim_route_text(const Patient *p, char *out, int size)
{
    if (p->route[0] == ROOM_NONE) { snprintf(out, (size_t)size, "Home"); return; }
    if (p->route[1] == ROOM_NONE) snprintf(out, (size_t)size, "%s", ROOM_INFO[p->route[0]].name);
    else snprintf(out, (size_t)size, "%s, %s", ROOM_INFO[p->route[0]].name, ROOM_INFO[p->route[1]].name);
}

int sim_patients_in_rooms(const Sim *sim)
{
    int n = 0;
    for (int r = 0; r < ROOM_TYPES; r++) {
        n += sim->rooms[r].queueCount;
        for (int s = 0; s < sim->rooms[r].capacity; s++) if (sim->rooms[r].in[s]) n++;
    }
    return n;
}

// Send a patient on to the next room on their route, or home (freed).
static void send_on(Sim *sim, Patient *p)
{
    int r = (p->routeStep < 2) ? p->route[p->routeStep] : ROOM_NONE;
    if (r == ROOM_NONE) {
        free(p);
        return;
    }
    queue_insert(&sim->rooms[r].queue, p);   // most urgent first
    sim->rooms[r].queueCount++;
    sim->stats.sentTo[r]++;
}

// Time passes in every room: finished patients move on, then free places
// take the next patient from the room's queue.
static void update_rooms(Sim *sim, float dt)
{
    for (int r = 0; r < ROOM_TYPES; r++) {
        SimRoom *room = &sim->rooms[r];
        for (int s = 0; s < room->capacity; s++) {
            Patient *p = room->in[s];
            if (p == NULL) continue;
            p->roomLeft -= dt;
            if (p->roomLeft > 0.0f) continue;
            room->in[s] = NULL;
            p->routeStep++;
            bool last = (p->routeStep >= 2 || p->route[p->routeStep] == ROOM_NONE);
            if (last) sim->stats.leftHospital++;
            send_on(sim, p);
        }
        for (int s = 0; s < room->capacity; s++) {
            if (room->in[s] != NULL) continue;
            Patient *p = queue_pop(&room->queue);
            if (p == NULL) break;
            room->queueCount--;
            p->roomLeft = room->duration * (0.8f + (float)GetRandomValue(0, 40) / 100.0f);
            room->in[s] = p;
        }
    }
}

// The next helicopter rescue: somewhere between `from` and `to` (fractions of
// the way through the shift).
static void schedule_rescue(Sim *sim, float from, float to)
{
    float start = (sim->shift == SHIFT_NIGHT) ? SHIFT_LENGTH : 0.0f;
    float k = from + (to - from) * (float)GetRandomValue(0, 100) / 100.0f;
    sim->nextRescue = start + k * SHIFT_LENGTH;
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
    p->rescue = false;
    p->rescueService = 0;
    p->roomLeft = 0.0f;
    set_route(p, p->condition->pathway);
    p->next = NULL;
 
    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
    sim->stats.arrived++;
}

void sim_add_rescue(Sim *sim)
{
    sim_add_patient(sim, 1);
    // Find them (the newest patient) and move them to the very front of the queue
    Patient **link = &sim->waiting;
    while (*link != NULL && (*link)->id != sim->nextId - 1) link = &(*link)->next;
    Patient *p = *link;
    if (p == NULL) return;
    *link = p->next;
    p->next = sim->waiting;
    sim->waiting = p;

    p->rescue = true;
    RescueService service;
    p->condition = conditions_rescue(&service);
    p->rescueService = (int)service;
    set_route(p, p->condition->pathway);
    p->treatmentTotal = 9.0f + (float)GetRandomValue(0, 20) / 10.0f;
    p->treatmentLeft = p->treatmentTotal;
    sim->stats.rescues++;
}
 
void sim_init(Sim *sim, int day)
{
    *sim = (Sim){ 0 };
    sim->day = day;
    sim->shift = SHIFT_DAY;
    sim->nextId = 1;
    sim->nextArrival = random_arrival_gap(sim);
    rooms_init(sim);
    schedule_rescue(sim, 0.10f, 0.60f);
    for (int d = 0; d < NURSE_COUNT; d++) {
        sim->nurses[d].busyWithBed = -1;
    }
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        sim->doctors[d].busyWithBed = -1;
    }
    roster_assign(sim);      // day shift staff
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
 
    for (int r = 0; r < ROOM_TYPES; r++) {
        SimRoom *room = &sim->rooms[r];
        for (int s = 0; s < ROOM_SLOTS_MAX; s++) {
            free(room->in[s]);
            room->in[s] = NULL;
        }
        while ((p = queue_pop(&room->queue)) != NULL) free(p);
        room->queueCount = 0;
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
    r->rescue = p->rescue;
    r->rescueService = p->rescueService;
    sim_route_text(p, r->sentTo, (int)sizeof r->sentTo);
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
    schedule_rescue(sim, 0.10f, 0.60f);
 
    // Staff who aren't on nights go home. Their patients keep their bed and
    // their progress, and the night staff pick them up.
    for (int d = NURSES_ON(SHIFT_NIGHT); d < NURSE_COUNT; d++)   sim->nurses[d].busyWithBed = -1;
    for (int d = DOCTORS_ON(SHIFT_NIGHT); d < DOCTOR_COUNT; d++) sim->doctors[d].busyWithBed = -1;
 
    roster_assign(sim);      // night shift staff
}
 
// Clock time at which the current shift ends: 19:00 or 07:00 next morning.
static float shift_end(const Sim *sim)
{
    return (sim->shift == SHIFT_DAY) ? SHIFT_LENGTH : 2.0f * SHIFT_LENGTH;
}
 
// A clock value as a time of day, e.g. "18:52". out must hold at least 6 chars.
static void clock_to_hhmm(float clock, char *out, size_t size)
{
    int total = DAY_START_HOUR * 60 + (int)sim_minutes(clock);
    snprintf(out, size, "%02d:%02d", (total / 60) % 24, total % 60);
}
 
static const char *severity_name(int severity)
{
    if (severity == 1) return "Emergency";
    if (severity == 2) return "Urgent";
    return "Routine";
}
 
bool sim_save_log_csv(const Sim *sim, const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        printf("Treatment log: could not write %s\n", filename);
        return false;
    }
 
    fprintf(f, "patient,severity,severity_name,condition,icd10,arrived,"
               "waited_min,discharged,minutes_in_ed,treated_by,saw_doctor,sent_to,helicopter\n");
 
    for (int i = 0; i < sim->treatedLogCount; i++) {
        const TreatedRecord *r = &sim->treatedLog[i];
        char arrived[8], discharged[8];
        clock_to_hhmm(r->arrivalTime,   arrived,    sizeof arrived);
        clock_to_hhmm(r->dischargeTime, discharged, sizeof discharged);
 
        // Text fields are quoted so a comma in a name can't break the columns.
        fprintf(f, "%d,%d,%s,\"%s\",%s,%s,%.0f,%s,%.0f,\"%s\",%s,\"%s\",%s\n",
                r->id, r->severity, severity_name(r->severity),
                r->condition->name, r->condition->icd10,
                arrived, sim_minutes(r->wait), discharged,
                sim_minutes(r->dischargeTime - r->arrivalTime),
                r->treatedBy, r->sawDoctor ? "yes" : "no",
                r->sentTo, r->rescue ? rescue_service_name((RescueService)r->rescueService) : "no");
    }
 
    bool ok = !ferror(f);          // did any write fail (e.g. disk full)?
    if (fclose(f) != 0) ok = false;
    if (ok) printf("Treatment log: saved %d patients to %s\n", sim->treatedLogCount, filename);
    else    printf("Treatment log: error while writing %s\n", filename);
    return ok;
}
 
// Called once when the clock reaches the end of the shift.
static void end_shift(Sim *sim)
{
    sim->shiftOver = true;
 
    char filename[64];
    snprintf(filename, sizeof filename, "log_day%d_%s.csv",
             sim->day, sim->shift == SHIFT_DAY ? "day" : "night");
    sim_save_log_csv(sim, filename);
 
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
 
    // 1. Arrivals, and now and then a helicopter rescue
    while (sim->clock >= sim->nextArrival) {
        sim_add_patient(sim, random_severity());
        sim->nextArrival += random_arrival_gap(sim);
    }
    if (sim->nextRescue >= 0.0f && sim->clock >= sim->nextRescue) {
        sim_add_rescue(sim);
        // Maybe a second one later in the shift (never more than 2 a shift)
        if (sim->stats.rescues < 2 && GetRandomValue(1, 100) <= RESCUE_CHANCE_SECOND) {
            float start = (sim->shift == SHIFT_NIGHT) ? SHIFT_LENGTH : 0.0f;
            float done = (sim->clock - start) / SHIFT_LENGTH;
            schedule_rescue(sim, done + 0.10f, 0.95f);
            if (sim->nextRescue >= start + 0.95f * SHIFT_LENGTH) sim->nextRescue = -1.0f;
        } else {
            sim->nextRescue = -1.0f;
        }
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
        if (!p->needsDoctor && n == -1) continue;  // waiting for a nurse (theirs went home)
 
        p->treatmentLeft -= dt;
        if (p->treatmentLeft <= 0.0f) {
            sim->stats.treatedBySeverity[p->severity - 1]++;
 
            char who[TREATED_BY_LEN];
            if (p->needsDoctor) staff_label(who, sizeof who, true,  sim->doctors[d].name, d);
            else                staff_label(who, sizeof who, false, sim->nurses[n].name,  n);
            log_treated(sim, p, who);
 
            if (p->needsDoctor) sim->doctors[d].busyWithBed = -1;
            else                sim->nurses[n].busyWithBed = -1;
            sim->beds[i].occupant = NULL;
            sim->stats.treated++;
            send_on(sim, p);             // on to their first room, or home
        }
    }
 
    // 2b. The rooms after A&E
    update_rooms(sim, dt);
 
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
    for (int d = 0; d < NURSES_ON(sim->shift); d++) {
        if (sim->nurses[d].busyWithBed != -1) continue;
 
        int bed = bed_needing_nurse(sim);
        if (bed == -1) break;
 
        sim->nurses[d].busyWithBed = bed;
    }
 
        // 5. Doctor rounds: every free doctor goes to the most urgent patient
    //    a nurse has sent to a doctor
    for (int d = 0; d < DOCTORS_ON(sim->shift); d++) {
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