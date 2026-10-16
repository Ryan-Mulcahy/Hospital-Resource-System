// patient.h - the Patient record and the severity-ordered waiting queue.
// The queue is a singly linked list kept sorted: most urgent first,
// and arrival order within the same severity.

#ifndef PATIENT_H
#define PATIENT_H
#include <stdbool.h>
#include "conditions.h"
typedef struct Patient {
    int id;
    int severity;            // 1 = emergency, 2 = urgent, 3 = routine
    float arrivalTime;       // simulation clock when they arrived
    float treatmentTotal;    // seconds of treatment needed
    float treatmentLeft;     // seconds of treatment remaining
    float assessLeft;        // seconds of nurse assessment remaining
    bool needsDoctor;        // the nurse's decision once the assessment is done
    const Condition *condition; // presenting condition, chosen to match severity
    struct Patient *next;    // next patient in the waiting queue
} Patient;

// Insert behind everyone of the same or higher urgency.
void queue_insert(Patient **head, Patient *p);

// Remove and return the most urgent patient, or NULL if nobody is waiting.
Patient *queue_pop(Patient **head);

#endif