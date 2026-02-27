#include "mutex.h"
#include <stdint.h>
#include "scheduler.h"

extern volatile TCB_t *currentTCB;
extern volatile TCB_t *nextTCB;
extern TCB_t* SelectNextTask(void);


void Mutex_Init(Mutex_t *mutex,const char *name){
    mutex->owner=NULL;
    mutex->original_priority=0;
    mutex->lock_count=0;
    mutex->waiting_count=0;

    for (int i=0;i<MAX_TASK;i++){
        mutex->waiting_list[i]=NULL;
    }
    mutex->lock_times=0;
    mutex->contention_count=0;
    mutex->max_wait_time_ms=0;

    if (name){
        strncpy(mutex->name,name,sizeof(mutex->name)-1);
        mutex->name[sizeof(mutex->name)-1]='\0';
    }
    else{
        mutex->name[0]='\0';
    }
}

bool Mutex_Lock(Mutex_t *mutex,uint32_t timeout_ms){
    uint32_t start_time=HAL_GetTick();

    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;
    

    //当前没有任务
    if (current==NULL){
        __enable_irq();
        return false;
    }
    //情况1，未被占用
    if (mutex->owner==NULL){
        mutex->owner=current;
        mutex->original_priority=current->priority;
        mutex->lock_count=1;
        mutex->lock_times++;
        __enable_irq();
        return true;
    }
    //情况2，递归锁
    if (mutex->owner==current){
        mutex->lock_count++;
        __enable_irq();
        return true;
    }
    //情况3，被其他任务占用
    mutex->contention_count++;

    if (current->priority<mutex->owner->priority){
        //临时提升所有者优先级,确保拿着锁的任务尽快尽快运行完
        mutex->owner->priority=current->priority;
        //通过优先级继承抬高了拿着锁的任务，就要立刻把他送到CPU上运行
        //以便尽快释放锁
        nextTCB=SelectNextTask();
        if (nextTCB!=currentTCB){
            __enable_irq();
            SCB->ICSR=SCB_ICSR_PENDSVSET_Msk;
            __disable_irq();
        }
    }


    if (mutex->waiting_count<MAX_TASK){
        mutex->waiting_list[mutex->waiting_count]=current;
        mutex->waiting_count++;

        current->state=TASK_STATE_BLOCKED;
        __enable_irq();
        nextTCB=SelectNextTask();
        SCB->ICSR=SCB_ICSR_PENDSVSET_Msk;

        while(current->state==TASK_STATE_BLOCKED){
            if (timeout_ms>0){
                uint32_t elapsed=HAL_GetTick()-start_time;
                if (elapsed>=timeout_ms){
                    __disable_irq();

                    for (int i=0;i<mutex->waiting_count;i++){
                        if (mutex->waiting_list[i]==current){
                            for (int j=i;j<mutex->waiting_count-1;j++){
                                mutex->waiting_list[j]=mutex->waiting_list[j+1];
                            }
                            mutex->waiting_count--;
                            break;
                        }
                    }

                    current->state=TASK_STATE_READY;
                    __enable_irq();
                }
            }
            __WFI();
        }
        uint32_t wait_time=HAL_GetTick()-start_time;
        if (wait_time>mutex->max_wait_time_ms){
            mutex->max_wait_time_ms=wait_time;
        }
        return true;
    }
    else{
        //等待队列已满
        __enable_irq();
        return false;
    }
}