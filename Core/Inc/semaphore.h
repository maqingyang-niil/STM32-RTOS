#ifndef __SEMAPHORE_H
#define __SEMAPHORE_H

#include <stdint.h>
#include <stdbool.h>
#include "scheduler.h"


typedef struct{
    volatile uint32_t count;
    TCB_t *waiting_list[MAX_TASK];
    volatile uint8_t waiting_count;
    char name[16];
}Semaphore_t;

void Sem_Init(Semaphore_t *sem,
              uint32_t initial_count,
              const char *name);

bool Sem_Wait(Semaphore_t *sem);

bool Sem_TryWait(Semaphore_t *sem);

bool Sem_WaitTimeout(Semaphore_t *sem, uint32_t timeout_ms);

void Sem_Post(Semaphore_t *sem);

uint32_t Sem_GetValue(Semaphore_t *sem);

uint8_t Sem_GetWaitingCount(Semaphore_t *sem);





















#endif /* __SEMAPHORE_H */