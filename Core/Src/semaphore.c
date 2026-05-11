#include "semaphore.h"

extern volatile TCB_t *currentTCB;

/*
必须在系统开始调度前完成
*/
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

/*
获取信号量，阻塞式
内部临界区操作
*/
bool Sem_Wait(Semaphore_t *sem){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;
    if (current==NULL){
        __set_PRIMASK(primask);
        return false;
    }

    if (sem->count>0){
        sem->count--;
        __set_PRIMASK(primask);
        return true;
    }
    
    if (sem->waiting_count>=MAX_TASK){
        __set_PRIMASK(primask);
        return false;
    }

    Task_SetState(current,TASK_STATE_BLOCKED);
    sem->waiting_list[sem->waiting_count]=current;
    sem->waiting_count++;
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    while(current->state==TASK_STATE_BLOCKED){
        __WFI();
    }
    return true;
}

/*
获取信号量，非阻塞式
内部临界区操作
*/
bool Sem_TryWait(Semaphore_t *sem){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;
    if (current==NULL){
        __set_PRIMASK(primask);
        return false;
    }
    
    if (sem->count>0){
        sem->count--;
        __set_PRIMASK(primask);
        return true;
    }

    __set_PRIMASK(primask);
    return false;
}

/*
释放信号量
*/
void Sem_Post(Semaphore_t *sem){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    // FIFO唤醒
    if (sem->waiting_count>0){
        TCB_t *task=sem->waiting_list[0];

        for (uint8_t i=0;i<sem->waiting_count-1;i++){
            sem->waiting_list[i]=sem->waiting_list[i+1];
        }
        sem->waiting_list[sem->waiting_count-1]=NULL;
        sem->waiting_count--;
        Task_SetState(task,TASK_STATE_READY);
        __set_PRIMASK(primask);
        if (currentTCB&&task->priority<((TCB_t*)currentTCB)->priority){
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        }
    }
    else{
        sem->count++;
        __set_PRIMASK(primask);
    }
}

bool Sem_WaitTimeout(Semaphore_t *sem, uint32_t timeout_ms){
    return true;
}

/*
获取当前资源值
内部临界区操作
*/
uint32_t Sem_GetValue(Semaphore_t *sem){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    uint32_t value=sem->count;
    __set_PRIMASK(primask);
    return value;
}

/*
获取等待资源的任务数量
内部临界区操作
*/
uint8_t Sem_GetWaitingCount(Semaphore_t *sem){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    uint8_t count=sem->waiting_count;
    __set_PRIMASK(primask);
    return count;
}

void Sem_Reset(Semaphore_t *sem,uint32_t new_count){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    while(sem->waiting_count>0){
        TCB_t *task=sem->waiting_list[0];
        for (uint8_t i=0;i<sem->waiting_count-1;i++){
            sem->waiting_list[i]=sem->waiting_list[i+1];
        }
        sem->waiting_list[sem->waiting_count-1]=NULL;
        sem->waiting_count--;
        Task_SetState(task,TASK_STATE_READY);
    }
    
    sem->count=new_count;
    __set_PRIMASK(primask);
}