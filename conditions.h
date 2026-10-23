// conditions.h - presenting conditions, loaded from conditions.csv at startup.
// Each patient gets a random condition that matches their severity.

#ifndef CONDITIONS_H
#define CONDITIONS_H

#define MAX_CONDITIONS     64
#define CONDITION_NAME_LEN 48
#define ICD10_LEN          12
#define PATHWAY_LEN        40

typedef struct {
    int severity;                     // 1 = emergency, 2 = urgent, 3 = routine
    char name[CONDITION_NAME_LEN];    // e.g. "Chest pain"
    char icd10[ICD10_LEN];            // e.g. "R07.9"
    char pathway[PATHWAY_LEN];        // where they go after A&E, e.g. "xray" or
                                      // "theatre+ward"; "home" or empty: straight home
    char equipment[PATHWAY_LEN];      // equipment they need, e.g. "monitor+drip";
                                      // empty: none
} Condition;

// Read conditions from a CSV file with the columns severity,condition,icd10
// and, optionally, pathway (rooms after A&E, joined with '+') and equipment
// (what they need, joined with '+': monitor, ventilator, defib, drip, wheelchair).
// The header row and any line that doesn't parse are skipped.
// Returns how many conditions were loaded (0 if the file couldn't be opened).
int conditions_load(const char *filename);

// A random condition with this severity. Never returns NULL: if no condition
// of that severity was loaded, a built-in "Unspecified" entry is returned.
const Condition *conditions_random(int severity);

// The condition with this exact name (as in conditions.csv), or NULL.
const Condition *conditions_find(const char *name);

// Who flies a rescue patient in. Each service has its own helicopter colours
// and its own message.
typedef enum { SERVICE_COAST_GUARD, SERVICE_SEARCH_RESCUE, SERVICE_FIRE, SERVICE_COUNT } RescueService;

// A condition for a patient brought in by helicopter, and which service
// brought them (a sea rescue comes with the coast guard, and so on).
const Condition *conditions_rescue(RescueService *service);

// "Coast Guard", "Search and Rescue" or "Fire Service".
const char *rescue_service_name(RescueService service);

#endif