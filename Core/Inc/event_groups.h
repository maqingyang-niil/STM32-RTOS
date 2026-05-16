#ifndef __EVENT_GROUPS_H
#define __EVENT_GROUPS_H


#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "scheduler.h"


typedef struct{
    volatile uint32_t bits;    //事件位
    TCB_t *waiting_list[MAX_TASK_4_EVENT_GROUPS]; //等待该事件的任务列表，最多支持MAX_TASK-1个任务等待
    uint8_t waiting_count;     //等待该事件的任务数量   
    char name[16];
}EventFlags_t;

void EventFlags_Init(EventFlags_t *ef,const char *name);

void EventFlags_Set(EventFlags_t *ef,uint32_t bits);

uint32_t EventFlags_Wait(EventFlags_t *ef,uint32_t bits,uint8_t mode,uint8_t clear);

void EventFlags_RemoveWaiting(void *ef_void, TCB_t *tcb);

uint32_t EventFlags_WaitTimeout(EventFlags_t *ef,uint32_t bits,uint8_t mode,uint8_t clear,uint32_t timeout_ms);

void EventFlags_Clear(EventFlags_t *ef, uint32_t bits);

uint32_t EventFlags_Get(EventFlags_t *ef);
#endif /* __EVENT_GROUPS_H */