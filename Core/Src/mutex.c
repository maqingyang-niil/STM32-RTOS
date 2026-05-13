#include "mutex.h"

//extern volatile TCB_t *currentTCB;

void Mutex_Init(Mutex_t *mutex,const char *name){
    mutex->owner=NULL;
    mutex->original_priority=0;
    mutex->lock_count=0;
    mutex->waiting_count=0;

    for (int i=0;i<MAX_TASK;i++){
        mutex->waiting_list[i]=NULL;
    }

    if (name){
        strncpy(mutex->name,name,sizeof(mutex->name)-1);
        mutex->name[sizeof(mutex->name)-1]='\0';
    }
    else{
        mutex->name[0]='\0';
    }
}

void Mutex_RemoveWaiting(void *mutex_void,TCB_t *tcb){
    Mutex_t *mutex=(Mutex_t*)mutex_void;
    for (uint8_t i=0;i<mutex->waiting_count;i++){
        if (mutex->waiting_list[i]==tcb){
            for (uint8_t j=i;j<mutex->waiting_count-1;j++){
                mutex->waiting_list[j]=mutex->waiting_list[j+1];
            }
            mutex->waiting_list[mutex->waiting_count-1]=NULL;
            mutex->waiting_count--;
            return;
        }
    }
}

bool Mutex_Lock(Mutex_t *mutex){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;
    
    //当前没有任务
    if (current==NULL){
        __set_PRIMASK(primask);
        return false;
    }

    //情况1，未被占用
    if (mutex->owner==NULL){
        mutex->owner=current;
        mutex->original_priority=current->priority;
        mutex->lock_count=1;
        __set_PRIMASK(primask);
        return true;
    }

    //情况2，递归锁
    if (mutex->owner==current){
        mutex->lock_count++;
        __set_PRIMASK(primask);
        return true;
    }

    //情况3，被其他任务占用
    if (current->priority<mutex->owner->priority){
        //提升所有者优先级
        Task_ChangePriority(mutex->owner,current->priority);
    }

    if (mutex->waiting_count<MAX_TASK_4_MUTEX){
        current->waiting_on=mutex;
        current->unblock_cleanup=Mutex_RemoveWaiting;

        mutex->waiting_list[mutex->waiting_count]=current;
        mutex->waiting_count++;
        Task_SetState(current,TASK_STATE_BLOCKED);
        __set_PRIMASK(primask);
        SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
        while(current->state==TASK_STATE_BLOCKED||
              current->state==TASK_STATE_SUSPENDED){
            __WFI();
        }
        
        if (current->waiting_on!=NULL){
            current->waiting_on=NULL;
            current->unblock_cleanup=NULL;
            return false;
        }
        return true;
    }
    else{
        //等待队列已满
        __set_PRIMASK(primask);
        return false;
    }
}

bool Mutex_TryLock(Mutex_t *mutex){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;

    //当前没有任务
    if (current==NULL){
        __set_PRIMASK(primask);
        return false;
    }

    //未被占用
    if (mutex->owner==NULL){
        mutex->owner=current;
        mutex->original_priority=current->priority;
        mutex->lock_count=1;
        __set_PRIMASK(primask);
        return true;
    }

    //递归锁
    if (mutex->owner==current){
        mutex->lock_count++;
        __set_PRIMASK(primask);
        return true;
    }

    //被其他任务占用
    __set_PRIMASK(primask);
    return false;
}

bool Mutex_Unlock(Mutex_t *mutex){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;
    //非owner不能解锁
    if (current==NULL||mutex->owner!=current){
        __set_PRIMASK(primask);
        return false;
    }

    mutex->lock_count--;

    //仍持有锁
    if (mutex->lock_count>0){
        __set_PRIMASK(primask);
        return true;
    }

    //完全释放锁,恢复优先级
    Task_ChangePriority(current,mutex->original_priority);

    if (mutex->waiting_count>0){
        TCB_t *next=mutex->waiting_list[0];
        for (uint8_t i=0;i<mutex->waiting_count-1;i++){
            mutex->waiting_list[i]=mutex->waiting_list[i+1];
        }
        mutex->waiting_list[mutex->waiting_count-1]=NULL;
        mutex->waiting_count--;

        mutex->owner=next;
        mutex->original_priority=next->priority;
        mutex->lock_count=1;

        next->waiting_on=NULL;
        next->unblock_cleanup=NULL;

        Task_SetState(next,TASK_STATE_READY);
        __set_PRIMASK(primask);

        if (next->priority<current->priority){
            SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
        }
    }
    else{
        mutex->owner=NULL;
        mutex->original_priority=0;
        mutex->lock_count=0;
        __set_PRIMASK(primask);
    }

    return true;
}

bool Mutex_IsOwner(Mutex_t *mutex){
    return mutex->owner==(TCB_t*)currentTCB;
}

TCB_t* Mutex_GetOwner(Mutex_t *mutex){
    return mutex->owner;
}
