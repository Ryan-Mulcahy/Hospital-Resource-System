// patient.c - waiting queue operations.

#include <stddef.h>
#include "patient.h"

void queue_insert(Patient **head, Patient *p)
{
    while (*head != NULL && (*head)->severity <= p->severity) {
        head = &(*head)->next;
    }
    p->next = *head;
    *head = p;
}

Patient *queue_pop(Patient **head)
{
    Patient *p = *head;
    if (p != NULL) {
        *head = p->next;
        p->next = NULL;
    }
    return p;
}