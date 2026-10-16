// roster.c - the staff pools and the rota.
// Each shift takes the next staff from the pool, wrapping round at the end,
// so nobody works two shifts in a row and everyone gets days and nights.

#include "roster.h"

static const char *NURSE_POOL[] = {
    "Aoife", "Ciaran", "Niamh", "Sean", "Orla", "Darragh", "Meabh", "Conor", "Grainne"
};
static const char *DOCTOR_POOL[] = {
    "Byrne", "Walsh", "Kelly", "Murphy", "Nolan"
};
#define NURSE_POOL_SIZE  (int)(sizeof NURSE_POOL / sizeof NURSE_POOL[0])
#define DOCTOR_POOL_SIZE (int)(sizeof DOCTOR_POOL / sizeof DOCTOR_POOL[0])

int roster_shift_number(int day, ShiftType shift)
{
    return (day - 1) * 2 + (shift == SHIFT_NIGHT ? 1 : 0);
}

// Where in the pool a shift starts: everyone used by the shifts before it.
static int pool_start(int shiftNumber, int perShift, int poolSize)
{
    return (shiftNumber * perShift) % poolSize;
}

const char *roster_nurse(int shiftNumber, int k)
{
    int start = pool_start(shiftNumber, NURSE_COUNT, NURSE_POOL_SIZE);
    return NURSE_POOL[(start + k) % NURSE_POOL_SIZE];
}

const char *roster_doctor(int shiftNumber, int k)
{
    int start = pool_start(shiftNumber, DOCTOR_COUNT, DOCTOR_POOL_SIZE);
    return DOCTOR_POOL[(start + k) % DOCTOR_POOL_SIZE];
}

void roster_assign(Sim *sim)
{
    int s = roster_shift_number(sim->day, sim->shift);
    for (int k = 0; k < NURSE_COUNT; k++)  sim->nurses[k].name  = roster_nurse(s, k);
    for (int k = 0; k < DOCTOR_COUNT; k++) sim->doctors[k].name = roster_doctor(s, k);
}