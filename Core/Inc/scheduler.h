#ifndef __SCHEDULER_H
#define __SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct{
    uint32_t * volatile stackPtr;
    uint8_t priority;
    uint8_t state;
    uint32_t delay_ticks;
    char name[16];
} TCB_t;

extern volatile TCB_t *currentTCB;
extern volatile TCB_t *nextTCB;
extern volatile uint8_t isFirstSwitch;

#define TASK_STATE_READY      0
#define TASK_STATE_RUNNING    1
#define TASK_STATE_BLOCKED    2

#define MAX_TASK 8
#define TIME_SLICE_MS 10

#define PRIORITY_IDLE 255
#define IDLE_STACK_SIZE 64
void Scheduler_Init(void);

void Scheduler_AddTask(TCB_t *tcb,
                       uint32_t *stack,
                       uint32_t stack_size,
                       void (*taskFunc)(void),
                       uint8_t priority,
                       const char *name);


void Scheduler_Start(void);

TCB_t* SelectNextTask(void);

void Task_Delay(uint32_t ms);

void Task_Yield(void);

const char* Scheduler_GetCurrentTaskName(void);

uint8_t Scheduler_GetCPUUsage(void);










#endif /* __SCHEDULER_H */