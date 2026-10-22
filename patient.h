// patient.h - the Patient record and the severity-ordered waiting queue.
// The queue is a singly linked list kept sorted: most urgent first,
// and arrival order within the same severity.

#ifndef PATIENT_H
#define PATIENT_H
#include <stdbool.h>
#include "conditions.h"

#define ROUTE_MAX 3          // the most rooms a patient visits after A&E

typedef struct Patient {
    int id;
    int severity;            // 1 = emergency, 2 = urgent, 3 = routine
    float arrivalTime;       // simulation clock when they arrived
    float treatmentTotal;    // seconds of treatment needed
    float treatmentLeft;     // seconds of treatment remaining
    float assessLeft;        // seconds of nurse assessment remaining
    float waitedFor;         // seconds from arriving to getting a bed
    bool needsDoctor;        // the nurse's decision once the assessment is done
    const Condition *condition; // presenting condition, chosen to match severity
    bool rescue;             // brought in by helicopter
    int rescueService;       // which service flew them in (a RescueService)
    int route[ROUTE_MAX];    // rooms to visit after A&E, in order (-1 = none)
    int routeStep;           // which of them they're on
    float roomLeft;          // seconds left in the room they're in now
    float walkLeft;          // seconds until they reach their bed or room; nothing
                             // starts (assessment, treatment, a scan) until they're there
    unsigned equipNeeds;     // equipment they need (bits: 1 << EquipType, see sim.h)
    unsigned equipHeld;      // equipment they have with them now
    unsigned equipMissing;   // equipment they're waiting for (0: not waiting)
    float equipWaitStart;    // clock time they started waiting for it (< 0: not waiting)
    bool doneInAE;           // treatment in A&E finished; waiting for a wheelchair to leave
    struct Patient *next;    // next patient in the waiting queue (or a room's queue)
} Patient;

// Insert behind everyone of the same or higher urgency.
void queue_insert(Patient **head, Patient *p);

// Remove and return the most urgent patient, or NULL if nobody is waiting.
Patient *queue_pop(Patient **head);

#endif