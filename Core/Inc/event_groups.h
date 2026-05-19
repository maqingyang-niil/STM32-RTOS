#ifndef __EVENT_GROUPS_H
#define __EVENT_GROUPS_H


#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "scheduler.h"
#include "list.h"

#define EVENT_WAIT_ANY 0              //等待事件中的任意一个事件发生
#define EVENT_WAIT_ALL 1              //等待事件中的所有事件发生

typedef struct{
    volatile uint32_t bits;    //事件位
    List_t waiting_list;  
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