#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "scheduler.h"
#include "list.h"

#define TIMER_STATE_IDLE 0
#define TIMER_STATE_RUNNING 1
#define TIMER_STATE_EXPIRED 2

typedef struct Timer_t Timer_t;

typedef void (*TimerCallback_t)(struct Timer_t *timer,void *arg);

struct Timer_t{
    uint32_t period_ms;       //定时周期
    uint32_t expire_ticks;    //下次到期的tick值
    uint8_t state;            //定时器状态
    bool auto_reload;         //是否自动重载
    TimerCallback_t callback; //定时器回调函数 
    void *arg;                //回调函数参数
    ListNode_t node;
    char name[16];            //定时器名字
};

void Timer_Init(Timer_t *timer,
                uint32_t period_ms,
                bool auto_reload,
                void (*callback)(Timer_t *timer,void *arg),
                void *arg,
                const char *name);

void Timer_Start(Timer_t *timer);

void Timer_Stop(Timer_t *timer);

void Timer_Reset(Timer_t *timer);

bool Timer_IsRunning(Timer_t *timer);

void Timer_Tick(void);

void Timer_SystemInit(void);







#endif /* TIMER_H */
