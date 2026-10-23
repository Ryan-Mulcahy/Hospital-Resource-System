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
 
#include <math.h>            // fmodf
#include <stdio.h>           // snprintf, printf
#include <stdlib.h>
#include <string.h>          // strncmp
#include "raylib.h"          // GetRandomValue
#include "sim.h"
#include "roster.h"
#include "conditions.h"
 
static float random_arrival_gap(const Sim *sim)
{
    // Flu season: about half as many patients again, day and night
    if (sim->setup.scenario == SCENARIO_FLU) {
        if (sim->shift == SHIFT_NIGHT) return (float)GetRandomValue(NIGHT_GAP_MIN, NIGHT_GAP_MAX) / 10.0f * 0.65f;
        return (float)GetRandomValue(6, 20) / 10.0f * 0.65f;
    }
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
    [ROOM_CARDIO]     = { "Cardiology", "cardio",     1,  8.0f, 1 },   // 32 minutes in the cath lab
    [ROOM_ICU]        = { "ICU",        "icu",        4, 24.0f, 1 },   // 96 minutes in intensive care
    [ROOM_PHARMACY]   = { "Pharmacy",   "pharmacy",   2,  1.5f, 1 },   //  6 minutes to collect medicine
    [ROOM_MATERNITY]  = { "Maternity",  "maternity",  2, 25.0f, 2 },   // 100 minutes: a baby is born
    [ROOM_CHILDREN]   = { "Children",   "children",   3, 18.0f, 2 },   // 72 minutes on the children's ward
    [ROOM_PHYSIO]     = { "Physio",     "physio",     2,  6.0f, 2 },   // 24 minutes of exercises
    [ROOM_DIALYSIS]   = { "Dialysis",   "dialysis",   3, 16.0f, 2 },   // 64 minutes on the machine
};

// The equipment: name, the word for it in conditions.csv, and how many there are
static const struct { const char *name, *shortName, *key; int total; } EQUIP_INFO[EQUIP_TYPES] = {
    [EQUIP_MONITOR]    = { "Heart monitor", "Monitor",    "monitor",    4 },
    [EQUIP_VENTILATOR] = { "Ventilator",    "Ventilator", "ventilator", 6 },
    [EQUIP_DEFIB]      = { "Defibrillator", "Defib",      "defib",      1 },
    [EQUIP_DRIP]       = { "Drip pump",     "Drip pump",  "drip",       4 },
    [EQUIP_WHEELCHAIR] = { "Wheelchair",    "Wheelchair", "wheelchair", 4 },
};

static void rooms_init(Sim *sim)
{
    for (int r = 0; r < ROOM_TYPES; r++) {
        sim->rooms[r] = (SimRoom){ 0 };
        sim->rooms[r].name = ROOM_INFO[r].name;
        sim->rooms[r].capacity = ROOM_INFO[r].capacity;
        sim->rooms[r].duration = ROOM_INFO[r].duration;
    }
    for (int e = 0; e < EQUIP_TYPES; e++) {      // as many of each as the day's setup says
        sim->equipment[e] = (Equipment){ EQUIP_INFO[e].name, EQUIP_INFO[e].key, sim->setup.equipment[e], 0 };
    }
}

void sim_default_setup(Setup *setup)
{
    *setup = (Setup){ 0 };
    setup->scenario = SCENARIO_NORMAL;
    setup->nurses[SHIFT_DAY] = 4;    setup->nurses[SHIFT_NIGHT] = 2;
    setup->doctors[SHIFT_DAY] = 2;   setup->doctors[SHIFT_NIGHT] = 1;
    setup->beds = BED_COUNT - 1;
    for (int e = 0; e < EQUIP_TYPES; e++) setup->equipment[e] = EQUIP_INFO[e].total;
    setup->powerCuts = true;
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void sim_clamp_setup(Setup *setup)
{
    if ((int)setup->scenario < 0 || setup->scenario >= SCENARIO_COUNT) setup->scenario = SCENARIO_NORMAL;
    for (int sh = 0; sh < 2; sh++) {
        setup->nurses[sh]  = clamp_int(setup->nurses[sh],  SETUP_NURSES_MIN,  NURSE_MAX);
        setup->doctors[sh] = clamp_int(setup->doctors[sh], SETUP_DOCTORS_MIN, DOCTOR_MAX);
    }
    setup->beds = clamp_int(setup->beds, SETUP_BEDS_MIN, BED_COUNT - 1);
    for (int e = 0; e < EQUIP_TYPES; e++) setup->equipment[e] = clamp_int(setup->equipment[e], 1, EQUIP_MAX_EACH);
}

int sim_nurses_on(const Sim *sim)     { return sim->setup.nurses[sim->shift]; }
int sim_shift_doctors(const Sim *sim) { return sim->setup.doctors[sim->shift]; }

bool sim_bed_open(const Sim *sim, int bed)
{
    return bed == RESUS_BED || (bed >= 0 && bed < sim->setup.beds);
}

static Patient *new_patient(Sim *sim, int severity);         // (further down)
static void set_condition(Patient *p, const Condition *c);

float sim_shift_time(const Sim *sim)
{
    return sim->clock - (sim->shift == SHIFT_NIGHT ? SHIFT_LENGTH : 0.0f);
}

const char *sim_scenario_name(Scenario s)
{
    switch (s) {
    case SCENARIO_FLU:            return "Flu season";
    case SCENARIO_MAJOR_INCIDENT: return "Major incident";
    default:                      return "Normal day";
    }
}

const char *sim_event_name(EventType e)
{
    switch (e) {
    case EVENT_HELI_LANDED: return "Helicopter landed";
    case EVENT_ON_CALL:     return "On-call doctor in";
    case EVENT_POWER_OFF:   return "Power off";
    case EVENT_POWER_ON:    return "Power back";
    case EVENT_INCIDENT:    return "Major incident";
    default:                return "";
    }
}

// Note that something happened now, for the graphs on the report
static void add_event(Sim *sim, EventType type)
{
    if (sim->stats.eventCount < MAX_EVENTS)
        sim->stats.events[sim->stats.eventCount++] = (ShiftEvent){ sim_shift_time(sim), type };
}

// ---- Power cuts ----

// Maybe a power cut by itself some time this shift (if they're switched on)
static void schedule_power_cut(Sim *sim)
{
    sim->nextPowerCut = -1.0f;
    if (!sim->setup.powerCuts || GetRandomValue(1, 100) > POWER_CUT_CHANCE) return;
    float start = (sim->shift == SHIFT_NIGHT) ? SHIFT_LENGTH : 0.0f;
    sim->nextPowerCut = start + SHIFT_LENGTH * (float)GetRandomValue(15, 85) / 100.0f;
}

static bool is_scanner(int room) { return room == ROOM_XRAY || room == ROOM_CT || room == ROOM_MRI; }

void sim_power_cut(Sim *sim)
{
    if (sim->power == POWER_OUT || sim->shiftOver) return;
    sim->power = POWER_OUT;
    sim->powerTimer = GENERATOR_START;
    sim->generatorStuck = GetRandomValue(1, 100) <= GENERATOR_FAIL_CHANCE;
    sim->stats.powerCuts++;
    add_event(sim, EVENT_POWER_OFF);
    // Who's caught by it: in a scanner, or on their way up or down in the lift
    for (int r = 0; r < ROOM_TYPES; r++) {
        const SimRoom *room = &sim->rooms[r];
        for (int s = 0; s < room->capacity; s++) {
            const Patient *p = room->in[s];
            if (p == NULL) continue;
            if (is_scanner(r) && p->walkLeft <= 0.0f && p->roomLeft > 0.0f) sim->stats.scansHeld++;
            if (sim_room_floor(r) > 0 && p->walkLeft > 0.0f) sim->stats.liftStuck++;
        }
        if (sim_room_floor(r) > 0)
            for (const Patient *q = room->queue; q != NULL; q = q->next) if (q->walkLeft > 0.0f) sim->stats.liftStuck++;
    }
}

bool sim_generator_needed(const Sim *sim)
{
    return sim->power == POWER_OUT && sim->generatorStuck && !sim->shiftOver;
}

void sim_generator_result(Sim *sim, bool won)
{
    if (!sim_generator_needed(sim)) return;
    sim->generatorStuck = false;
    sim->stats.generatorGames++;
    if (won) {
        sim->powerTimer = 0.3f;              // it starts straight away
    } else {
        sim->powerTimer = GENERATOR_FIX;     // the electricians will have to fix it
        sim->stats.generatorFailed++;
    }
}

float sim_lights(const Sim *sim)
{
    if (sim->power == POWER_OUT) return 0.0f;
    if (sim->powerBackAt >= 0.0f && sim->clock - sim->powerBackAt < 0.8f) {      // flickering back on
        return fmodf((sim->clock - sim->powerBackAt) * 9.0f, 1.0f) < 0.5f ? 1.0f : 0.25f;
    }
    return 1.0f;
}

// Is a walk to this room held up by the power cut? (The lifts are off.)
static bool walk_stopped(const Sim *sim, int room)
{
    return sim->power == POWER_OUT && sim_room_floor(room) > 0;
}

// ---- A major incident ----
// The casualties of a bus crash: injuries, mostly bad ones. (Falls back to
// any condition of that severity if conditions.csv doesn't have these.)
static void add_casualty(Sim *sim)
{
    static const char *const BAD[] = { "Head injury (car crash)", "Internal bleeding" };
    static const char *const MID[] = { "Broken hip", "Broken wrist", "Chest pain" };
    static const char *const MINOR[] = { "Cut hand", "Ankle sprain", "Sprained knee", "Back pain" };
    int roll = GetRandomValue(1, 100);
    int severity = roll <= 50 ? 1 : roll <= 85 ? 2 : 3;
    const char *name = severity == 1 ? BAD[GetRandomValue(0, 1)] : severity == 2 ? MID[GetRandomValue(0, 2)] : MINOR[GetRandomValue(0, 3)];
    Patient *p = new_patient(sim, severity);
    if (p == NULL) return;
    const Condition *c = conditions_find(name);
    if (c != NULL && c->severity == severity) set_condition(p, c);
    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
    sim->stats.arrived++;
    sim->stats.incidentCasualties++;
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

// Send a patient on to the next room on their route, or home. `fromFloor` is
// the floor they're on now (for how long the walk takes, with the lift).
// Going home they're freed, unless they're in a wheelchair: then the porter
// pushes them out to the front door first (sim->leaving).
static void send_on(Sim *sim, Patient *p, int fromFloor)
{
    int r = (p->routeStep < ROUTE_MAX) ? p->route[p->routeStep] : ROOM_NONE;
    if (r == ROOM_NONE) {
        if (p->equipHeld & EQUIP_BIT(EQUIP_WHEELCHAIR)) {
            give_back(sim, p, p->equipHeld & ~EQUIP_BIT(EQUIP_WHEELCHAIR));
            p->walkLeft = WALK_OUT + WALK_PER_FLOOR * fromFloor;
            p->next = sim->leaving;
            sim->leaving = p;
            return;
        }
        give_back(sim, p, p->equipHeld);
        free(p);
        return;
    }
    p->walkLeft = WALK_TO_ROOM + WALK_PER_FLOOR * (fromFloor + ROOM_INFO[r].floor);   // the walk there (and the lift)
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
                if (walk_stopped(sim, r)) continue;                    // (stuck in the lift: power cut)
                p->walkLeft -= dt;
                if (p->walkLeft <= 0.0f) arrived(sim, p);
                continue;
            }
            if (sim->power == POWER_OUT && is_scanner(r)) continue;   // the scanner's off: they wait in it
            p->roomLeft -= dt;
            if (p->roomLeft > 0.0f) continue;
            p->roomLeft = 0.0f;
            // Done here. Patients who can't walk wait for a wheelchair (and a
            // porter) before they move on, keeping their place meanwhile.
            if ((p->equipNeeds & EQUIP_BIT(EQUIP_WHEELCHAIR)) && !take_equipment(sim, p, EQUIP_BIT(EQUIP_WHEELCHAIR))) continue;
            room->in[s] = NULL;
            room->ready[s] = ROOM_TURNOVER;                        // cleaned and set up for the next one
            if (r == ROOM_ICU) give_back(sim, p, EQUIP_BIT(EQUIP_VENTILATOR));   // ICU's ventilator stays in ICU
            p->routeStep++;
            bool last = (p->routeStep >= ROUTE_MAX || p->route[p->routeStep] == ROOM_NONE);
            if (last) sim->stats.leftHospital++;
            send_on(sim, p, ROOM_INFO[r].floor);
        }
        sim->stats.roomBusyTime[r] += busy * dt;
        if (room->queueCount > sim->stats.roomLongestQueue[r]) sim->stats.roomLongestQueue[r] = room->queueCount;
        for (Patient *q = room->queue; q != NULL; q = q->next) {   // walking to the queue
            if (q->walkLeft > 0.0f && !walk_stopped(sim, r)) {
                q->walkLeft -= dt;
                if (q->walkLeft <= 0.0f) arrived(sim, q);
            }
        }
        for (int s = 0; s < room->capacity; s++) {
            if (room->ready[s] > 0.0f) room->ready[s] -= dt;
        }
        for (int s = 0; s < room->capacity; s++) {
            if (room->in[s] != NULL || room->ready[s] > 0.0f) continue;   // taken, or not ready yet
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

// Patients being pushed out to the front door: when they get there, the
// porter takes the wheelchair back and they go home.
static void update_leaving(Sim *sim, float dt)
{
    Patient **link = &sim->leaving;
    while (*link != NULL) {
        Patient *p = *link;
        p->walkLeft -= dt;
        if (p->walkLeft > 0.0f) { link = &p->next; continue; }
        *link = p->next;
        give_back(sim, p, p->equipHeld);
        free(p);
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

// A new patient with a random condition of this severity (not in any list yet).
static Patient *new_patient(Sim *sim, int severity)
{
    Patient *p = malloc(sizeof(Patient));
    if (p == NULL) return NULL;
 
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
    return p;
}

void sim_add_patient(Sim *sim, int severity)
{
    Patient *p = new_patient(sim, severity);
    if (p == NULL) return;
    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
    sim->stats.arrived++;
}

// A rescue is called in: the patient is in the air until the helicopter
// lands (one at a time on the helipad), then they're taken in.
void sim_add_rescue(Sim *sim)
{
    Patient *p = new_patient(sim, 1);
    if (p == NULL) return;
    float land = sim->clock + HELI_FLIGHT;
    if (land < sim->lastLanding + HELI_PAD_GAP) land = sim->lastLanding + HELI_PAD_GAP;
    sim->lastLanding = land;
    p->roomLeft = land - sim->clock;            // flight time left
    Patient **link = &sim->inbound;             // at the end: they land in order
    while (*link != NULL) link = &(*link)->next;
    *link = p;

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

int sim_inbound_count(const Sim *sim)
{
    int n = 0;
    for (const Patient *p = sim->inbound; p != NULL; p = p->next) n++;
    return n;
}

int sim_waiting_for_doctor(const Sim *sim)
{
    int n = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p && p->needsDoctor && !p->doneInAE && sim_doctor_at(sim, i) == -1) n++;
    }
    return n;
}

int sim_doctors_on(const Sim *sim)
{
    return sim_shift_doctors(sim) + (sim->onCall ? 1 : 0);
}

// Rescues in the air: when one lands they go to the front of the queue
// (behind any rescue that landed before them), and are wheeled in from the
// helipad, which takes HELI_WALK_IN on top of the usual walk to a bed.
static void update_inbound(Sim *sim, float dt)
{
    while (sim->inbound != NULL) {
        Patient *p = sim->inbound;
        p->roomLeft -= dt;
        if (p->roomLeft > 0.0f) break;            // (the others land after this one)
        sim->inbound = p->next;
        add_event(sim, EVENT_HELI_LANDED);
        p->roomLeft = 0.0f;
        p->arrivalTime = sim->clock;
        p->walkLeft = HELI_WALK_IN;
        Patient **link = &sim->waiting;
        while (*link != NULL && (*link)->rescue) link = &(*link)->next;
        p->next = *link;
        *link = p;
        sim->waitingCount++;
        sim->stats.arrived++;
    }
    // inbound patients behind the first one keep counting down too
    for (Patient *p = sim->inbound; p != NULL; p = p->next) {
        if (p != sim->inbound) p->roomLeft -= dt;
    }
}

void sim_shock_result(Sim *sim, int id, bool won)
{
    for (int i = 0; i < BED_COUNT; i++) {
        Patient *p = sim->beds[i].occupant;
        if (p == NULL || p->id != id) continue;
        sim->stats.shocksPlayed++;
        if (won) {
            p->treatmentLeft *= 0.5f;
        } else {
            p->treatmentLeft += 0.3f * p->treatmentTotal;
            sim->stats.shocksFailed++;
        }
        return;
    }
}

void sim_spin_result(Sim *sim, int id, bool won)
{
    SimRoom *room = &sim->rooms[ROOM_BLOODS];
    for (int s = 0; s < room->capacity; s++) {
        Patient *p = room->in[s];
        if (p == NULL || p->id != id) continue;
        sim->stats.spinsPlayed++;
        if (won) {
            p->roomLeft *= 0.5f;
        } else {
            p->roomLeft += 0.5f * room->duration;
            sim->stats.spinsFailed++;
        }
        return;
    }
}

void sim_scan_result(Sim *sim, int room, int id, bool won)
{
    SimRoom *r = &sim->rooms[room];
    for (int s = 0; s < r->capacity; s++) {
        Patient *p = r->in[s];
        if (p == NULL || p->id != id) continue;
        sim->stats.scansPlayed++;
        if (won) {
            p->roomLeft *= 0.5f;
        } else {
            p->roomLeft += 0.5f * r->duration;           // the retake
            sim->stats.scansFailed++;
        }
        return;
    }
}

void sim_pharmacy_result(Sim *sim, int id, PharmacyResult result)
{
    SimRoom *room = &sim->rooms[ROOM_PHARMACY];
    for (int s = 0; s < room->capacity; s++) {
        Patient *p = room->in[s];
        if (p == NULL || p->id != id) continue;
        sim->stats.pharmacyPlayed++;
        if (result == PHARMACY_FAST) {
            p->roomLeft *= 0.5f;
        } else if (result == PHARMACY_TIMEOUT) {
            p->roomLeft += 0.5f * room->duration;
            sim->stats.pharmacyFailed++;
        } else if (result == PHARMACY_WRONG) {
            // Back to the queue: their place is given up (and set up for the
            // next one), and they wait with everyone else for another go
            sim->stats.pharmacyFailed++;
            sim->stats.pharmacySentBack++;
            room->in[s] = NULL;
            room->ready[s] = ROOM_TURNOVER;
            p->roomLeft = 0.0f;
            p->walkLeft = 0.0f;                          // already there: straight into the queue
            queue_insert(&room->queue, p);
            room->queueCount++;
        }
        return;
    }
}

Patient *sim_triage_new(Sim *sim)
{
    return new_patient(sim, random_severity());
}

void sim_triage_admit(Sim *sim, Patient *p, int given)
{
    if (p == NULL) return;
    if (given >= 1 && given <= 3) {
        if (given == p->severity)     sim->stats.triageRight++;
        else if (given > p->severity) sim->stats.triageTooLow++;    // e.g. an emergency called routine
        else                          sim->stats.triageTooHigh++;
        p->severity = given;          // their place in the queue follows the call that was made
    }
    p->arrivalTime = sim->clock;
    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
    sim->stats.arrived++;
}

void sim_triage_game(Sim *sim, bool allRight)
{
    sim->stats.triageGames++;
    if (!allRight) sim->stats.triageGamesLost++;
}

int sim_ambulance_arrival(Sim *sim, bool fast)
{
    Patient *p = new_patient(sim, 1);
    if (p == NULL) return 0;
    if (fast) {
        p->treatmentLeft *= 0.6f;                        // started on the way in
    } else {
        p->treatmentTotal *= 1.2f;                       // longer to stabilise
        p->treatmentLeft = p->treatmentTotal;
    }
    queue_insert(&sim->waiting, p);
    sim->waitingCount++;
    sim->stats.arrived++;
    sim->stats.drivesPlayed++;
    if (!fast) sim->stats.drivesSlow++;
    return p->id;
}

void sim_landing_result(Sim *sim, int id, bool won)
{
    for (Patient *p = sim->inbound; p != NULL; p = p->next) {
        if (p->id != id) continue;
        sim->stats.landingsPlayed++;
        if (won) {
            p->treatmentLeft *= 0.6f;
        } else {
            p->treatmentTotal *= 1.25f;                  // (not started yet: the whole treatment is longer)
            p->treatmentLeft = p->treatmentTotal;
            sim->stats.landingsFailed++;
        }
        return;
    }
}

void sim_init(Sim *sim, int day, const Setup *setup)
{
    *sim = (Sim){ 0 };
    if (setup != NULL) sim->setup = *setup;
    else               sim_default_setup(&sim->setup);
    sim_clamp_setup(&sim->setup);
    sim->onCallComing = -1.0f;
    sim->lastLanding = -100.0f;
    sim->day = day;
    sim->shift = SHIFT_DAY;
    sim->nextId = 1;
    sim->nextArrival = random_arrival_gap(sim);
    rooms_init(sim);
    schedule_rescue(sim, 0.10f, 0.60f);
    sim->power = POWER_ON;
    sim->powerBackAt = -1.0f;
    schedule_power_cut(sim);
    // The bus crash: some time in the middle of the day shift
    sim->incidentAt = (sim->setup.scenario == SCENARIO_MAJOR_INCIDENT)
                    ? SHIFT_LENGTH * (float)GetRandomValue(25, 55) / 100.0f : -1.0f;
    sim->incidentSince = -1.0f;
    for (int d = 0; d < NURSE_MAX; d++) {
        sim->nurses[d].busyWithBed = -1;
    }
    for (int d = 0; d < DOCTOR_SLOTS; d++) {
        sim->doctors[d].busyWithBed = -1;
    }
    roster_assign(sim);      // day shift staff
}
 
int sim_nurse_at(const Sim *sim, int bed)
{
    for (int d = 0; d < NURSE_MAX; d++) {
        if (sim->nurses[d].busyWithBed == bed) return d;
    }
    return -1;
}
 
int sim_doctor_at(const Sim *sim, int bed)
{
    for (int d = 0; d < DOCTOR_SLOTS; d++) {
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
// or -1 if every occupied bed already has one. Patients who still need
// assessing or a nurse's treatment come first; then patients waiting for a
// doctor (the nurse stays with them and starts their treatment).
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
    if (best != -1) return best;
    // Nobody needs assessing or treating: stay with the most urgent patient
    // who's waiting for a doctor, and start their treatment
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p == NULL || !p->needsDoctor || p->doneInAE || sim_nurse_at(sim, i) != -1 || sim_doctor_at(sim, i) != -1) continue;
        if (p->assessLeft <= 0.0f && p->treatmentLeft <= p->treatmentTotal * (1.0f - NURSE_HELP_LIMIT)) continue;   // done all they can
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
    while (sim->leaving != NULL) {
        p = sim->leaving;
        sim->leaving = p->next;
        free(p);
    }
    while (sim->inbound != NULL) {
        p = sim->inbound;
        sim->inbound = p->next;
        free(p);
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
    for (int d = sim->setup.nurses[SHIFT_NIGHT]; d < NURSE_MAX; d++)     sim->nurses[d].busyWithBed = -1;
    for (int d = sim->setup.doctors[SHIFT_NIGHT]; d < DOCTOR_SLOTS; d++) sim->doctors[d].busyWithBed = -1;
    sim->onCall = false;             // the on-call doctor goes home too
    sim->onCallComing = -1.0f;
    sim->onCallPressure = 0.0f;

    // The power is back on for the new shift (if it wasn't), and maybe it'll go again
    sim->power = POWER_ON;
    sim->generatorStuck = false;
    sim->powerBackAt = -1.0f;
    schedule_power_cut(sim);
 
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

// Patients who can't walk need a wheelchair (and a porter) to leave their
// A&E bed, for their first room or for the front door.
static bool needs_wheelchair_to_leave(const Patient *p)
{
    return (p->equipNeeds & EQUIP_BIT(EQUIP_WHEELCHAIR)) != 0;
}

// Is someone with this patient, ready to treat them? (A patient waiting for a
// doctor can have a nurse with them, starting their treatment.)
static bool staff_with(const Sim *sim, int bed, const Patient *p)
{
    if (p->needsDoctor && sim_doctor_at(sim, bed) != -1) return true;
    return sim_nurse_at(sim, bed) != -1;
}

// How far a nurse can take the treatment of a patient who needs a doctor:
// treatmentLeft can't go below this without the doctor.
static float nurse_limit(const Patient *p)
{
    return p->treatmentTotal * (1.0f - NURSE_HELP_LIMIT);
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
    send_on(sim, p, 0);
}

void sim_update(Sim *sim, float dt)
{
    if (sim->shiftOver) return;
 
    // Never run past the end of the shift.
    if (sim->clock + dt >= shift_end(sim)) dt = shift_end(sim) - sim->clock;
    sim->clock += dt;
 
    // 0. The power: a cut by itself, and the generator starting
    if (sim->nextPowerCut >= 0.0f && sim->clock >= sim->nextPowerCut) {
        sim->nextPowerCut = -1.0f;
        sim_power_cut(sim);
    }
    if (sim->power == POWER_OUT) {
        sim->stats.powerOffTime += dt;
        if (!sim->generatorStuck) {
            sim->powerTimer -= dt;
            if (sim->powerTimer <= 0.0f) {
                sim->power = POWER_ON;
                sim->powerBackAt = sim->clock;
                add_event(sim, EVENT_POWER_ON);
            }
        }
    }
    float speed = (sim->power == POWER_OUT) ? DARK_TREATMENT_SPEED : 1.0f;   // treatment under the emergency lights

    // 1. Arrivals, and now and then a helicopter rescue; a major incident's casualties
    if (sim->incidentAt >= 0.0f && sim->clock >= sim->incidentAt) {
        sim->incidentAt = -1.0f;
        sim->incidentLeft = 12;
        sim->incidentNext = sim->clock;
        sim->incidentSince = sim->clock;
        add_event(sim, EVENT_INCIDENT);
    }
    while (sim->incidentLeft > 0 && sim->clock >= sim->incidentNext) {
        add_casualty(sim);
        sim->incidentLeft--;
        sim->incidentNext += 1.0f + (float)GetRandomValue(0, 6) / 10.0f;
    }
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
                p->needsDoctor = true;      // a doctor is needed; the nurse stays until one comes
                sim->stats.sawDoctor++;
            }
            continue;
        }
 
        if (p->needsDoctor && d == -1) {
            // Waiting for a doctor. A nurse with them starts the treatment at
            // half speed, but can only take it so far on their own.
            if (n == -1 || still_needs(p) != 0) continue;
            if (p->treatmentLeft > nurse_limit(p)) {
                p->treatmentLeft -= dt * NURSE_HELP_SPEED * speed;
                if (p->treatmentLeft < nurse_limit(p)) p->treatmentLeft = nurse_limit(p);
                sim->stats.nurseHelpTime += dt;
            }
            continue;
        }
        if (p->needsDoctor && n != -1) {            // the doctor has taken over: the nurse moves on
            sim->nurses[n].busyWithBed = -1;
            n = -1;
        }
        if (!p->needsDoctor && n == -1) continue;  // waiting for a nurse (theirs went home)
        if (still_needs(p) != 0) continue;         // waiting for equipment
 
        p->treatmentLeft -= dt * speed;
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
 
    // 3b. The rooms after A&E, patients being taken out to the front door, and
    //     rescues in the air
    update_rooms(sim, dt);
    update_leaving(sim, dt);
    update_inbound(sim, dt);
    for (Patient *p = sim->waiting; p != NULL; p = p->next) {   // wheeled in from the helipad
        if (p->walkLeft > 0.0f) p->walkLeft -= dt;
    }
 
    // 4. Admission: every free bed takes the most urgent waiting patient. The
    //    resus bay is tried first but only takes emergencies; the queue is
    //    sorted most urgent first, so if the front of it isn't an emergency
    //    nobody waiting is, and the resus bay stays free for the next one.
    for (int k = 0; k < BED_COUNT; k++) {
        int i = (k == 0) ? RESUS_BED : k - 1;
        if (sim->beds[i].occupant != NULL || !sim_bed_open(sim, i)) continue;   // taken, or closed today
        if (sim->waiting == NULL) break;
        if (i == RESUS_BED && sim->waiting->severity != 1) continue;
 
        Patient *p = queue_pop(&sim->waiting);
 
        sim->beds[i].occupant = p;
        // nothing starts until they get to the bed (from the helipad, if they're still on their way in)
        p->walkLeft = WALK_TO_BED + (p->walkLeft > 0.0f ? p->walkLeft : 0.0f);
        sim->waitingCount--;
        sim->stats.admitted++;
        float wait = sim->clock - p->arrivalTime;
        p->waitedFor = wait;
        sim->stats.totalWait += wait;
        if (wait > sim->stats.longestWait) sim->stats.longestWait = wait;
    }
 
    // 5. Nurse rounds. A nurse helping a patient who's waiting for a doctor
    //    leaves once they've done all they can, or when another patient needs
    //    a nurse and there's nobody else free. Then every free nurse goes to
    //    the most urgent bed that needs one.
    int needNurse = 0, freeNurses = 0;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p && !p->needsDoctor && !p->doneInAE && sim_nurse_at(sim, i) == -1) needNurse++;
    }
    for (int d = 0; d < sim_nurses_on(sim); d++) {
        int b = sim->nurses[d].busyWithBed;
        if (b == -1) { freeNurses++; continue; }
        const Patient *p = sim->beds[b].occupant;
        bool helping = p && p->needsDoctor && p->assessLeft <= 0.0f && sim_doctor_at(sim, b) == -1;
        if (!helping) continue;
        if (p->treatmentLeft <= nurse_limit(p) || needNurse > freeNurses) {
            sim->nurses[d].busyWithBed = -1;
            freeNurses++;
        }
    }
    for (int d = 0; d < sim_nurses_on(sim); d++) {
        if (sim->nurses[d].busyWithBed != -1) continue;
 
        int bed = bed_needing_nurse(sim);
        if (bed == -1) break;
 
        sim->nurses[d].busyWithBed = bed;
    }
 
    // 5b. The on-call doctor: called in when the waiting room has been full
    //     (WAITING_ROOM_SEATS or more waiting) for ON_CALL_AFTER seconds in a
    //     row, arriving ON_CALL_DELAY later. They stay until the waiting room
    //     is down to half full (ON_CALL_HOME) and they're free.
    int onCallSlot = sim_shift_doctors(sim);
    if (!sim->onCall) {
        if (sim->waitingCount >= WAITING_ROOM_SEATS) sim->onCallPressure += dt;
        else                                         sim->onCallPressure = 0.0f;
        if (sim->onCallPressure >= ON_CALL_AFTER && sim->onCallComing < 0.0f) sim->onCallComing = ON_CALL_DELAY;
        if (sim->onCallComing >= 0.0f) {
            sim->onCallComing -= dt;
            if (sim->onCallComing <= 0.0f) {
                sim->onCall = true;
                sim->onCallComing = -1.0f;
                sim->doctors[onCallSlot].name = roster_on_call(roster_shift_number(sim->day, sim->shift));
                sim->doctors[onCallSlot].busyWithBed = -1;
                sim->stats.onCallCalls++;
                add_event(sim, EVENT_ON_CALL);
            }
        }
    } else {
        sim->stats.onCallTime += dt;
        if (sim->waitingCount <= ON_CALL_HOME && sim->doctors[onCallSlot].busyWithBed == -1) sim->onCall = false;
    }

        // 6. Doctor rounds: every free doctor goes to the most urgent patient
    //    a nurse has sent to a doctor
    for (int d = 0; d < sim_doctors_on(sim); d++) {
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
    for (int r = 0; r < ROOM_TYPES; r++) {                   // finished in a room, waiting for a wheelchair
        for (int s = 0; s < sim->rooms[r].capacity; s++) {
            const Patient *p = sim->rooms[r].in[s];
            if (p == NULL) continue;
            for (int e = 0; e < EQUIP_TYPES; e++) if (p->equipMissing & EQUIP_BIT(e)) sim->stats.equipWaitTotal[e] += dt;
        }
    }
    for (int d = 0; d < NURSE_MAX; d++) {
        if (sim->nurses[d].busyWithBed != -1) sim->stats.nurseBusyTime += dt;
    }
    for (int d = 0; d < sim_shift_doctors(sim); d++) {      // (the shift's own doctors; on call is counted apart)
        if (sim->doctors[d].busyWithBed != -1) sim->stats.doctorBusyTime += dt;
    }
 
    // 8. The graphs: the waiting room every SAMPLE_EVERY seconds
    while (sim->stats.samples < SAMPLES && sim->stats.samples * SAMPLE_EVERY <= sim_shift_time(sim) + 0.001f) {
        int k = sim->stats.samples++, inBeds = 0;
        for (int i = 0; i < BED_COUNT; i++) if (sim->beds[i].occupant) inBeds++;
        sim->stats.sampleWaiting[k] = sim->waitingCount;
        sim->stats.sampleAvgWait[k] = sim->stats.admitted > 0 ? sim->stats.totalWait / sim->stats.admitted : 0.0f;
        sim->stats.sampleInBeds[k] = inBeds;
    }

    if (sim->clock >= shift_end(sim)) end_shift(sim);
}