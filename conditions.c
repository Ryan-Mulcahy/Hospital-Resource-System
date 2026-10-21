// conditions.c - loads conditions.csv and picks conditions for new patients.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "raylib.h"          // GetRandomValue, same random source as sim.c
#include "conditions.h"

static Condition table[MAX_CONDITIONS];
static int count = 0;

// Used when the file is missing or has nothing for a severity.
// R69 is the ICD-10 code for "Illness, unspecified".
static const Condition fallback[3] = {
    { 1, "Unspecified emergency", "R69", "" },
    { 2, "Unspecified urgent",    "R69", "" },
    { 3, "Unspecified routine",   "R69", "" },
};

// Cases brought in by helicopter, and which service flies each one. All emergencies.
static const struct { Condition condition; RescueService service; } rescues[] = {
    { { 1, "Hypothermia (sea rescue)",           "T68",   "ward" },         SERVICE_COAST_GUARD },
    { { 1, "Near drowning",                      "T75.1", "bloods+ward" },  SERVICE_COAST_GUARD },
    { { 1, "Fall from height (mountain rescue)", "T14.9", "theatre+ward" }, SERVICE_SEARCH_RESCUE },
    { { 1, "Multiple injuries (cliff fall)",     "T07",   "mri+theatre" },  SERVICE_SEARCH_RESCUE },
    { { 1, "Burns (house fire)",                 "T30.0", "theatre+ward" }, SERVICE_FIRE },
    { { 1, "Smoke inhalation (house fire)",      "J70.5", "xray+ward" },    SERVICE_FIRE },
};
#define RESCUE_COUNT (int)(sizeof rescues / sizeof rescues[0])

// Skip spaces and tabs at the start of a string.
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Remove spaces, tabs and line endings (\r\n from Windows) from the end.
static void trim_end(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

int conditions_load(const char *filename)
{
    count = 0;

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        printf("conditions: could not open %s, using built-in conditions\n", filename);
        return 0;
    }

    char line[256];
    while (count < MAX_CONDITIONS && fgets(line, sizeof line, f) != NULL) {
        // Split the line at its commas: severity , name , code [, pathway]
        char *comma1 = strchr(line, ',');
        char *comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;
        if (comma2 == NULL) continue;              // not three columns
        *comma1 = '\0';
        *comma2 = '\0';
        char *comma3 = strchr(comma2 + 1, ',');    // the pathway column, if there is one
        const char *pathway = "";
        if (comma3 != NULL) {
            *comma3 = '\0';
            pathway = skip_spaces(comma3 + 1);
            char *comma4 = strchr(comma3 + 1, ',');   // ignore any extra columns
            if (comma4 != NULL) *comma4 = '\0';
        }

        // strtol skips leading spaces; the header row ("severity") has no number
        char *end;
        long severity = strtol(line, &end, 10);
        if (end == line || severity < 1 || severity > 3) continue;

        Condition c;
        c.severity = (int)severity;
        // snprintf copies at most the buffer size, so an over-long name is
        // shortened rather than overflowing or making the line be skipped
        snprintf(c.name,  sizeof c.name,  "%s", skip_spaces(comma1 + 1));
        snprintf(c.icd10, sizeof c.icd10, "%s", skip_spaces(comma2 + 1));
        snprintf(c.pathway, sizeof c.pathway, "%s", pathway);
        trim_end(c.name);
        trim_end(c.icd10);
        trim_end(c.pathway);
        if (c.name[0] == '\0') continue;
        table[count++] = c;
    }

    fclose(f);
    printf("conditions: loaded %d from %s\n", count, filename);
    return count;
}

const Condition *conditions_random(int severity)
{
    if (severity < 1 || severity > 3) severity = 3;

    // Count the conditions with this severity, then pick the k-th one.
    int matches = 0;
    for (int i = 0; i < count; i++) {
        if (table[i].severity == severity) matches++;
    }
    if (matches == 0) return &fallback[severity - 1];

    int k = GetRandomValue(0, matches - 1);
    for (int i = 0; i < count; i++) {
        if (table[i].severity == severity && k-- == 0) return &table[i];
    }
    return &fallback[severity - 1];   // not reached
}

const Condition *conditions_rescue(RescueService *service)
{
    int k = GetRandomValue(0, RESCUE_COUNT - 1);
    *service = rescues[k].service;
    return &rescues[k].condition;
}

const char *rescue_service_name(RescueService service)
{
    if (service == SERVICE_COAST_GUARD)   return "Coast Guard";
    if (service == SERVICE_SEARCH_RESCUE) return "Search and Rescue";
    return "Fire Service";
}