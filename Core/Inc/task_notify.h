#ifndef __TASK_NOTIFY_H
#define __TASK_NOTIFY_H

#include <stdint.h>
#include "scheduler.h"

void Task_NotifySet(TCB_t *tcb, uint32_t bits);

uint32_t Task_NotifyWait(uint32_t clear_bits);

uint32_t Task_NotifyWaitTimeout(uint32_t clear_bits, uint32_t timeout_ms);

void Task_NotifyClear(TCB_t *tcb, uint32_t bits);

uint32_t Task_NotifyGet(TCB_t *tcb);

#endif /*__TASK_NOTIFY_H*/