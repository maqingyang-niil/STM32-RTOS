#include "timer.h"

static List_t timer_list;

void Timer_Init(Timer_t *timer,
                uint32_t period_ms,
                bool auto_reload,
                void (*callback)(Timer_t *timer,void *arg),
                void *arg,
                const char *name){
    if (timer==NULL) return;

    timer->period_ms=period_ms;
    timer->expire_ticks=0;
    timer->state=TIMER_STATE_IDLE;
    timer->auto_reload=auto_reload;
    timer->callback=callback;
    timer->arg=arg;

    List_NodeInit(&timer->node,timer);

    if (name){
        strncpy(timer->name, name, sizeof(timer->name)-1);
        timer->name[sizeof(timer->name)-1] = '\0';
    }
    else{
        timer->name[0] = '\0';
    }
}

void Timer_SystemInit(void){
    List_Init(&timer_list);
}

/*
内部临界区保护
运行定时器
*/
void Timer_Start(Timer_t *timer){
    if (timer==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    //如果任务已经在运行，那么重新运行
    if (timer->state==TIMER_STATE_RUNNING){
        List_Remove(&timer_list,&timer->node);
    }

    timer->expire_ticks=GetCurrentTicks()+timer->period_ms;
    timer->node.value=timer->expire_ticks;
    timer->state=TIMER_STATE_RUNNING;
    List_InsertSorted(&timer_list,&timer->node);

    __set_PRIMASK(primask);
}

/*
内部临界区保护
停止定时器
*/
void Timer_Stop(Timer_t *timer){
    if (timer==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    if (timer->state==TIMER_STATE_RUNNING){
        List_Remove(&timer_list,&timer->node);
        timer->state=TIMER_STATE_IDLE;
    }

    __set_PRIMASK(primask);
}

/*
内部临界区保护
重新触发定时器
*/
void Timer_Reset(Timer_t *timer){
    if (timer==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    if (timer->state==TIMER_STATE_RUNNING){
        List_Remove(&timer_list,&timer->node);
        timer->expire_ticks=GetCurrentTicks()+timer->period_ms;
        timer->node.value=timer->expire_ticks;
        List_InsertSorted(&timer_list,&timer->node);
    }
    else{
        timer->expire_ticks=GetCurrentTicks()+timer->period_ms;
        timer->node.value=timer->expire_ticks;
        timer->state=TIMER_STATE_RUNNING;
        List_InsertSorted(&timer_list,&timer->node);
    }

    __set_PRIMASK(primask);
}

/*

*/
bool Timer_IsRunning(Timer_t *timer){
    if (timer==NULL) return false;

    return timer->state==TIMER_STATE_RUNNING;
}

/*
由systick_handler调用
内部无临界区保护
回调函数禁止调用阻塞功能
*/
void Timer_Tick(void){
    uint32_t now=GetCurrentTicks();

    while(timer_list.head!=NULL){
        Timer_t *timer=(Timer_t*)timer_list.head->owner;

        if ((int32_t)(now-timer->expire_ticks)<0) break;

        List_Remove(&timer_list,timer_list.head);

        if (timer->auto_reload){
            timer->expire_ticks+=timer->period_ms;
            timer->node.value=timer->expire_ticks;
            List_InsertSorted(&timer_list,&timer->node);
        }
        else{
            timer->state=TIMER_STATE_EXPIRED;
        }

        if (timer->callback!=NULL){
            timer->callback(timer,timer->arg);
        }
    }
}

void Scheduler_TickHook(void){
    Timer_Tick();
}