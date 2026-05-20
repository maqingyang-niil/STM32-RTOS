#include "task_notify.h"

void Task_NotifySet(TCB_t *tcb, uint32_t bits){
    if (tcb==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    tcb->notify_value|=bits;
    tcb->notify_pending=1;

    //任务的waiting on是自己就说明任务在等任务通知
    if (tcb->waiting_on==tcb){
        tcb->waiting_on=NULL;
        Task_SetState(tcb,TASK_STATE_READY);

        if (currentTCB!=NULL&&tcb->priority<((TCB_t*)currentTCB)->priority){
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        }
    }
    __set_PRIMASK(primask);
}

uint32_t Task_NotifyWait(uint32_t clear_bits){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    TCB_t *current=(TCB_t*)currentTCB;
    if (current==NULL){
        __set_PRIMASK(primask);
        return 0;
    }

    if (current->notify_pending){
        uint32_t val=current->notify_value;
        current->notify_value&=~clear_bits;
        current->notify_pending=0;
        __set_PRIMASK(primask);
        return val;
    }

    current->waiting_on=current;
    Task_SetState(current,TASK_STATE_BLOCKED);
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    while (current->state == TASK_STATE_BLOCKED ||
           current->state == TASK_STATE_SUSPENDED){
        __WFI();
    }

    primask=__get_PRIMASK();
    __disable_irq();
    
    current->waiting_on=NULL;
    uint32_t val = current->notify_value;
    current->notify_value &= ~clear_bits;
    current->notify_pending = 0;
    __set_PRIMASK(primask);

    return val;
}

uint32_t Task_NotifyWaitTimeout(uint32_t clear_bits, uint32_t timeout_ms){
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    TCB_t *current = (TCB_t*)currentTCB;
    if (current == NULL) {
        __set_PRIMASK(primask);
        return 0;
    }

    if (current->notify_pending){
        uint32_t val=current->notify_value;
        current->notify_value&=~clear_bits;
        current->notify_pending=0;
        __set_PRIMASK(primask);
        return val;
    }

    if (timeout_ms==0){
        __set_PRIMASK(primask);
        return 0;
    }

    current->waiting_on=current;
    current->wake_ticks=GetCurrentTicks()+timeout_ms;
    Task_SetState(current,TASK_STATE_BLOCKED_TIMEOUT);
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    while(current->state==TASK_STATE_BLOCKED_TIMEOUT||
          current->state==TASK_STATE_SUSPENDED){
        __WFI();
    }

    primask=__get_PRIMASK();
    __disable_irq();

    //pending为0，没收到通知，清waiting_on，返回
    if (!current->notify_pending){
        current->waiting_on=NULL;
        __set_PRIMASK(primask);
        return 0;
    }
    
    //pending为1，收到通知，读值返回
    uint32_t val = current->notify_value;
    current->notify_value &= ~clear_bits;
    current->notify_pending = 0;
    __set_PRIMASK(primask);
    return val;
}

void Task_NotifyClear(TCB_t *tcb, uint32_t bits){
    if (tcb==NULL) return;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    tcb->notify_value&=~bits;
    __set_PRIMASK(primask);
}

uint32_t Task_NotifyGet(TCB_t *tcb){
    if (tcb == NULL) return 0;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t val = tcb->notify_value;
    __set_PRIMASK(primask);
    return val;
}