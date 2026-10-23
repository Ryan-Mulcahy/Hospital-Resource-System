// tests/test_core.c - automatic tests for the simulation's core C code.
//
// Run them with:   make test
//
// No window is opened: these call the simulation directly and check the
// results. They cover the parts that do the "advanced programming" work:
//
//   1. the waiting queue      a sorted linked list (queue_insert, queue_pop)
//   2. the conditions loader  reading conditions.csv, including bad input
//   3. the treatment log      a dynamic array that grows with realloc
//   4. no patient lost        every patient is in exactly one place, always,
//                             on every kind of day, through power cuts
//   5. the CSV export         writing the log, and failing safely
//   6. freeing memory         sim_free leaves nothing behind
//
// Every check prints nothing when it passes and a FAIL line when it doesn't.
// Each test prints PASS or FAIL, and the program exits with 1 if anything
// failed, so `make test` stops with an error.
//
// To check for memory leaks as well, on a Mac:   leaks --atExit -- ./run_tests

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"          // SetRandomSeed, so every run is the same
#include "patient.h"
#include "conditions.h"
#include "sim.h"

// ------------------------------------------------------------------------
// A very small test framework
// ------------------------------------------------------------------------

static int checksFailed = 0;     // in the current test
static int testsRun = 0, testsFailed = 0;

// Check one thing. On failure, say which line and what was expected.
#define CHECK(condition, ...)                                              \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("    FAIL (line %d): ", __LINE__);                      \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
            checksFailed++;                                                \
        }                                                                  \
    } while (0)

static void run_test(void (*test)(void), const char *name)
{
    checksFailed = 0;
    test();
    testsRun++;
    if (checksFailed > 0) testsFailed++;
    printf("%s  %s\n", checksFailed == 0 ? "PASS" : "FAIL", name);
}

// A patient on the stack, for the queue tests (they never get freed).
static Patient make_patient(int id, int severity)
{
    Patient p = { 0 };
    p.id = id;
    p.severity = severity;
    return p;
}

// Write a small file for the loader tests. "wb" so "\r\n" stays as written.
static void write_file(const char *name, const char *text)
{
    FILE *f = fopen(name, "wb");
    if (f == NULL) { printf("    could not write %s\n", name); return; }
    fputs(text, f);
    fclose(f);
}

static int list_length(const Patient *head)
{
    int n = 0;
    for (; head != NULL; head = head->next) n++;
    return n;
}

// ------------------------------------------------------------------------
// 1. The waiting queue
// ------------------------------------------------------------------------

static void test_queue_empty(void)
{
    Patient *head = NULL;
    CHECK(queue_pop(&head) == NULL, "popping an empty queue should give NULL");
    CHECK(head == NULL, "the queue should still be empty");
}

// Mixed severities come out most urgent first; equal severities in arrival order.
static void test_queue_order(void)
{
    int severities[6] = { 3, 1, 2, 1, 3, 2 };       // patients 1 to 6 arrive in this order
    int expected[6]   = { 2, 4, 3, 6, 1, 5 };       // ids in the order they should leave
    Patient p[6];
    Patient *head = NULL;
    for (int i = 0; i < 6; i++) {
        p[i] = make_patient(i + 1, severities[i]);
        queue_insert(&head, &p[i]);
    }
    CHECK(list_length(head) == 6, "expected 6 in the queue, found %d", list_length(head));
    for (int i = 0; i < 6; i++) {
        Patient *out = queue_pop(&head);
        CHECK(out != NULL, "queue ran out after %d patients", i);
        if (out == NULL) return;
        CHECK(out->id == expected[i], "position %d: expected patient %d, got patient %d",
              i + 1, expected[i], out->id);
        CHECK(out->next == NULL, "a popped patient should be unlinked (next == NULL)");
    }
    CHECK(head == NULL, "the queue should be empty after popping everyone");
}

// Five patients of the same severity leave in the order they arrived (the <= in queue_insert).
static void test_queue_ties_first_come_first_served(void)
{
    Patient p[5];
    Patient *head = NULL;
    for (int i = 0; i < 5; i++) {
        p[i] = make_patient(i + 1, 2);
        queue_insert(&head, &p[i]);
    }
    for (int i = 0; i < 5; i++) {
        Patient *out = queue_pop(&head);
        CHECK(out != NULL && out->id == i + 1, "expected patient %d next", i + 1);
    }
}

// Inserting at the front, middle and end all work: this is what Patient ** is for.
static void test_queue_insert_positions(void)
{
    Patient urgent  = make_patient(1, 2);
    Patient routine = make_patient(2, 3);
    Patient emerg   = make_patient(3, 1);    // goes to the front: the head pointer must change
    Patient urgent2 = make_patient(4, 2);    // goes in the middle, behind the other urgent one
    Patient *head = NULL;

    queue_insert(&head, &urgent);
    queue_insert(&head, &routine);           // end
    queue_insert(&head, &emerg);             // front
    queue_insert(&head, &urgent2);           // middle

    CHECK(head == &emerg, "the emergency should now be at the head of the queue");
    int expected[4] = { 3, 1, 4, 2 };
    int i = 0;
    for (const Patient *q = head; q != NULL && i < 4; q = q->next, i++) {
        CHECK(q->id == expected[i], "position %d: expected %d, got %d", i + 1, expected[i], q->id);
    }
    CHECK(list_length(head) == 4, "expected 4 in the queue, found %d", list_length(head));
}

// ------------------------------------------------------------------------
// 2. The conditions loader
// ------------------------------------------------------------------------

static void test_conditions_good_file(void)
{
    write_file("test_conditions.csv",
               "severity,condition,icd10,pathway\n"
               "1,Cardiac arrest,I46.9,bloods+ward\n"
               "2,Broken wrist,S62.90,xray\n"
               "3,Cut hand,S61.419A,home\n");
    int n = conditions_load("test_conditions.csv");
    CHECK(n == 3, "expected 3 conditions (header skipped), loaded %d", n);

    const Condition *c = conditions_random(2);    // the only urgent one
    CHECK(strcmp(c->name, "Broken wrist") == 0, "name: got \"%s\"", c->name);
    CHECK(strcmp(c->icd10, "S62.90") == 0, "code: got \"%s\"", c->icd10);
    CHECK(strcmp(c->pathway, "xray") == 0, "pathway: got \"%s\"", c->pathway);
    remove("test_conditions.csv");
}

// A file saved on Windows ends every line with \r\n; the \r must not stick to the last column.
static void test_conditions_windows_line_endings(void)
{
    write_file("test_conditions.csv",
               "severity,condition,icd10\r\n"
               "1,Stroke,I63.9\r\n"
               "3,Back pain,M54.5\r\n");
    int n = conditions_load("test_conditions.csv");
    CHECK(n == 2, "expected 2 conditions, loaded %d", n);
    const Condition *c = conditions_random(1);
    CHECK(strcmp(c->icd10, "I63.9") == 0, "code should be \"I63.9\" with no \\r, got \"%s\"", c->icd10);
    CHECK(c->pathway[0] == '\0', "no pathway column should mean an empty pathway");
    remove("test_conditions.csv");
}

// Lines that don't make sense are skipped instead of crashing or loading rubbish.
static void test_conditions_bad_lines(void)
{
    write_file("test_conditions.csv",
               "severity,condition,icd10\n"
               "abc,Not a number,X1\n"         // severity isn't a number
               "4,Too severe,X2\n"             // severity out of range
               "0,Not severe enough,X3\n"
               "2,,R07.9\n"                    // no name
               "1,Only two columns\n"
               "\n"                            // blank line
               "2,Chest pain,R07.9\n"          // the only good line
               "3,Extra columns,R42,home,unexpected,more\n");   // extra columns ignored
    int n = conditions_load("test_conditions.csv");
    CHECK(n == 2, "expected 2 good lines, loaded %d", n);
    const Condition *c = conditions_random(3);
    CHECK(strcmp(c->name, "Extra columns") == 0 && strcmp(c->pathway, "home") == 0,
          "extra columns should be ignored, got \"%s\" / \"%s\"", c->name, c->pathway);
    remove("test_conditions.csv");
}

// A name longer than the buffer is shortened, not skipped, and never overflows.
static void test_conditions_long_name(void)
{
    write_file("test_conditions.csv",
               "2,Acute exacerbation of chronic obstructive pulmonary disease with infection,J44.1\n");
    int n = conditions_load("test_conditions.csv");
    CHECK(n == 1, "the long line should still load, loaded %d", n);
    const Condition *c = conditions_random(2);
    CHECK(strlen(c->name) == CONDITION_NAME_LEN - 1, "name should be cut to %d characters, is %d",
          CONDITION_NAME_LEN - 1, (int)strlen(c->name));
    CHECK(strcmp(c->icd10, "J44.1") == 0, "the code after a long name should still load");
    remove("test_conditions.csv");
}

// With no file, every severity still gets a condition: the built-in "Unspecified" one.
static void test_conditions_missing_file(void)
{
    int n = conditions_load("this_file_does_not_exist.csv");
    CHECK(n == 0, "a missing file should load 0, loaded %d", n);
    for (int s = 1; s <= 3; s++) {
        const Condition *c = conditions_random(s);
        CHECK(c != NULL, "conditions_random(%d) returned NULL", s);
        if (c == NULL) continue;
        CHECK(c->severity == s, "fallback for %d has severity %d", s, c->severity);
        CHECK(strcmp(c->icd10, "R69") == 0, "fallback code should be R69, got %s", c->icd10);
    }
}

// Every random pick matches the severity asked for (1000 tries with the real file).
static void test_conditions_random_matches_severity(void)
{
    conditions_load("conditions.csv");
    int wrong = 0;
    for (int i = 0; i < 1000; i++) {
        int s = 1 + i % 3;
        if (conditions_random(s)->severity != s) wrong++;
    }
    CHECK(wrong == 0, "%d of 1000 random conditions had the wrong severity", wrong);
}

// ------------------------------------------------------------------------
// 3. The treatment log
// ------------------------------------------------------------------------

// Run until the shift ends, 1/30 of a second at a time.
static void run_shift(Sim *sim)
{
    int steps = 0;
    while (!sim->shiftOver && steps < 100000) {
        sim_update(sim, 1.0f / 30.0f);
        steps++;
    }
}

static void test_log_grows_and_matches(void)
{
    Sim sim;
    sim_init(&sim, 1, NULL);
    run_shift(&sim);

    int count = sim.treatedLogCount, cap = sim.treatedLogCapacity;
    CHECK(count == sim.stats.treated, "log has %d records but %d patients were treated",
          count, sim.stats.treated);
    CHECK(count > 64, "a full shift should treat more than 64 patients (so the log had to grow), "
          "only %d", count);
    CHECK(cap >= count, "capacity %d is smaller than the %d records in it", cap, count);
    int c = 64;                                      // starts at 64 and doubles
    while (c < cap) c *= 2;
    CHECK(c == cap, "capacity %d isn't 64 doubled some number of times", cap);

    // Every record makes sense, and they're in the order patients left
    for (int i = 0; i < count; i++) {
        const TreatedRecord *r = &sim.treatedLog[i];
        CHECK(r->condition != NULL, "record %d has no condition", i);
        CHECK(r->dischargeTime >= r->arrivalTime, "record %d left before arriving", i);
        CHECK(r->wait >= 0.0f, "record %d has a negative wait", i);
        CHECK(strncmp(r->treatedBy, "Dr ", 3) == 0 || strncmp(r->treatedBy, "Nurse ", 6) == 0,
              "record %d: treated by \"%s\"", i, r->treatedBy);
        // Anyone a nurse sent to a doctor must have been treated by one. (It doesn't
        // work the other way: a free doctor also takes routine and urgent patients.)
        if (r->sawDoctor) CHECK(strncmp(r->treatedBy, "Dr ", 3) == 0,
                                "record %d: sent to a doctor but treated by \"%s\"", i, r->treatedBy);
        if (i > 0) CHECK(r->dischargeTime >= sim.treatedLog[i - 1].dischargeTime,
                         "record %d left before record %d", i, i - 1);
        if (checksFailed > 5) break;                 // don't print hundreds of lines
    }
    sim_free(&sim);
}

// At handover the log starts again but keeps its memory, so night doesn't reallocate.
static void test_log_reused_at_handover(void)
{
    Sim sim;
    sim_init(&sim, 1, NULL);
    run_shift(&sim);
    TreatedRecord *before = sim.treatedLog;
    int cap = sim.treatedLogCapacity;

    sim_start_night(&sim);
    CHECK(sim.treatedLogCount == 0, "the night log should start empty, has %d", sim.treatedLogCount);
    CHECK(sim.treatedLog == before && sim.treatedLogCapacity == cap,
          "the night shift should reuse the day's array, not allocate a new one");

    run_shift(&sim);
    CHECK(sim.treatedLogCount == sim.stats.treated, "night log %d vs treated %d",
          sim.treatedLogCount, sim.stats.treated);
    sim_free(&sim);
}

// ------------------------------------------------------------------------
// 4. No patient lost or counted twice
// ------------------------------------------------------------------------

// Patients the log says were sent home straight from A&E (no rooms after).
static int went_home_from_ae(const Sim *sim)
{
    int n = 0;
    for (int i = 0; i < sim->treatedLogCount; i++) {
        if (strcmp(sim->treatedLog[i].sentTo, "Home") == 0) n++;
    }
    return n;
}

// Patients who have finished (treated in A&E with no rooms after, or done with
// their last room) but haven't left the building yet: still in their A&E bed
// waiting for a wheelchair, or being wheeled out to the front door.
static int finished_but_still_here(const Sim *sim)
{
    int n = 0;
    for (const Patient *p = sim->leaving; p != NULL; p = p->next) n++;
    for (int i = 0; i < BED_COUNT; i++) {
        const Patient *p = sim->beds[i].occupant;
        if (p != NULL && p->doneInAE && p->route[0] == ROOM_NONE) n++;
    }
    return n;
}

// Check one moment: every patient who has ever arrived (or is on their way in
// by helicopter) is in exactly one place: in the air, waiting, in a bed, in a
// room or its queue, or being wheeled out. Everyone else must have gone home.
// Also checks the counters match the lists and every queue is in severity order.
static void check_everyone_accounted_for(const Sim *sim, int homeEarlierShifts, const char *when)
{
    static char seen[100000];
    int arrived = sim->nextId - 1;
    if (arrived >= (int)sizeof seen) return;
    memset(seen, 0, (size_t)arrived + 1);
    int here = 0, twice = 0;

    #define VISIT(p) do { if (seen[(p)->id]++) twice++; here++; } while (0)

    int waiting = 0, lastSeverity = 0;
    for (const Patient *p = sim->waiting; p != NULL; p = p->next) {
        VISIT(p);
        waiting++;
        CHECK(p->severity >= lastSeverity, "%s: waiting queue out of order", when);
        lastSeverity = p->severity;
    }
    CHECK(waiting == sim->waitingCount, "%s: waitingCount says %d, list has %d",
          when, sim->waitingCount, waiting);

    for (int i = 0; i < BED_COUNT; i++) {
        if (sim->beds[i].occupant) VISIT(sim->beds[i].occupant);
    }
    for (const Patient *p = sim->inbound; p != NULL; p = p->next) VISIT(p);   // in the air
    for (const Patient *p = sim->leaving; p != NULL; p = p->next) VISIT(p);   // being wheeled out
    for (int r = 0; r < ROOM_TYPES; r++) {
        const SimRoom *room = &sim->rooms[r];
        for (int s = 0; s < room->capacity; s++) if (room->in[s]) VISIT(room->in[s]);
        int queued = 0;
        for (const Patient *p = room->queue; p != NULL; p = p->next) { VISIT(p); queued++; }
        CHECK(queued == room->queueCount, "%s: %s queueCount says %d, list has %d",
              when, room->name, room->queueCount, queued);
    }
    #undef VISIT

    int home = homeEarlierShifts + went_home_from_ae(sim) + sim->stats.leftHospital
             - finished_but_still_here(sim);
    CHECK(twice == 0, "%s: %d patients are in two places at once", when, twice);
    CHECK(here == sim_patients_in_hospital(sim), "%s: sim_patients_in_hospital says %d, the lists hold %d",
          when, sim_patients_in_hospital(sim), here);
    CHECK(here + home == arrived, "%s: %d arrived, but %d are here and %d went home",
          when, arrived, here, home);
}

static void test_no_patient_lost(void)
{
    Sim sim;
    sim_init(&sim, 1, NULL);
    int checks = 0;

    // Day shift, checking every 2 simulated seconds (90 times)
    for (int step = 1; !sim.shiftOver; step++) {
        sim_update(&sim, 1.0f / 30.0f);
        if (step % 60 == 0) { check_everyone_accounted_for(&sim, 0, "day shift"); checks++; }
        if (checksFailed > 5) break;
    }
    int homeByDay = went_home_from_ae(&sim) + sim.stats.leftHospital;

    // Press H a few times mid-night: rescues jump the queue but nobody may be lost
    sim_start_night(&sim);
    for (int step = 1; !sim.shiftOver; step++) {
        if (step == 900 || step == 901 || step == 902) sim_add_rescue(&sim);
        sim_update(&sim, 1.0f / 30.0f);
        if (step % 60 == 0) { check_everyone_accounted_for(&sim, homeByDay, "night shift"); checks++; }
        if (checksFailed > 5) break;
    }
    CHECK(checks > 150, "only %d checks ran", checks);
    sim_free(&sim);
}

// One shift, checking everyone is accounted for every 3 simulated seconds.
// `home` is everyone who went home in earlier shifts; it's updated at the end.
static void run_checked_shift(Sim *sim, int *home, const char *when, int *checks)
{
    for (int step = 1; !sim->shiftOver; step++) {
        sim_update(sim, 1.0f / 30.0f);
        if (step == 1500) {                          // the power goes off mid-shift
            sim_power_cut(sim);
            if (sim_generator_needed(sim)) sim_generator_result(sim, false);   // and the game is lost
        }
        if (step % 90 == 0) { check_everyone_accounted_for(sim, *home, when); (*checks)++; }
        if (checksFailed > 5) return;
    }
    check_everyone_accounted_for(sim, *home, when);
    *home += went_home_from_ae(sim) + sim->stats.leftHospital;
}

// Every kind of day (normal, flu, major incident), with a power cut in every
// shift and yesterday's patients carried over into day 2.
static void test_every_kind_of_day(void)
{
    for (int sc = 0; sc < SCENARIO_COUNT; sc++) {
        Setup setup;
        sim_default_setup(&setup);
        setup.scenario = (Scenario)sc;
        setup.carryOver = true;

        Sim sim;
        sim_init(&sim, 1, &setup);
        int home = 0, checks = 0;
        for (int day = 1; day <= 2; day++) {
            run_checked_shift(&sim, &home, sim_scenario_name((Scenario)sc), &checks);
            sim_start_night(&sim);
            run_checked_shift(&sim, &home, sim_scenario_name((Scenario)sc), &checks);
            if (day == 1) sim_next_day(&sim, &setup);       // patients still in stay in
        }
        CHECK(checks > 200, "%s: only %d checks ran", sim_scenario_name((Scenario)sc), checks);
        CHECK(sim.stats.powerCuts >= 1, "%s: the power cut didn't happen", sim_scenario_name((Scenario)sc));
        sim_free(&sim);
    }
}

// ------------------------------------------------------------------------
// 5. The CSV export
// ------------------------------------------------------------------------

// Count the columns in a CSV line, ignoring commas inside "quotes".
static int csv_columns(const char *line)
{
    int cols = 1;
    bool quoted = false;
    for (; *line != '\0' && *line != '\n'; line++) {
        if (*line == '"') quoted = !quoted;
        else if (*line == ',' && !quoted) cols++;
    }
    return cols;
}

static void test_csv_export(void)
{
    Sim sim;
    sim_init(&sim, 1, NULL);
    run_shift(&sim);

    CHECK(sim_save_log_csv(&sim, "test_log.csv"), "saving the CSV should succeed");
    FILE *f = fopen("test_log.csv", "r");
    CHECK(f != NULL, "the CSV file wasn't created");
    if (f != NULL) {
        char line[512];
        int lines = 0, badRows = 0;
        while (fgets(line, sizeof line, f) != NULL) {
            if (lines == 0) {
                CHECK(strncmp(line, "patient,severity,", 17) == 0, "unexpected header: %s", line);
            }
            if (csv_columns(line) != 13) badRows++;
            if (lines > 0) {
                int id = atoi(line);
                CHECK(id == sim.treatedLog[lines - 1].id, "row %d: patient %d, expected %d",
                      lines, id, sim.treatedLog[lines - 1].id);
            }
            lines++;
            if (checksFailed > 5) break;
        }
        fclose(f);
        CHECK(lines == sim.treatedLogCount + 1, "expected a header and %d rows, found %d lines",
              sim.treatedLogCount, lines);
        CHECK(badRows == 0, "%d lines don't have 13 columns", badRows);
    }
    remove("test_log.csv");

    // A folder that doesn't exist: must report failure, not crash
    CHECK(!sim_save_log_csv(&sim, "no_such_folder/test_log.csv"),
          "saving into a missing folder should return false");
    sim_free(&sim);
}

// ------------------------------------------------------------------------
// 6. Freeing memory
// ------------------------------------------------------------------------

static void test_sim_free_empties_everything(void)
{
    Sim sim;
    sim_init(&sim, 1, NULL);
    // Run until there are patients in every kind of place: waiting, in beds,
    // in rooms and queuing for a room. That's when freeing is hardest to get right.
    bool roomQueue = false;
    for (int i = 0; i < 20000 && !sim.shiftOver; i++) {
        sim_update(&sim, 1.0f / 30.0f);
        roomQueue = false;
        for (int r = 0; r < ROOM_TYPES; r++) if (sim.rooms[r].queue != NULL) roomQueue = true;
        if (roomQueue && sim.waiting != NULL) break;
    }
    CHECK(roomQueue && sim.waiting != NULL,
          "test setup: expected patients waiting and queuing for a room before freeing");

    sim_free(&sim);
    CHECK(sim.waiting == NULL, "waiting queue not emptied");
    CHECK(sim.inbound == NULL, "helicopter list not emptied");
    CHECK(sim.leaving == NULL, "leaving list not emptied");
    for (int i = 0; i < BED_COUNT; i++) CHECK(sim.beds[i].occupant == NULL, "bed %d not emptied", i);
    for (int r = 0; r < ROOM_TYPES; r++) {
        CHECK(sim.rooms[r].queue == NULL && sim.rooms[r].queueCount == 0, "%s queue not emptied",
              sim.rooms[r].name);
        for (int s = 0; s < ROOM_SLOTS_MAX; s++) CHECK(sim.rooms[r].in[s] == NULL, "%s place %d not emptied",
                                                      sim.rooms[r].name, s);
    }
    CHECK(sim.treatedLog == NULL && sim.treatedLogCount == 0 && sim.treatedLogCapacity == 0,
          "treatment log not freed");

    sim_free(&sim);                  // freeing twice must be safe (everything is NULL now)
}

// Three whole days in a row, the way the program runs them.
static void test_three_days(void)
{
    Sim sim;
    sim_init(&sim, 1, NULL);
    for (int day = 1; day <= 3; day++) {
        run_shift(&sim);
        CHECK(sim.shiftOver && sim.shift == SHIFT_DAY, "day %d: the day shift didn't end", day);
        sim_start_night(&sim);
        run_shift(&sim);
        CHECK(sim.shiftOver && sim.shift == SHIFT_NIGHT, "day %d: the night shift didn't end", day);
        sim_free(&sim);
        sim_init(&sim, day + 1, NULL);
        CHECK(sim.day == day + 1 && sim.waiting == NULL, "day %d: the next day didn't start clean", day);
    }
    sim_free(&sim);
}

// The rooms a patient is sent to, as text for the log.
static void test_route_text(void)
{
    Patient p = make_patient(1, 2);
    char out[PATHWAY_LEN];
    for (int i = 0; i < ROUTE_MAX; i++) p.route[i] = ROOM_NONE;
    sim_route_text(&p, out, sizeof out);
    CHECK(strcmp(out, "Home") == 0, "no rooms: got \"%s\"", out);
    p.route[0] = ROOM_XRAY;
    sim_route_text(&p, out, sizeof out);
    CHECK(strcmp(out, "X-ray") == 0, "one room: got \"%s\"", out);
    p.route[0] = ROOM_THEATRE; p.route[1] = ROOM_WARD;
    sim_route_text(&p, out, sizeof out);
    CHECK(strcmp(out, "Theatre, Ward") == 0, "two rooms: got \"%s\"", out);
}

// ------------------------------------------------------------------------

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    SetRandomSeed(2026);             // the same "random" hospital every run

    printf("\n1. Waiting queue\n");
    run_test(test_queue_empty, "popping an empty queue gives NULL");
    run_test(test_queue_order, "most urgent first, then in arrival order");
    run_test(test_queue_ties_first_come_first_served, "same severity: first come, first served");
    run_test(test_queue_insert_positions, "insert at the front, middle and end");

    printf("\n2. Conditions loader\n");
    run_test(test_conditions_good_file, "loads a good file and skips the header");
    run_test(test_conditions_windows_line_endings, "copes with Windows line endings");
    run_test(test_conditions_bad_lines, "skips lines that don't make sense");
    run_test(test_conditions_long_name, "shortens a name that's too long");
    run_test(test_conditions_missing_file, "missing file: built-in conditions, never NULL");
    run_test(test_conditions_random_matches_severity, "random conditions match the severity");

    printf("\n3. Treatment log\n");
    run_test(test_log_grows_and_matches, "grows past 64 and matches the patients treated");
    run_test(test_log_reused_at_handover, "starts again at handover, reusing its memory");
    run_test(test_route_text, "where patients were sent, as text");

    printf("\n4. No patient lost\n");
    run_test(test_no_patient_lost, "everyone is in exactly one place, day and night");
    run_test(test_every_kind_of_day, "...on every kind of day, with power cuts and carry-over");

    printf("\n5. CSV export\n");
    run_test(test_csv_export, "one row per patient, 13 columns, fails safely");

    printf("\n6. Freeing memory\n");
    run_test(test_sim_free_empties_everything, "sim_free empties every list, bed and room");
    run_test(test_three_days, "three whole days in a row");

    printf("\n%d tests, %d passed, %d failed\n\n", testsRun, testsRun - testsFailed, testsFailed);
    return testsFailed == 0 ? 0 : 1;
}