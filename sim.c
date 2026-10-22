// sim.c - arrivals, treatment, admission, nurse rounds and the shift clock.
// Each update: new patients arrive, treatment counts down in beds that have
// a nurse, every free bed takes the most urgent waiting patient, then every
// free nurse goes to the most urgent patient lying in a bed without one.
//
// After A&E, some patients are sent on to other rooms (X-ray, MRI, bloods,
// theatre, the ward, and the rooms upstairs), depending on their condition.
// Each room takes a few patients at a time; the rest queue for it. When
// they're done in their last room they go home.
//
// Equipment (monitors, ventilators, defibrillators, drip pumps, wheelchairs)
// is shared: a patient who needs something waits until one is free.
 
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

// Name, places, simulated seconds per patient (1 second = 4 minutes), and floor
static const struct { const char *name, *key; int capacity; float duration; int floor; } ROOM_INFO[ROOM_TYPES] = {
    [ROOM_XRAY]       = { "X-ray",      "xray",       1,  6.0f, 0 },   // 24 minutes on the table
    [ROOM_MRI]        = { "MRI",        "mri",        1, 10.0f, 0 },   // 40 minutes in the scanner
    [ROOM_BLOODS]     = { "Bloods",     "bloods",     2,  2.0f, 0 },   //  8 minutes
    [ROOM_THEATRE]    = { "Theatre",    "theatre",    1, 15.0f, 0 },   // an hour
    [ROOM_WARD]       = { "Ward",       "ward",       4, 30.0f, 0 },   // two hours on the ward
    [ROOM_CT]         = { "CT scan",    "ct",         1,  5.0f, 1 },   // 20 minutes
    [ROOM_ULTRASOUND] = { "Ultrasound", "ultrasound", 1,  4.0f, 1 },   // 16 minutes
    [ROOM_CARDIO]     = { "Cardiology", "cardio",     1, 12.0f, 1 },   // 48 minutes in the cath lab
    [ROOM_ICU]        = { "ICU",        "icu",        4, 30.0f, 1 },   // two hours in intensive care
    [ROOM_PHARMACY]   = { "Pharmacy",   "pharmacy",   2,  1.5f, 1 },   //  6 minutes to collect medicine
    [ROOM_MATERNITY]  = { "Maternity",  "maternity",  2, 25.0f, 2 },   // 100 minutes: a baby is born
    [ROOM_CHILDREN]   = { "Children",   "children",   3, 18.0f, 2 },   // 72 minutes on the children's ward
    [ROOM_PHYSIO]     = { "Physio",     "physio",     2,  6.0f, 2 },   // 24 minutes of exercises
    [ROOM_DIALYSIS]   = { "Dialysis",   "dialysis",   3, 16.0f, 2 },   // 64 minutes on the machine
};

// The equipment: name, the word for it in conditions.csv, and how many there are
static const struct { const char *name, *shortName, *key; int total; } EQUIP_INFO[EQUIP_TYPES] = {
    [EQUIP_MONITOR]    = { "Heart monitor", "Monitor",    "monitor",    4 },
    [EQUIP_VENTILATOR] = { "Ventilator",    "Ventilator", "ventilator", 4 },
    [EQUIP_DEFIB]      = { "Defibrillator", "Defib",      "defib",      1 },
    [EQUIP_DRIP]       = { "Drip pump",     "Drip pump",  "drip",       4 },
    [EQUIP_WHEELCHAIR] = { "Wheelchair",    "Wheelchair", "wheelchair", 2 },
};

static void rooms_init(Sim *sim)
{
    for (int r = 0; r < ROOM_TYPES; r++) {
        sim->rooms[r] = (SimRoom){ 0 };
        sim->rooms[r].name = ROOM_INFO[r].name;
        sim->rooms[r].capacity = ROOM_INFO[r].capacity;
        sim->rooms[r].duration = ROOM_INFO[r].duration;
    }
    for (int e = 0; e < EQUIP_TYPES; e++) {
        sim->equipment[e] = (Equipment){ EQUIP_INFO[e].name, EQUIP_INFO[e].key, EQUIP_INFO[e].total, 0 };
    }
}

int sim_room_floor(int room)
{
    return (room >= 0 && room < ROOM_TYPES) ? ROOM_INFO[room].floor : 0;
}

const char *sim_equipment_short(int e)
{
    return (e >= 0 && e < EQUIP_TYPES) ? EQUIP_INFO[e].shortName : "";
}

void sim_equipment_text(unsigned bits, char *out, int size)
{
    int used = 0;
    out[0] = '\0';
    for (int e = 0; e < EQUIP_TYPES; e++) {
        if (!(bits & EQUIP_BIT(e))) continue;
        int n = snprintf(out + used, (size_t)(size - used), "%s%s", used ? ", " : "", EQUIP_INFO[e].name);
        if (n < 0 || used + n >= size) break;
        used += n;
    }
}

// Read words joined with '+' (like "theatre+ward" or "monitor+drip"), and
// call back with the index of each one found in `keys`. Unknown words are skipped.
static int match_word(const char *s, const char *const *keys, int count)
{
    for (int i = 0; i < count; i++) {
        size_t len = strlen(keys[i]);
        // the whole word must match: "ct" mustn't match the start of "cardio"
        if (strncmp(s, keys[i], len) == 0 && (s[len] == '\0' || s[len] == '+' || s[len] == ' ')) return i;
    }
    return -1;
}

// Read a pathway like "theatre+ward" into the patient's route. Unknown words
// are skipped; "home" (or nothing) means no rooms at all.
static void set_route(Patient *p, const char *pathway)
{
    const char *keys[ROOM_TYPES];
    for (int r = 0; r < ROOM_TYPES; r++) keys[r] = ROOM_INFO[r].key;
    for (int i = 0; i < ROUTE_MAX; i++) p->route[i] = ROOM_NONE;
    p->routeStep = 0;
    int n = 0;
    const char *s = pathway;
    while (*s != '\0' && n < ROUTE_MAX) {
        int r = match_word(s, keys, ROOM_TYPES);
        if (r >= 0) p->route[n++] = r;
        const char *plus = strchr(s, '+');
        if (plus == NULL) break;
        s = plus + 1;
    }
}

// Read the equipment a condition needs, like "monitor+drip", into bits.
static unsigned equipment_needed(const char *list)
{
    const char *keys[EQUIP_TYPES];
    for (int e = 0; e < EQUIP_TYPES; e++) keys[e] = EQUIP_INFO[e].key;
    unsigned bits = 0;
    const char *s = list;
    while (*s != '\0') {
        int e = match_word(s, keys, EQUIP_TYPES);
        if (e >= 0) bits |= EQUIP_BIT(e);
        const char *plus = strchr(s, '+');
        if (plus == NULL) break;
        s = plus + 1;
    }
    return bits;
}

// Set the condition-dependent parts of a new patient: route and equipment.
static void set_condition(Patient *p, const Condition *c)
{
    p->condition = c;
    set_route(p, c->pathway);
    p->equipNeeds = equipment_needed(c->equipment);
}

void sim_route_text(const Patient *p, char *out, int size)
{
    if (p->route[0] == ROOM_NONE) { snprintf(out, (size_t)size, "Home"); return; }
    int used = 0;
    out[0] = '\0';
    for (int i = 0; i < ROUTE_MAX && p->route[i] != ROOM_NONE; i++) {
        int n = snprintf(out + used, (size_t)(size - used), "%s%s", i ? ", " : "", ROOM_INFO[p->route[i]].name);
        if (n < 0 || used + n >= size) break;
        used += n;
    }
}

// ---- Equipment ----

// Take every piece of equipment in `bits` for the patient, or none of it: if
// any of it isn't free, they get nothing and wait (so nobody holds half of
// what they need, and nobody can get stuck waiting on each other).
static bool take_equipment(Sim *sim, Patient *p, unsigned bits)
{
    unsigned missing = 0;
    for (int e = 0; e < EQUIP_TYPES; e++) {
        if ((bits & EQUIP_BIT(e)) && sim->equipment[e].inUse >= sim->equipment[e].total) missing |= EQUIP_BIT(e);
    }
    if (missing != 0) {
        // Count each patient once for each thing they end up waiting for
        for (int e = 0; e < EQUIP_TYPES; e++) {
            if ((missing & EQUIP_BIT(e)) && !(p->equipMissing & EQUIP_BIT(e))) sim->stats.equipWaits[e]++;
        }
        if (p->equipMissing == 0) p->equipWaitStart = sim->clock;
        p->equipMissing |= missing;
        return false;
    }
    for (int e = 0; e < EQUIP_TYPES; e++) {
        if (!(bits & EQUIP_BIT(e))) continue;
        sim->equipment[e].inUse++;
        if (sim->equipment[e].inUse > sim->stats.equipPeak[e]) sim->stats.equipPeak[e] = sim->equipment[e].inUse;
    }
    p->equipHeld |= bits;
    if (p->equipMissing != 0) {                  // they had been waiting
        float wait = sim->clock - p->equipWaitStart;
        for (int e = 0; e < EQUIP_TYPES; e++) {
            if (!(p->equipMissing & EQUIP_BIT(e))) continue;
            if (wait > sim->stats.equipLongestWait[e]) sim->stats.equipLongestWait[e] = wait;
        }
        p->equipMissing = 0;
        p->equipWaitStart = -1.0f;
    }
    return true;
}

// Give back the equipment in `bits` that the patient has.
static void give_back(Sim *sim, Patient *p, unsigned bits)
{
    for (int e = 0; e < EQUIP_TYPES; e++) {
        if ((bits & p->equipHeld & EQUIP_BIT(e)) && sim->equipment[e].inUse > 0) sim->equipment[e].inUse--;
    }
    p->equipHeld &= ~bits;
}

// The patient has got where they were walking to: the porter takes the wheelchair back.
static void arrived(Sim *sim, Patient *p)
{
    give_back(sim, p, EQUIP_BIT(EQUIP_WHEELCHAIR));
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
    int r = (p->routeStep < ROUTE_MAX) ? p->route[p->routeStep] : ROOM_NONE;
    if (r == ROOM_NONE) {
        give_back(sim, p, p->equipHeld);
        free(p);
        return;
    }
    p->walkLeft = WALK_TO_ROOM + WALK_PER_FLOOR * ROOM_INFO[r].floor;   // the walk there (and the lift up)
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
        int busy = 0;
        for (int s = 0; s < room->capacity; s++) {
            Patient *p = room->in[s];
            if (p == NULL) continue;
            busy++;
            if (p->walkLeft > 0.0f) {                                  // still walking there
                p->walkLeft -= dt;
                if (p->walkLeft <= 0.0f) arrived(sim, p);
                continue;
            }
            p->roomLeft -= dt;
            if (p->roomLeft > 0.0f) continue;
            room->in[s] = NULL;
            if (r == ROOM_ICU) give_back(sim, p, EQUIP_BIT(EQUIP_VENTILATOR));   // ICU's ventilator stays in ICU
            p->routeStep++;
            bool last = (p->routeStep >= ROUTE_MAX || p->route[p->routeStep] == ROOM_NONE);
            if (last) sim->stats.leftHospital++;
            send_on(sim, p);
        }
        sim->stats.roomBusyTime[r] += busy * dt;
        if (room->queueCount > sim->stats.roomLongestQueue[r]) sim->stats.roomLongestQueue[r] = room->queueCount;
        for (Patient *q = room->queue; q != NULL; q = q->next) {   // walking to the queue
            if (q->walkLeft > 0.0f) {
                q->walkLeft -= dt;
                if (q->walkLeft <= 0.0f) arrived(sim, q);
            }
        }
        for (int s = 0; s < room->capacity; s++) {
            if (room->in[s] != NULL) continue;
            if (room->queue == NULL) break;
            // Every bed in ICU needs a ventilator: no ventilator, no place
            if (r == ROOM_ICU && !take_equipment(sim, room->queue, EQUIP_BIT(EQUIP_VENTILATOR))) break;
            Patient *p = queue_pop(&room->queue);
            room->queueCount--;
            p->roomLeft = room->duration * (0.8f + (float)GetRandomValue(0, 40) / 100.0f);
            // The scan or stay starts once they get there: the rest of their walk,
            // or a step in through the door if they were already queuing outside
            if (p->walkLeft < 1.0f) p->walkLeft = 1.0f;
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
    p->rescue = false;
    p->rescueService = 0;
    p->roomLeft = 0.0f;
    p->walkLeft = 0.0f;
    p->equipHeld = 0;
    p->equipMissing = 0;
    p->equipWaitStart = -1.0f;
    p->doneInAE = false;
    set_condition(p, conditions_random(severity));
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
    set_condition(p, conditions_rescue(&service));
    p->rescueService = (int)service;
    p->treatmentTotal = 9.0f + (float)GetRandomValue(0, 20) / 10.0f;
    p->treatmentLeft = p->treatmentTotal;
    sim->stats.rescues++;
}
 
void sim_surgery_result(Sim *sim, int id, bool won)
{
    SimRoom *theatre = &sim->rooms[ROOM_THEATRE];
    for (int s = 0; s < theatre->capacity; s++) {
        Patient *p = theatre->in[s];
        if (p == NULL || p->id != id) continue;
        sim->stats.surgeriesPlayed++;
        if (won) {
            p->roomLeft *= 0.5f;
        } else {
            p->roomLeft *= 1.6f;
            sim->stats.complications++;
        }
        return;
    }
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
        if (p == NULL || p->needsDoctor || p->doneInAE || sim_nurse_at(sim, i) != -1) continue;
 
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
        if (p == NULL || !p->needsDoctor || p->doneInAE || sim_doctor_at(sim, i) != -1) continue;
 
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
 
// Equipment the patient still needs before treatment can start (the
// wheelchair is for leaving, so it isn't counted here).
static unsigned still_needs(const Patient *p)
{
    return p->equipNeeds & ~EQUIP_BIT(EQUIP_WHEELCHAIR) & ~p->equipHeld;
}

// Patients who can't walk need a wheelchair (and a porter) to get to their
// first room. Going home, their family takes them.
static bool needs_wheelchair_to_leave(const Patient *p)
{
    return (p->equipNeeds & EQUIP_BIT(EQUIP_WHEELCHAIR)) && p->route[0] != ROOM_NONE;
}

// Is someone with this patient, ready to treat them?
static bool staff_with(const Sim *sim, int bed, const Patient *p)
{
    return p->needsDoctor ? sim_doctor_at(sim, bed) != -1 : sim_nurse_at(sim, bed) != -1;
}

// Hand out equipment to patients who are ready for treatment (in their bed,
// assessed, with their nurse or doctor) but don't have what they need yet,
// most urgent first (earliest arrival breaks ties).
static void equipment_round(Sim *sim)
{
    int order[BED_COUNT], n = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || p->doneInAE || p->walkLeft > 0.0f || p->assessLeft > 0.0f) continue;
        if (!staff_with(sim, i, p) || still_needs(p) == 0) continue;
        // insertion sort into order[], most urgent first
        int k = n++;
        while (k > 0) {
            const Patient *q = sim->beds[order[k - 1]].occupant;
            if (q->severity < p->severity || (q->severity == p->severity && q->arrivalTime <= p->arrivalTime)) break;
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
    for (int k = 0; k < n; k++) {
        Patient *p = sim->beds[order[k]].occupant;
        take_equipment(sim, p, still_needs(p));
    }
}

// A patient leaves their A&E bed for their first room, or home.
static void leave_bed(Sim *sim, int bed)
{
    Patient *p = sim->beds[bed].occupant;
    sim->beds[bed].occupant = NULL;
    send_on(sim, p);
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
 
    // 2. Equipment for the patients who are ready to be treated
    equipment_round(sim);

        // 3. Assessment and treatment.
    //    A nurse assesses every patient first. If they need a doctor the nurse
    //    leaves and a doctor treats them; otherwise the nurse stays and treats
    //    them. Treatment only starts once they have the equipment they need.
    //    When it finishes, whoever treated them is freed and the equipment
    //    goes back; the bed is freed when they leave (which may mean waiting
    //    for a wheelchair).
    for (int i = 0; i < BED_COUNT; i++) {
        Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;

        if (p->doneInAE) {                  // treated: waiting for a wheelchair to take them on
            if (take_equipment(sim, p, EQUIP_BIT(EQUIP_WHEELCHAIR))) leave_bed(sim, i);
            continue;
        }
 
        int n = sim_nurse_at(sim, i);
        int d = sim_doctor_at(sim, i);
 
        if (p->walkLeft > 0.0f) {           // still walking to the bed: the nurse waits for them
            p->walkLeft -= dt;
            continue;
        }
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
        if (still_needs(p) != 0) continue;         // waiting for equipment
 
        p->treatmentLeft -= dt;
        if (p->treatmentLeft <= 0.0f) {
            sim->stats.treatedBySeverity[p->severity - 1]++;
 
            char who[TREATED_BY_LEN];
            if (p->needsDoctor) staff_label(who, sizeof who, true,  sim->doctors[d].name, d);
            else                staff_label(who, sizeof who, false, sim->nurses[n].name,  n);
            log_treated(sim, p, who);
 
            if (p->needsDoctor) sim->doctors[d].busyWithBed = -1;
            else                sim->nurses[n].busyWithBed = -1;
            sim->stats.treated++;
            give_back(sim, p, p->equipHeld);     // the equipment goes back for the next patient
            p->doneInAE = true;
            // On to their first room, or home; in a wheelchair if they need one
            if (needs_wheelchair_to_leave(p) && !take_equipment(sim, p, EQUIP_BIT(EQUIP_WHEELCHAIR))) continue;
            leave_bed(sim, i);
        }
    }
 
    // 3b. The rooms after A&E
    update_rooms(sim, dt);
 
    // 4. Admission: every free bed takes the most urgent waiting patient. The
    //    resus bay is tried first but only takes emergencies; the queue is
    //    sorted most urgent first, so if the front of it isn't an emergency
    //    nobody waiting is, and the resus bay stays free for the next one.
    for (int k = 0; k < BED_COUNT; k++) {
        int i = (k == 0) ? RESUS_BED : k - 1;
        if (sim->beds[i].occupant != NULL) continue;
        if (sim->waiting == NULL) break;
        if (i == RESUS_BED && sim->waiting->severity != 1) continue;
 
        Patient *p = queue_pop(&sim->waiting);
 
        sim->beds[i].occupant = p;
        p->walkLeft = WALK_TO_BED;           // nothing starts until they get to the bed
        sim->waitingCount--;
        sim->stats.admitted++;
        float wait = sim->clock - p->arrivalTime;
        p->waitedFor = wait;
        sim->stats.totalWait += wait;
        if (wait > sim->stats.longestWait) sim->stats.longestWait = wait;
    }
 
    // 5. Nurse rounds: every free nurse goes to the most urgent untreated bed
    for (int d = 0; d < NURSES_ON(sim->shift); d++) {
        if (sim->nurses[d].busyWithBed != -1) continue;
 
        int bed = bed_needing_nurse(sim);
        if (bed == -1) break;
 
        sim->nurses[d].busyWithBed = bed;
    }
 
        // 6. Doctor rounds: every free doctor goes to the most urgent patient
    //    a nurse has sent to a doctor
    for (int d = 0; d < DOCTORS_ON(sim->shift); d++) {
        if (sim->doctors[d].busyWithBed != -1) continue;
 
        int bed = bed_needing_doctor(sim);
        if (bed == -1) break;
 
        sim->doctors[d].busyWithBed = bed;
    }
 
    // 7. Resource usage for the end-of-shift report
    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant != NULL) sim->stats.bedBusyTime += dt;
    }
    for (int e = 0; e < EQUIP_TYPES; e++) {
        sim->stats.equipBusyTime[e] += sim->equipment[e].inUse * dt;
    }
    // Time spent waiting for equipment: patients in beds, and in the ICU queue
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL) continue;
        for (int e = 0; e < EQUIP_TYPES; e++) if (p->equipMissing & EQUIP_BIT(e)) sim->stats.equipWaitTotal[e] += dt;
    }
    for (const Patient *p = sim->rooms[ROOM_ICU].queue; p != NULL; p = p->next) {
        for (int e = 0; e < EQUIP_TYPES; e++) if (p->equipMissing & EQUIP_BIT(e)) sim->stats.equipWaitTotal[e] += dt;
    }
    for (int d = 0; d < NURSE_COUNT; d++) {
        if (sim->nurses[d].busyWithBed != -1) sim->stats.nurseBusyTime += dt;
    }
    for (int d = 0; d < DOCTOR_COUNT; d++) {
        if (sim->doctors[d].busyWithBed != -1) sim->stats.doctorBusyTime += dt;
    }
 
    if (sim->clock >= shift_end(sim)) end_shift(sim);
}