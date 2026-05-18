#include "semaphore.h"

extern volatile TCB_t *currentTCB;

/*
信号量初始化
在系统开始调度前初始化
*/
void Sem_Init(Semaphore_t *sem,
              uint32_t initial_count,
              const char *name){
    sem->count=initial_count;

    List_Init(&sem->waiting_list);

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
    if (sem==NULL) return false;

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

    Task_SetState(current,TASK_STATE_BLOCKED);

    current->waiting_on=sem;
    current->unblock_cleanup=Sem_RemoveWaiting;

    List_InsertTail(&sem->waiting_list,&current->wait_node);
    __set_PRIMASK(primask);

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    //SUSPEND条件让任务被挂起期间无法逃脱等待，必须等待真的POST唤醒
    while(current->state==TASK_STATE_BLOCKED
        ||current->state==TASK_STATE_SUSPENDED){
        __WFI();
    }
    
    /*
    情况1：被POST正常唤醒，waiting_on被Sem_Post设为NULL
    情况2：被SUSPEND->RESUME唤醒，waiting_on仍然指向信号量
    进入if后，说明是情况2，等待标志被清除，获取不到资源，返回false
    */
    if (current->waiting_on!=NULL){
        current->waiting_on=NULL;
        current->unblock_cleanup=NULL;
        return false;
    }

    return true;
}

/*
获取信号量，非阻塞式
内部临界区操作
*/
bool Sem_TryWait(Semaphore_t *sem){
    if (sem==NULL) return false;

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
内部临界区保护
释放信号量
*/
void Sem_Post(Semaphore_t *sem){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    // 有等待任务
    if (!List_IsEmpty(&sem->waiting_list)){
        TCB_t *task=(TCB_t*)sem->waiting_list.head->owner;
        List_Remove(&sem->waiting_list,sem->waiting_list.head);
        
        task->waiting_on=NULL;
        task->unblock_cleanup=NULL;

        Task_SetState(task,TASK_STATE_READY);
        __set_PRIMASK(primask);

        if (currentTCB&&task->priority<((TCB_t*)currentTCB)->priority){
            SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
        }
    }
    else{
        sem->count++;
        __set_PRIMASK(primask);
    }
}

bool Sem_WaitTimeout(Semaphore_t *sem, uint32_t timeout_ms){
    if (sem==NULL) return false;

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

    if (timeout_ms==0){
        __set_PRIMASK(primask);
        return false;
    }

    current->waiting_on=sem;
    current->unblock_cleanup=Sem_RemoveWaiting;
    List_InsertTail(&sem->waiting_list,&current->wait_node);

    current->wake_ticks=timeout_ms+GetCurrentTicks();
    Task_SetState(current,TASK_STATE_BLOCKED_TIMEOUT);
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    while(current->state==TASK_STATE_BLOCKED_TIMEOUT
        ||current->state==TASK_STATE_SUSPENDED){
        __WFI();
    }

    if (current->waiting_on!=NULL){
        current->waiting_on=NULL;
        current->unblock_cleanup=NULL;
        return false;
    }
    return true;
}

/*
获取当前资源值
内部临界区操作
*/
uint32_t Sem_GetValue(Semaphore_t *sem){
    if (sem==NULL) return 0;
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
    if (sem==NULL) return 0;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    uint8_t count=sem->waiting_list.count;
    __set_PRIMASK(primask);
    return count;
}

void Sem_Reset(Semaphore_t *sem,uint32_t new_count){
    if (sem==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    //决定是否触发调度
    uint8_t priority=PRIORITY_IDLE;

    while(!List_IsEmpty(&sem->waiting_list)){
        TCB_t *task=(TCB_t*)sem->waiting_list.head->owner;
        if (priority>task->priority){
            priority=task->priority;
        }
        List_Remove(&sem->waiting_list,sem->waiting_list.head);
        task->waiting_on=NULL;
        task->unblock_cleanup=NULL;

        Task_SetState(task,TASK_STATE_READY);
    }

    sem->count=new_count;
    __set_PRIMASK(primask);

    if ((TCB_t*)currentTCB!=NULL&&
    priority<((TCB_t*)currentTCB)->priority){
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    }
}

/*
由调用者保证临界区
*/
void Sem_RemoveWaiting(void *sem_void,TCB_t *tcb){
    if (sem_void==NULL||tcb==NULL) return;
    Semaphore_t *sem=(Semaphore_t*)sem_void;
    List_Remove(&sem->waiting_list,&tcb->wait_node);
}