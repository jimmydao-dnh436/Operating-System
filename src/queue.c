/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL || q->size >= MAX_QUEUE_SIZE) {
                return;
        }
        
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (q == NULL || q->size == 0) {
                return NULL;
        }

        struct pcb_t *p = q->proc[0];
        //Doi phan tu qua trai
        for (int i = 0; i < q->size - 1; i++){
                q->proc[i] = q->proc[i+1];
        }
        q->proc[q->size - 1] = NULL;
        q->size--;

        return p;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (q == NULL || q->size == 0 || proc == NULL) {
                return NULL;
        }
        //Duyet tim proc can xoa
        for (int i = 0; i < q->size; i++){
                //Tim thay
                if (q->proc[i] == proc){

                        struct pcb_t *purged = proc;
                        //Doi phan tu qua trai bat dau tu i
                        for (int j = i; j < q->size - 1; j++){
                                q->proc[j] = q->proc[j+1];
                        }
                        q->proc[q->size - 1] = NULL;
                        q->size--;

                        return purged;
                }
        }

        return NULL;
}