#include "event_groups.h"



void EventFlags_Init(EventFlags_t *ef, const char *name){
    if (ef==NULL) return;
    ef->bits=0;

    List_Init(&ef->waiting_list);

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
    ListNode_t *curr=ef->waiting_list.head;
    while(curr!=NULL){
        ListNode_t *next=curr->next;
        TCB_t *task=(TCB_t*)curr->owner;
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
            List_Remove(&ef->waiting_list, curr);
            task->waiting_on=NULL;
            task->unblock_cleanup=NULL;
            Task_SetState(task, TASK_STATE_READY);

            if (currentTCB!=NULL&&
                task->priority<((TCB_t*)currentTCB)->priority)
            {
                need_imme_schedule=true;
            }
        }
        curr=next;
    }
    if (need_imme_schedule){
        SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
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

    current->wait_bits=bits;
    current->wait_mode=mode;
    current->wait_clear=clear;
    current->waiting_on=ef;
    current->unblock_cleanup=EventFlags_RemoveWaiting;
    List_InsertTail(&ef->waiting_list,&current->wait_node);

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

    current->wait_bits=bits;
    current->wait_mode=mode;
    current->wait_clear=clear;
    current->waiting_on=ef;
    current->unblock_cleanup=EventFlags_RemoveWaiting;

    List_InsertTail(&ef->waiting_list,&current->wait_node);

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
    if (ef_void==NULL||tcb==NULL) return;
    EventFlags_t *ef=(EventFlags_t *)ef_void;
    List_Remove(&ef->waiting_list,&tcb->wait_node);
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
