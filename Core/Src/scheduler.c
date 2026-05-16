#include "scheduler.h"

volatile TCB_t *currentTCB=NULL;
volatile uint8_t isFirstSwitch=1;
volatile uint32_t current_ticks=0;
static volatile uint32_t tick_count=0;    //时间记数，用于确定分片时间
static TCB_t *taskList[MAX_TASK];    //任务列表
static uint8_t taskCount=0;          //任务数量

static TCB_t idle_tcb;
static uint32_t idle_stack[64];

static uint32_t ready_bitmap=0;                   //就绪位图
static TCB_t *ready_list[PRIORITY_LEVELS]={NULL}; //每个优先级的环形列表的表尾
static TCB_t *delay_list=NULL;                    //延迟链表

//位图操作宏
#define SET_READY_BIT(priority)    (ready_bitmap|=(1U<<(priority)))
#define CLEAR_READY_BIT(priority)  (ready_bitmap&=~(1U<<(priority)))
#define IS_READY_BIT_SET(priority) (ready_bitmap&(1U<<(priority)))

//函数声明
static void Stack_CheckCurrent(void);


/*
获取当前时钟计数值
*/
uint32_t GetCurrentTicks(void){
    return current_ticks;
}

/*
加入delay_list
依赖调用者保证临界区
在调用这个函数之前，应当把任务唤醒的时间载入TCB
*/
static void AddToDelayList(TCB_t *tcb){
    if (tcb==NULL)  return;
    tcb->next=NULL;//冗余设计
    //如果当前delay_list中没有任务 或者 第一个任务的wake_ticks比tcb的小，插入头部
    if (delay_list==NULL || (int32_t)(delay_list->wake_ticks-tcb->wake_ticks)>0){
        tcb->next=delay_list;
        delay_list=tcb;
        return;
    }
    TCB_t *prev=delay_list;
    TCB_t *curr=delay_list->next;

    while(curr!=NULL&&(int32_t)(curr->wake_ticks-tcb->wake_ticks)<0){
        prev=curr;
        curr=curr->next;
    }
    tcb->next=curr;
    prev->next=tcb;
}

/*
从delay_list中移除指定任务
依赖调用者保证临界区
移除后tcb->next=NULL
*/
static void RemoveFromDelayList(TCB_t *tcb){
    if (tcb==NULL || delay_list==NULL){
        return;
    }
    // 头节点为要移除的节点
    if (delay_list==tcb){
        delay_list=tcb->next;
        tcb->next=NULL;
        return;
    }
    // 非头节点：找前驱
    TCB_t *prev=delay_list;
    while(prev->next!=NULL && prev->next!=tcb){
        prev=prev->next;
    }
    if (prev->next==NULL){
        return; // 未找到
    }
    prev->next=tcb->next;
    tcb->next=NULL;
}

/*
被DelayListCheck调用，用于确定当前delay任务和基准时钟的差值
*/
static inline int32_t TickDiff(uint32_t a,uint32_t b){
    return (int32_t)(a-b);
}

/*
检查delay_list中的任务是不是应该放出来了
内部临界区保护操作
由systick_handler调用，每毫秒执行一次
如果有符合唤醒条件任务，将该任务加入ready list
*/
static void DelayListCheck(void){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    bool need_immed_schedule=false;
    while(delay_list!=NULL && TickDiff(current_ticks,delay_list->wake_ticks)>=0){//满足条件，该移出delay_list
        TCB_t *tcb=delay_list;
        if (tcb->state==TASK_STATE_BLOCKED_TIMEOUT){
            if (tcb->unblock_cleanup!=NULL){
                tcb->unblock_cleanup(tcb->waiting_on,tcb);
            }
        }
        Task_SetState(tcb,TASK_STATE_READY);
        if (currentTCB && tcb->priority<((TCB_t*)currentTCB)->priority){
            need_immed_schedule=true;
        }
    }
    __set_PRIMASK(primask);

    if (need_immed_schedule)  SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

/*
内部函数，
将任务加入就绪链表，依赖调用者保证临界区
新任务被插入尾部，保证轮转调度的公平性
由Task_SetState函数调用，严格临界区内操作
一定确保移出后tcb->next=NULL
*/
static void AddToReadyList(TCB_t *tcb){
    if (tcb==NULL){
        return;
    }
    uint8_t priority=tcb->priority;
    SET_READY_BIT(priority);

    //该任务为当前优先级第一个任务
    if (ready_list[priority]==NULL){
        ready_list[priority]=tcb;
        tcb->next=tcb; //环形链表，指向自己
    }
    else{//当前优先级有多个任务
        TCB_t *tail=ready_list[priority];
        tcb->next=tail->next;
        tail->next=tcb;
        ready_list[priority]=tcb;
    }
}

/*
内部函数，
将任务移出就绪链表，依赖调用者保证临界区
由Task_SetState函数调用，严格临界区内操作
*/
static void RemoveFromReadyList(TCB_t *tcb){
    if (tcb==NULL){
        return;
    }
    uint8_t priority = tcb->priority;
    // 当前优先级没有任务在就序列表中
    if (ready_list[priority] == NULL){
        return;
    }
    // 单节点
    if (tcb->next == tcb){
        if (ready_list[priority] != tcb){
            return; // tcb 不在链表中
        }
        ready_list[priority] = NULL;
        CLEAR_READY_BIT(priority);
        tcb->next = NULL;
        return;
    }
    // 多节点：找前驱
    TCB_t *prev = ready_list[priority];
    TCB_t *start = prev;
    while(prev->next != tcb){
        prev = prev->next;
        if (prev == start){
            return; // 转了一圈没找到
        }
    }
    prev->next = tcb->next;
    if (ready_list[priority] == tcb){//更新指向的位置
        ready_list[priority] = tcb->next;
    }
    tcb->next = NULL;
}

/*
获取下一个任务（确保与当前任务不同）
如果出现ready_map和ready_list不相符的情况，返回idle_tcb，which is theoretically impossible
只转指针，不取出任务
取出任务的操作全部由Task_SetState在严格临界区内完成
*/
TCB_t* SelectNextTask(void){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    TCB_t *result=&idle_tcb;
    
    if (ready_bitmap){
        uint8_t hightest_priority=__CLZ(__RBIT(ready_bitmap));
        TCB_t *tail=ready_list[hightest_priority];
        if (tail!=NULL){
            TCB_t *head=tail->next;
            ready_list[hightest_priority]=head;
            if (head!=(TCB_t*)currentTCB){
                tick_count=0;
            }
            result=head;
        }
    }
    __set_PRIMASK(primask);
    return result;
}

/*
设置任务状态
将任务移入对应队列
内部严格临界区内操作
*/
void Task_SetState(TCB_t *tcb,uint8_t new_state){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    if (tcb==NULL || tcb->state==new_state){
        __set_PRIMASK(primask);
        return;
    }
    //离开旧状态
    if (tcb->state==TASK_STATE_READY)    RemoveFromReadyList(tcb);
    if (tcb->state==TASK_STATE_DELAYED)  RemoveFromDelayList(tcb);
    if (tcb->state==TASK_STATE_BLOCKED_TIMEOUT)  RemoveFromDelayList(tcb);
    //进入新状态
    if (new_state==TASK_STATE_READY)     AddToReadyList(tcb);
    if (new_state==TASK_STATE_DELAYED)   AddToDelayList(tcb);
    if (new_state==TASK_STATE_BLOCKED_TIMEOUT) AddToDelayList(tcb);
    tcb->state=new_state;
    __set_PRIMASK(primask);
}

//空闲任务函数
static void idle_task_func(void){
    while(1){
        __WFI();
    }
}

/*
初始化任务调度器
加入idle任务
无需临界区保护
*/
void Scheduler_Init(void){
    taskCount=0;
    currentTCB=NULL;
    isFirstSwitch=1;
    ready_bitmap=0;
    tick_count=0;
    for (int i=0;i<MAX_TASK;i++){
        taskList[i]=NULL;
    }
    for (int i=0;i<PRIORITY_LEVELS;i++){
        ready_list[i]=NULL;
    }
    delay_list=NULL;
    Scheduler_AddTask(&idle_tcb,
                      idle_stack,
                      IDLE_STACK_SIZE,
                      idle_task_func,
                      PRIORITY_IDLE,
                      "Idle");
}

/*
添加任务
内部局部临界区保护
*/
void Scheduler_AddTask(TCB_t *tcb,
                       uint32_t *stack, //传入数组的地址
                       uint32_t stack_size,
                       void (*taskFunc)(void),
                       uint8_t priority,
                       const char *name){
    if (taskCount>=MAX_TASK){
        return;
    }
    if (priority>=PRIORITY_LEVELS){
        priority=PRIORITY_LEVELS-2;//任何任务的优先级都要比idle task的优先级高
    }

    //栈溢出检测魔数
    for (uint8_t i=0;i<STACK_GUARD_WORDS;i++){
        stack[i]=STACK_GUARD_MAGIC;
    }
    
    uint32_t *stk=&stack[stack_size];
    *(--stk)=0x01000000;          // xPSR
    *(--stk)=(uint32_t)taskFunc;  // PC
    *(--stk)=0xFFFFFFFD;          // LR
    *(--stk)=0x12121212;          // R12
    *(--stk)=0x03030303;          // R3
    *(--stk)=0x02020202;          // R2
    *(--stk)=0x01010101;          // R1
    *(--stk)=0x00000000;          // R0

    *(--stk)=0x11111111;          // R11
    *(--stk)=0x10101010;          // R10
    *(--stk)=0x09090909;          // R9
    *(--stk)=0x08080808;          // R8
    *(--stk)=0x07070707;          // R7
    *(--stk)=0x06060606;          // R6
    *(--stk)=0x05050505;          // R5
    *(--stk)=0x04040404;          // R4

    tcb->stackPtr=stk;
    tcb->priority=priority;
    tcb->delay_ticks=0;
    tcb->wake_ticks=0;
    tcb->next=NULL;
    tcb->state=TASK_STATE_UNINIT;
    tcb->waiting_on=NULL;
    tcb->unblock_cleanup=NULL;
    tcb->stack_base=stack;
    tcb->stack_size=stack_size;
    tcb->wait_bits=0;
    tcb->wait_mode=0;
    tcb->wait_clear=0;

    if (name){
        strncpy(tcb->name,name,sizeof(tcb->name)-1);
        tcb->name[sizeof(tcb->name)-1]='\0';
    }
    else{
        tcb->name[0]='\0';
    }
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    taskList[taskCount++]=tcb;
    __set_PRIMASK(primask);
    Task_SetState(tcb,TASK_STATE_READY);
}

/*
在初始化任务调度器之后以及添加完成任务之后运行
不可被其他函数调用
*/
void Scheduler_Start(void){
    //没有任务
    __disable_irq();
    if (taskCount==0){
        while(1);
    }
    
    SysTick_Config(SystemCoreClock/1000); // 配置SysTick定时器
    HAL_NVIC_SetPriority(SysTick_IRQn,0,0); // 设置SysTick中断优先级
    HAL_NVIC_SetPriority(PendSV_IRQn,0xE,0);

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // PendSV中断是开中断后的第一个
    __enable_irq(); // 使能全局中断

    
    //理论上永远不会返回这里了
    while(1){
        __NOP();
    }
}

/*
时间到了只触发调度
*/
void SysTick_Handler(void){    
    //记录全局时间
    current_ticks++;
    //延迟列表检查
    DelayListCheck();
    //当前任务栈溢出检查
    Stack_CheckCurrent();
    //时间片计数
    tick_count++;
    if (tick_count>=TIME_SLICE_MS){
        tick_count=0;
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    }
}


/*
任务延时
AddToDelayList在task_delay中完成
*/
void Task_Delay(uint32_t ms){
    if (ms==0) return;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    if (currentTCB){
        ((TCB_t*)currentTCB)->delay_ticks=ms;
        ((TCB_t*)currentTCB)->wake_ticks=ms+current_ticks;
        Task_SetState((TCB_t*)currentTCB,TASK_STATE_DELAYED);
    }
    __set_PRIMASK(primask);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

/*
任务主动放弃cpu
*/
void Task_Yield(void){
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

/*
获取当前任务名称
*/
const char* Scheduler_GetCurrentTaskName(void){
    if (currentTCB){
        return ((TCB_t*)currentTCB)->name;
    }
    return "None";
}

/*
承担选择下一个任务的作用
*/
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        /* ========= 判断是否第一次启动 ========= */
        "LDR   R1, =isFirstSwitch     \n"
        "LDRB  R2, [R1]               \n"
        "CBZ   R2, normal_switch      \n"
        "MOV   R2, #0                 \n"
        "STRB  R2, [R1]               \n"

        /* ========= 第一次启动 ========= */
        "PUSH  {R4, LR}               \n"
        "BL    SelectNextTask         \n"
        "POP   {R4, LR}               \n"
        "LDR   R1, =currentTCB        \n"
        "STR   R0, [R1]               \n"
        "LDR   R0, [R0]               \n"
        "LDMIA R0!, {R4-R11}          \n"
        "MSR   PSP, R0                \n"
        "ORR   LR, LR, #0x04          \n"
        "BX    LR                     \n"

        /* ========= 正常切换 ========= */
        "normal_switch:               \n"
        /* 先选任务 */
        "PUSH  {R4, LR}               \n"
        "BL    SelectNextTask         \n"
        "POP   {R4, LR}               \n"

        /* 比较是否需要切换 */
        "LDR   R1, =currentTCB        \n"
        "LDR   R2, [R1]               \n"   // R2 = currentTCB
        "CMP   R0, R2                 \n"
        "BEQ   no_switch              \n"   // 相同任务，直接返回

        /* 保存旧任务上下文 */
        "MRS   R3, PSP                \n"
        "STMDB R3!, {R4-R11}          \n"
        "STR   R3, [R2]               \n"   // currentTCB->stackPtr = R3

        /* 切换到新任务 */
        "STR   R0, [R1]               \n"   // currentTCB = nextTask
        "LDR   R0, [R0]               \n"   // R0 = nextTask->stackPtr
        "LDMIA R0!, {R4-R11}          \n"
        "MSR   PSP, R0                \n"

        "no_switch:                   \n"
        "ORR   LR, LR, #0x04          \n"
        "BX    LR                     \n"
    );
}

/*
挂起任务
不能自己挂起自己
内部临界区保护
如果任务在等待信号量，清除等待队列中的该任务...
...但是waiting_on不修改，为的是区分判断信号量是...
...被正常唤醒还是被挂起后误唤醒
*/
void Task_Suspend(TCB_t *tcb){
    if (tcb==NULL)  return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    if ((TCB_t*)currentTCB==tcb||tcb->state==TASK_STATE_SUSPENDED){
        __set_PRIMASK(primask);
        return;
    }

    /*
    因为semaphore和mutex等待的任务被挂起
    清除等待队列中对应的任务
    */
    if ((tcb->state==TASK_STATE_BLOCKED||
         tcb->state==TASK_STATE_BLOCKED_TIMEOUT)&&
         tcb->unblock_cleanup!=NULL){
        tcb->unblock_cleanup(tcb->waiting_on,tcb);
    }
    Task_SetState(tcb,TASK_STATE_SUSPENDED);
    __set_PRIMASK(primask);
}

/*
挂起当前任务
立即调度
*/
void Task_SuspendSelf(void){
    if (currentTCB==NULL) return;
    Task_SetState((TCB_t*)currentTCB,TASK_STATE_SUSPENDED);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

/*
恢复任务，SUSPENDED->READY
如果被恢复的任务优先级高于当前任务，触发调度
内部临界区保护
载入ready list
*/
void Task_Resume(TCB_t *tcb){
    if (tcb==NULL) return;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    if ((TCB_t*)currentTCB!=tcb&&tcb->state==TASK_STATE_SUSPENDED){
        Task_SetState(tcb,TASK_STATE_READY);
        __set_PRIMASK(primask);
        if ((TCB_t*)currentTCB&&tcb->priority<((TCB_t*)currentTCB)->priority){
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        }
        return;
    }
    __set_PRIMASK(primask);
}

/*
只给mutex的优先级继承功能使用
内部临界区保护
*/
void Task_ChangePriority(TCB_t *tcb,uint8_t new_priority){
    if (tcb==NULL) return;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    if (tcb->priority==new_priority){
        __set_PRIMASK(primask);
        return;
    }

    bool is_ready=(tcb->state==TASK_STATE_READY);

    if (is_ready){
        RemoveFromReadyList(tcb);
    }

    tcb->priority=new_priority;

    if (is_ready){
        AddToReadyList(tcb);
    }

    __set_PRIMASK(primask);
}

/*
栈溢出检测
在systick_handler中调用，不需要额外的临界区保护
如果在其他位置调用，需要临界区保护
*/
__weak void Stack_OverflowHook(TCB_t *tcb){
    (void)tcb;
    __disable_irq();
    while(1);
}

static void Stack_CheckCurrent(void){
    TCB_t *tcb=(TCB_t*)currentTCB;
    if (tcb==NULL) return;
    
    //检测魔数
    if (tcb->stack_base[STACK_GUARD_WORDS-1]!=STACK_GUARD_MAGIC){
        Stack_OverflowHook(tcb);
        return;
    }
    //检测SP范围
    uint32_t psp;
    __asm volatile("MRS %0, PSP" : "=r" (psp) );
    uint32_t *sp=(uint32_t*)psp;
    uint32_t *stack_top=tcb->stack_base+tcb->stack_size;
    if (sp<(tcb->stack_base+STACK_GUARD_WORDS)||sp>stack_top){
        Stack_OverflowHook(tcb);
        return;
    }
}

