#ifndef __SCHEDULER_H
#define __SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"

typedef struct TCB_t{
    uint32_t * volatile stackPtr;        //栈指针
    uint8_t priority;
    uint8_t state;
    uint32_t delay_ticks;
    uint32_t wake_ticks;                 //唤醒时间
    char name[16];
    struct TCB_t *next;                  //同优先级任务链表
    void *waiting_on;                    //等待信号量的是否被正常唤醒标志
    void (*unblock_cleanup)(void*,struct TCB_t *);
    uint32_t *stack_base;                //栈底地址，用于检测栈溢出
    uint32_t stack_size;                 //栈大小
} TCB_t;

extern volatile TCB_t *currentTCB;
extern volatile uint8_t isFirstSwitch;
extern volatile uint32_t current_ticks;
#define TASK_STATE_READY              0
#define TASK_STATE_SUSPENDED          1
#define TASK_STATE_BLOCKED            2
#define TASK_STATE_DELAYED            3
#define TASK_STATE_BLOCKED_TIMEOUT    4
#define TASK_STATE_UNINIT     0xFF

#define MAX_TASK          32
#define MAX_TASK_4_MUTEX  MAX_TASK-1
#define TIME_SLICE_MS     10      //时间片长度

#define PRIORITY_IDLE     31      //任何任务的优先级都要比idle_task高
#define PRIORITY_LEVELS   32      //优先级级数

#define IDLE_STACK_SIZE   64      //空闲任务栈大小
#define STACK_GUARD_MAGIC 0xDEADBEFF  //栈溢出检测魔数，放在栈底，正常情况下不应该被覆盖
#define STACK_GUARD_WORDS 4       //栈溢出检测保护字数量，放在栈底，正常情况下不应该被覆盖

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

void Task_SetState(TCB_t *tcb,uint8_t new_state);

void Task_Yield(void);

const char* Scheduler_GetCurrentTaskName(void);

uint32_t GetCurrentTicks(void);

void Task_Suspend(TCB_t *tcb);

void Task_SuspendSelf(void);

void Task_Resume(TCB_t *tcb);

//只能给mutex使用
void Task_ChangePriority(TCB_t *tcb,uint8_t new_priority);


#endif /* __SCHEDULER_H */