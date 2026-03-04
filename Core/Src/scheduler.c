#include "scheduler.h"


volatile TCB_t *currentTCB=NULL;
volatile TCB_t *nextTCB=NULL;
volatile uint8_t isFirstSwitch=1;
volatile uint32_t current_ticks=0;
static volatile uint32_t tick_count=0;
static TCB_t *taskList[MAX_TASK];    //任务列表
static uint8_t taskCount=0;          //任务数量

static TCB_t idle_tcb;
static uint32_t idle_stack[64];
static volatile uint32_t idle_counter=0;
static volatile uint32_t total_counter=0;

static uint32_t ready_bitmap=0;                   //就绪位图
static TCB_t *ready_list[PRIORITY_LEVELS]={NULL}; //每个优先级的环形列表的表尾
static TCB_t *delay_list=NULL;                    //延迟链表

//位图操作宏
#define SET_READY_BIT(priority)    (ready_bitmap|=(1U<<(priority)))
#define CLEAR_READY_BIT(priority)  (ready_bitmap&=~(1U<<(priority)))
#define IS_READY_BIT_SET(priority) (ready_bitmap&(1U<<(priority)))

//临界区操作




/*
获取当前时钟计数值
*/
uint32_t GetCurrentTicks(void){
    return current_ticks;
}

/*
加入delay_list
依赖调用者保证临界区
设置加入任务的状态
在调用这个函数之前，应当把任务唤醒的时间载入TCB
*/
static void AddToDelayList(TCB_t *tcb){
    if (tcb==NULL){
        return;
    }
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    //设置任务状态
    Task_SetState(tcb,TASK_STATE_BLOCKED);
    //如果当前delay_list中没有任务 或者 第一个任务的wake_ticks比tcb的小，插入头部
    if (delay_list==NULL || delay_list->wake_ticks>tcb->wake_ticks){
        tcb->next=delay_list;
        delay_list=tcb;
        __set_PRIMASK(primask);
        return;
    }
    TCB_t *prev=delay_list;
    TCB_t *curr=delay_list->next;

    while(curr!=NULL&&curr->wake_ticks<tcb->wake_ticks){
        prev=curr;
        curr=curr->next;
    }
    tcb->next=curr;
    prev->next=tcb;
    __set_PRIMASK(primask);
}

static inline uint32_t TickDiff(uint32_t a,uint32_t b){
    return (int32_t)(a-b);
}
/*
检查delay_list中的任务是不是应该放出来了
临界区保护操作
供systick_handler调用
如果有符合唤醒条件任务，将该任务加入ready list
*/
static void DelayListCheck(void){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    while(delay_list!=NULL && TickDiff(current_ticks,delay_list->wake_ticks)>=0){//满足条件，该移出delay_list
        TCB_t *tcb=delay_list;
        delay_list=delay_list->next;
        tcb->next=NULL;
        Task_SetState(tcb,TASK_STATE_READY);
    }
    __set_PRIMASK(primask);
}


/*
内部函数，
将任务加入就绪链表，依赖调用者保证临界区
插入任务时，要更新这个环形列表的头指针未知，始终指向尾节点（轮转）
由Task_SetState函数调用，严格临界区内操作
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
*/
TCB_t* SelectNextTask(void){
    while(ready_bitmap){
        uint8_t hightest_priority=__CLZ(__RBIT(ready_bitmap));
        TCB_t *task=ready_list[hightest_priority];
        //轮转，即移动指针指向下一个位置的tcb
        if (task!=NULL){
            ready_list[hightest_priority]=task->next;
            return task;
        }
        //不用清位的方式，因为出现这种情况已经是其他地方临界区出现问题了，用清位的方式不能从根本上解决问题。
        return &idle_tcb;
    }
    return &idle_tcb;
}

/*
设置任务状态
将任务移入对应队列
严格临界区内操作
*/
void Task_SetState(TCB_t *tcb,uint8_t new_state){
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    //tcb为空，直接退
    if (tcb==NULL){
        __set_PRIMASK(primask);
        return;
    }
    //要设置的状态和当前的状态相同，直接退
    if (new_state==tcb->state){
        __set_PRIMASK(primask);
        return;
    }
    
    if (new_state==TASK_STATE_BLOCKED){
        RemoveFromReadyList(tcb);
    }

    if (new_state==TASK_STATE_READY){
        AddToReadyList(tcb);
    }
    tcb->state=new_state;
    __set_PRIMASK(primask);
}

//空闲任务函数
static void idle_task_func(void){
    while(1){
        idle_counter++;//每次进入idle任务，加一
        __WFI();
    }
}

/*
初始化任务调度器
加入idle任务
*/
void Scheduler_Init(void){
    taskCount=0;
    currentTCB=NULL;
    nextTCB=NULL;
    isFirstSwitch=1;
    idle_counter=0;
    total_counter=0;
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
*/
void Scheduler_AddTask(TCB_t *tcb,
                       uint32_t *stack,
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
    if (name){
        strncpy(tcb->name,name,sizeof(tcb->name)-1);
        tcb->name[sizeof(tcb->name)-1]='\0';
    }
    else{
        tcb->name[0]='\0';
    }
    taskList[taskCount++]=tcb;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    AddToReadyList(tcb);
    __set_PRIMASK(primask);
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

    nextTCB=SelectNextTask();
    
    SysTick_Config(SystemCoreClock/1000); // 配置SysTick定时器
    HAL_NVIC_SetPriority(SysTick_IRQn,0,0); // 设置SysTick中断优先级
    HAL_NVIC_SetPriority(PendSV_IRQn,0xF,0);

    __enable_irq(); // 使能全局中断

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // 触发PendSV中断，开始调度
    //理论上永远不会返回这里了
    while(1){
        __NOP();
    }
}

/*
时间到了只触发调度
*/
void SysTick_Handler(void){
    HAL_IncTick();
    
    //记录全局时间
    current_ticks++;

    DelayListCheck();

    tick_count++;
    if (tick_count>=TIME_SLICE_MS){
        tick_count=0;
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    }
}


/*
任务延时
*/
void Task_Delay(uint32_t ms){
    if (ms==0) return;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    if (currentTCB){
        ((TCB_t*)currentTCB)->delay_ticks=ms;
        ((TCB_t*)currentTCB)->wake_ticks=ms+current_ticks;
        Task_SetState((TCB_t*)currentTCB,TASK_STATE_BLOCKED);
        __set_PRIMASK(primask);
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
    }
    else{
        __set_PRIMASK(primask);
        return;
    }
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
        "LDRB  R2, [R1]               \n"   // 字节读取
        "CBZ   R2, normal_switch      \n"   // R2==0 跳到正常流程
        "MOV   R2, #0                 \n"
        "STRB  R2, [R1]               \n"   // 字节写入，清除标志

        /* ========= 第一次启动 ========= */
        "first_switch:                \n"
        "PUSH  {R4, LR}               \n"   // 保存 EXC_RETURN + 对齐
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
        "MRS   R0, PSP                \n"
        "STMDB R0!, {R4-R11}          \n"
        "LDR   R1, =currentTCB        \n"
        "LDR   R1, [R1]               \n"
        "STR   R0, [R1]               \n"
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
    );
}





