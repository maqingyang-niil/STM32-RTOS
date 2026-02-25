#include "semaphore.h"
#include "scheduler.h"
#include <string.h>
#include "stm32f4xx.h"

extern TCB_t* SelectNextTask(void);
extern volatile TCB_t *currentTCB;
extern volatile TCB_t *nextTCB;

void Sem_Init(Semaphore_t *sem,
              uint32_t initial_count,
              const char *name){
    sem->count=initial_count;
    sem->waiting_count=0;

    for (int i=0;i<MAX_TASK;i++){
        sem->waiting_list[i]=NULL;
    }

    if (name){
        strncpy(sem->name,name,sizeof(sem->name)-1);
        sem->name[sizeof(sem->name)-1]='\0';
    }
    else{
        sem->name[0]='\0';
    }
}


//获取信号量，阻塞式
bool Sem_Wait(Semaphore_t *sem){
    __disable_irq();

    //有资源
    if (sem->count>0){
        sem->count--;
        __enable_irq();
        return true;
    }
    //无资源，阻塞当前任务
    else{
        TCB_t *current=(TCB_t*)currentTCB;

        if (current==NULL){
            __enable_irq();
            return false;
        }

        if (sem->waiting_count<MAX_TASK){
            current->state=TASK_STATE_BLOCKED;
            sem->waiting_list[sem->waiting_count]=current;
            sem->waiting_count++;
            __enable_irq();
            nextTCB=SelectNextTask();
            SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
            while(current->state==TASK_STATE_BLOCKED){
                __WFI();
            }
            return true;
        }
        else{
            __enable_irq();
            return false;
        }
    }
}

//获取信号量，非阻塞式
bool Sem_TryWait(Semaphore_t *sem){
    __disable_irq();

    if (sem->count>0){
        sem->count--;
        __enable_irq();
        return true;
    }
    else{
        __enable_irq();
        return false;
    }
}

//获取信号量，带超时
bool Sem_WaitTimeout(Semaphore_t *sem,uint32_t timeout_ms){
    uint32_t start_time=HAL_GetTick();
    while(1){
        if (Sem_TryWait(sem)){
            return true;
        }
        if (HAL_GetTick()-start_time>=timeout_ms){
            return false;
        }

        Task_Delay(1);
    }
}

void Sem_Post(Semaphore_t *sem){
    __disable_irq();
    if (sem->waiting_count>0){
        TCB_t *task=sem->waiting_list[0];
        task->state=TASK_STATE_READY;

        for (uint8_t i=0;i<sem->waiting_count-1;i++){
            sem->waiting_list[i]=sem->waiting_list[i+1];
        }
        sem->waiting_list[sem->waiting_count-1]=NULL;
        sem->waiting_count--;

        if (currentTCB!=NULL){
            if (task->priority<((TCB_t*)currentTCB)->priority){
                nextTCB=task;
                __enable_irq();
                SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
                return;
            }
        }
        __enable_irq();
    }
    else{
        sem->count++;
        __enable_irq();
    }
}

uint32_t Sem_GetValue(Semaphore_t *sem){
    __disable_irq();
    uint32_t value=sem->count;
    __enable_irq();
    return value;
}


uint8_t Sem_GetWaitingCount(Semaphore_t *sem){
    __disable_irq();
    uint8_t count=sem->waiting_count;
    __enable_irq();
    return count;
}

