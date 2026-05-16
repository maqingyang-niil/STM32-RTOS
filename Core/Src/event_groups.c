#include "event_groups.h"

void EventFlags_Init(EventFlags_t *ef, const char *name){
    ef->bits=0;
    ef->waiting_count=0;

    for (uint8_t i=0;i<MAX_TASK_4_EVENT_GROUPS;i++){
        ef->waiting_list[i]=NULL;
    }

    if (name){
        strncpy(ef->name,name,sizeof(ef->name)-1);
        ef->name[sizeof(ef->name)-1]='\0';
    }
    else{
        ef->name[0]='\0';
    }
}

void EventFlags_Set(EventFlags_t *ef,uint32_t bits){
    if (ef==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    ef->bits|=bits;
    bool need_imme_schedule=false;
    uint8_t i=0;
    while(i<ef->waiting_count){
        TCB_t *task=ef->waiting_list[i];
        
        bool satisfied=false;
        
        if (task->wait_mode==EVENT_WAIT_ANY){
            satisfied=(ef->bits & task->wait_bits)!=0;
        }
        else{
            satisfied=(ef->bits & task->wait_bits)==task->wait_bits;
        }
        if (satisfied){
            uint32_t result=ef->bits;

            if (task->wait_clear){
                ef->bits &= ~task->wait_bits;
            }
            
            task->wait_bits=result;

            for (uint8_t j=i;j<ef->waiting_count-1;j++){
                ef->waiting_list[j]=ef->waiting_list[j+1];
            }
            ef->waiting_list[ef->waiting_count-1]=NULL;
            ef->waiting_count--;
            task->waiting_on=NULL;
            task->unblock_cleanup=NULL;
            Task_SetState(task,TASK_STATE_READY);

            if (currentTCB!=NULL&&
                task->priority<((TCB_t*)currentTCB)->priority)
            {
                need_imme_schedule=true;
            }
        }
        else{
            i++;
        }
    }
    if (need_imme_schedule){
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    }
    __set_PRIMASK(primask);
}

uint32_t EventFlags_Wait(EventFlags_t *ef,
                         uint32_t bits,
                         uint8_t mode,
                         uint8_t clear){
    if (ef==NULL||bits==0) return 0;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t *)currentTCB;

    if (current==NULL){
        __set_PRIMASK(primask);
        return 0;
    }

    bool satisfied=false;
    if (mode==EVENT_WAIT_ANY){
        satisfied=(ef->bits&bits)!=0;
    }
    else{
        satisfied=(ef->bits&bits)==bits;
    }
    //条件以满足
    if (satisfied){
        uint32_t result=ef->bits;
        if (clear){
            ef->bits &= ~bits;
        }
        __set_PRIMASK(primask);
        return result;
    }

    //条件不满足，加入等待队列
    if (ef->waiting_count>=MAX_TASK_4_EVENT_GROUPS){
        __set_PRIMASK(primask);
        return 0;
    }

    current->wait_bits=bits;
    current->wait_mode=mode;
    current->wait_clear=clear;
    current->waiting_on=ef;
    current->unblock_cleanup=EventFlags_RemoveWaiting;

    ef->waiting_list[ef->waiting_count]=current;
    ef->waiting_count++;

    Task_SetState(current,TASK_STATE_BLOCKED);
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    while(current->state==TASK_STATE_BLOCKED||
          current->state==TASK_STATE_SUSPENDED){
        __WFI();
    }

    if (current->waiting_on!=NULL){
        current->waiting_on=NULL;
        current->unblock_cleanup=NULL;
        return 0;
    }

    return current->wait_bits;
}

uint32_t EventFlags_WaitTimeout(EventFlags_t *ef,
                                uint32_t bits,
                                uint8_t mode,
                                uint8_t clear,
                                uint32_t timeout_ms){
    if (ef==NULL||bits==0) return 0;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t *)currentTCB;
    if (current==NULL){
        __set_PRIMASK(primask);
        return 0;
    }

    bool satisfied=false;
    if (mode==EVENT_WAIT_ANY){
        satisfied=(ef->bits&bits)!=0;
    }
    else{
        satisfied=(ef->bits&bits)==bits;
    }

    if (satisfied){
        uint32_t result=ef->bits;
        if (clear){
            ef->bits &= ~bits;
        }
        __set_PRIMASK(primask);
        return result;
    }

    if (timeout_ms==0){
        __set_PRIMASK(primask);
        return 0;
    }

    if (ef->waiting_count>=MAX_TASK_4_EVENT_GROUPS){
        __set_PRIMASK(primask);
        return 0;
    }

    current->wait_bits=bits;
    current->wait_mode=mode;
    current->wait_clear=clear;
    current->waiting_on=ef;
    current->unblock_cleanup=EventFlags_RemoveWaiting;

    ef->waiting_list[ef->waiting_count]=current;
    ef->waiting_count++;

    current->wake_ticks=timeout_ms+GetCurrentTicks();
    Task_SetState(current,TASK_STATE_BLOCKED_TIMEOUT);
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    while(current->state==TASK_STATE_BLOCKED_TIMEOUT||
          current->state==TASK_STATE_SUSPENDED){
        __WFI();
    }

    if (current->waiting_on!=NULL){
        current->waiting_on=NULL;
        current->unblock_cleanup=NULL;
        return 0;
    }

    return current->wait_bits;
}

void EventFlags_RemoveWaiting(void *ef_void, TCB_t *tcb){
    EventFlags_t *ef=(EventFlags_t *)ef_void;
    for (uint8_t i=0;i<ef->waiting_count;i++){
        if (ef->waiting_list[i]==tcb){
            for (uint8_t j=i;j<ef->waiting_count-1;j++){
                ef->waiting_list[j]=ef->waiting_list[j+1];
            }
            ef->waiting_list[ef->waiting_count-1]=NULL;
            ef->waiting_count--;
            break;
        }
    }
}

void EventFlags_Clear(EventFlags_t *ef, uint32_t bits){
    if (ef==NULL) return;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    ef->bits &= ~bits;
    __set_PRIMASK(primask);
}

uint32_t EventFlags_Get(EventFlags_t *ef){
    if (ef==NULL) return 0;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t result = ef->bits;
    __set_PRIMASK(primask);
    return result;
}
