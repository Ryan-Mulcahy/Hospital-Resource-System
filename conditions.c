// conditions.c - loads conditions.csv and picks conditions for new patients.

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "raylib.h"          // GetRandomValue, same random source as sim.c
#include "conditions.h"

static Condition table[MAX_CONDITIONS];
static int count = 0;

// Used when the file is missing or has nothing for a severity.
// R69 is the ICD-10 code for "Illness, unspecified".
static const Condition fallback[3] = {
    { 1, "Unspecified emergency", "R69" },
    { 2, "Unspecified urgent",    "R69" },
    { 3, "Unspecified routine",   "R69" },
};

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
        Condition c;
        // " %d" skips leading spaces; %47[^,] reads the name up to the next comma.
        // The header row fails the %d and is skipped.
        if (sscanf(line, " %d , %47[^,] , %11s", &c.severity, c.name, c.icd10) != 3) continue;
        if (c.severity < 1 || c.severity > 3) continue;

        trim_end(c.name);
        trim_end(c.icd10);
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