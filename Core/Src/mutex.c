#include "mutex.h"

void Mutex_Init(Mutex_t *mutex,const char *name){
    if (mutex==NULL) return;
    mutex->owner=NULL;
    mutex->original_priority=0;
    mutex->lock_count=0;

    List_Init(&mutex->waiting_list);

    if (name){
        strncpy(mutex->name,name,sizeof(mutex->name)-1);
        mutex->name[sizeof(mutex->name)-1]='\0';
    }
    else{
        mutex->name[0]='\0';
    }
}

/*
从mutex的等待队列中移除指定任务
*/
void Mutex_RemoveWaiting(void *mutex_void,TCB_t *tcb){
    if (mutex_void==NULL||tcb==NULL) return;
    Mutex_t *mutex=(Mutex_t*)mutex_void;
    List_Remove(&mutex->waiting_list,&tcb->wait_node);
}


bool Mutex_Lock(Mutex_t *mutex){
    if (mutex==NULL) return false;

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

    current->waiting_on=mutex;
    current->unblock_cleanup=Mutex_RemoveWaiting;
    
    List_InsertTail(&mutex->waiting_list,&current->wait_node);

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

bool Mutex_LockTimeout(Mutex_t *mutex,uint32_t timeout_ms){
    if (mutex==NULL) return false;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;

    if (current==NULL){
        __set_PRIMASK(primask);
        return false;
    }

    if (mutex->owner==NULL){
        mutex->owner=current;
        mutex->original_priority=current->priority;
        mutex->lock_count=1;
        __set_PRIMASK(primask);
        return true;
    }

    if (mutex->owner==current){
        mutex->lock_count++;
        __set_PRIMASK(primask);
        return true;
    }

    if (timeout_ms==0){
        __set_PRIMASK(primask);
        return false;
    }

    if (current->priority<mutex->owner->priority){
        Task_ChangePriority(mutex->owner,current->priority);
    }

    current->waiting_on=mutex;
    current->unblock_cleanup=Mutex_RemoveWaiting;

    List_InsertTail(&mutex->waiting_list,&current->wait_node);
    current->wake_ticks=timeout_ms+GetCurrentTicks();
    Task_SetState(current,TASK_STATE_BLOCKED_TIMEOUT);
    __set_PRIMASK(primask);
    SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
    while(current->state==TASK_STATE_BLOCKED_TIMEOUT||
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

bool Mutex_TryLock(Mutex_t *mutex){
    if (mutex==NULL) return false;

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
    if (mutex==NULL) return false;
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

    if (!List_IsEmpty(&mutex->waiting_list)){
        TCB_t *task=(TCB_t*)mutex->waiting_list.head->owner;

        List_Remove(&mutex->waiting_list,mutex->waiting_list.head);

        mutex->owner=task;
        mutex->original_priority=task->priority;
        mutex->lock_count=1;

        task->waiting_on=NULL;
        task->unblock_cleanup=NULL;

        Task_SetState(task,TASK_STATE_READY);
        __set_PRIMASK(primask);

        if (task->priority<current->priority){
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
    if (mutex==NULL) return false;
    return mutex->owner==(TCB_t*)currentTCB;
}

TCB_t* Mutex_GetOwner(Mutex_t *mutex){
    if (mutex==NULL) return NULL;
    return mutex->owner;
}
