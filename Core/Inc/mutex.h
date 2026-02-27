#ifndef __MUTEX_H
#define __MUTEX_H
#include <stdint.h>
#include "scheduler.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "stm32f4xx_hal.h"
typedef struct{
    TCB_t *owner;                    //当前拥有者
    uint8_t original_priority;       //拥有者的原始优先级
    uint8_t lock_count;              //递归计数
    TCB_t *waiting_list[MAX_TASK];   //等待队列
    uint8_t waiting_count;           //等待任务数量
    char name[16];                   //互斥锁名称

    //统计信息
    uint32_t lock_times;             //锁定次数
    uint32_t contention_count;       //争用次数
    uint32_t max_wait_time_ms;       //最长等待时间
}Mutex_t;

void Mutex_Init(Mutex_t *mutex,const char *name);

bool Mutex_Lock(Mutex_t *mutex,uint32_t timeout_ms);

bool Mutex_Unlock(Mutex_t *mutex);

bool Mutex_TryLock(Mutex_t *mutex);

bool Mutex_IsOwner(Mutex_t *mutex);

TCB_t* Mutex_GetOwner(Mutex_t *mutex);


























#endif /*__MUTEX_H*/