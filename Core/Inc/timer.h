#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "scheduler.h"

typedef void (*TimerCallback_t)(struct Timer_t *timer,void *arg);

typedef struct Timer_t{
    uint32_t period_ms;       //定时周期
    uint32_t expire_ticks;    //下次到期的tick值
    uint8_t state;            //定时器状态
    bool auto_reload;         //是否自动重载
    TimerCallback_t callback; //定时器回调函数 
    void *arg;                //回调函数参数
    char name[16];            //定时器名字
}Timer_t;








#endif /* TIMER_H */
