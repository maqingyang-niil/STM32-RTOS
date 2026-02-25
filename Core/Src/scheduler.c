#include "scheduler.h"
#include "stm32f4xx.h"
#include <string.h>

volatile TCB_t *currentTCB=NULL;
volatile TCB_t *nextTCB=NULL;
volatile uint8_t isFirstSwitch=1;

static TCB_t *taskList[MAX_TASK];    //任务列表
static uint8_t taskCount=0;          //任务数量

static TCB_t idle_tcb;
static uint32_t idle_stack[64];
static volatile uint32_t idle_counter=0;
static volatile uint32_t total_counter=0;


//空闲任务函数
static void idle_task_func(void){
    while(1){
        idle_counter++;//每次进入idle任务，加一
        __WFI();
    }
}


//初始化任务调度器
void Scheduler_Init(void){
    taskCount=0;
    currentTCB=NULL;
    nextTCB=NULL;
    isFirstSwitch=1;
    idle_counter=0;
    total_counter=0;

    for (int i=0;i<MAX_TASK;i++){
        taskList[i]=NULL;
    }

    Scheduler_AddTask(&idle_tcb,
                      idle_stack,
                      IDLE_STACK_SIZE,
                      idle_task_func,
                      PRIORITY_IDLE,
                      "Idle");
}


//添加任务
void Scheduler_AddTask(TCB_t *tcb,
                       uint32_t *stack,
                       uint32_t stack_size,
                       void (*taskFunc)(void),
                       uint8_t priority,
                       const char *name){
    if (taskCount>=MAX_TASK) return;
    uint32_t *stk=&stack[stack_size-1];
    *(stk)=0x01000000;            // xPSR
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
    tcb->state=TASK_STATE_READY;
    tcb->delay_ticks=0;
    
    if (name){
        strncpy(tcb->name,name,sizeof(tcb->name)-1);
        tcb->name[sizeof(tcb->name)-1]='\0';
    }
    else{
        tcb->name[0]='\0';
    }

    taskList[taskCount++]=tcb;
}


TCB_t* SelectNextTask(void){
    TCB_t *highest=NULL;
    uint8_t highest_priority=0xFF;

    for (uint8_t i=0;i<taskCount;i++){
        if (taskList[i]->state==TASK_STATE_READY){
            if (taskList[i]->priority<highest_priority){
                highest_priority=taskList[i]->priority;
                highest=taskList[i];
            }
        }
    }

    if (highest==NULL){
        highest=&idle_tcb;
    }
    return highest;
}

//启动调度器
void Scheduler_Start(void){
    //没有任务
    if (taskCount==0){
        while(1);
    }

    nextTCB=SelectNextTask();
    
    SysTick_Config(SystemCoreClock/1000); // 配置SysTick定时器
    HAL_NVIC_SetPriority(SysTick_IRQn,0,0); // 设置SysTick中断优先级
    HAL_NVIC_SetPriority(PendSV_IRQn,0xF,0);

    __enable_irq(); // 使能全局中断

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // 触发PendSV中断，开始调度

    while(1){
        __NOP();
    }
}





void SysTick_Handler(void){
    HAL_IncTick();
    
    total_counter++;//每次触发systick中断记录一次


    //更新任务延时时间
    for (uint8_t i=0;i<taskCount;i++){
        if (taskList[i]->state==TASK_STATE_BLOCKED){
            if (taskList[i]->delay_ticks>0){
                taskList[i]->delay_ticks--;
                if (taskList[i]->delay_ticks==0){
                    taskList[i]->state=TASK_STATE_READY;

                    if (currentTCB!=NULL){
                        if (taskList[i]->priority<((TCB_t*)currentTCB)->priority){
                            nextTCB=taskList[i];
                            SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
                            return;
                        }
                    }
                }
            }
        }
    }

    static uint32_t tick_count=0;
    tick_count++;

    if (tick_count>=TIME_SLICE_MS){
        tick_count=0;
        nextTCB=SelectNextTask();
        if (nextTCB!=currentTCB){
            SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
        }
    }
}

void Task_Delay(uint32_t ms){
    if (ms==0) return;
    
    __disable_irq();

    if (currentTCB){
        ((TCB_t*)currentTCB)->state=TASK_STATE_BLOCKED;
        ((TCB_t*)currentTCB)->delay_ticks=ms;
        nextTCB=SelectNextTask();
        __enable_irq();
        SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
        while(((TCB_t*)currentTCB)->state==TASK_STATE_BLOCKED){
            __WFI();
        }
    }
    else{
        __enable_irq();
    }
}


void Task_Yield(void){
    nextTCB=SelectNextTask();
    if (nextTCB!=currentTCB){
        SCB->ICSR|=SCB_ICSR_PENDSVSET_Msk;
    }
}

const char* Scheduler_GetCurrentTaskName(void){
    if (currentTCB){
        return ((TCB_t*)currentTCB)->name;
    }
    return "None";
}

uint8_t Scheduler_GetCPUUsage(void) {
    static uint32_t last_idle = 0;
    static uint32_t last_total = 0;
    
    __disable_irq();
    uint32_t current_idle = idle_counter;
    uint32_t current_total = total_counter;
    __enable_irq();
    
    uint32_t idle_delta = current_idle - last_idle;
    uint32_t total_delta = current_total - last_total;
    
    last_idle = current_idle;
    last_total = current_total;
    
    if(total_delta == 0) return 0;
    
    // CPU使用率 = (1 - 空闲时间/总时间) * 100
    uint32_t usage = 100 - ((idle_delta * 100) / total_delta);
    
    return (uint8_t)usage;
}


__attribute__((naked)) void PendSV_Handler(void){
  __asm volatile(
    "LDR R2, =isFirstSwitch\n" // 加载isFirstSwitch的地址到R2
    "LDRB R3, [R2]\n"          // 加载isFirstSwitch的值到R3
    "CBZ R3,not_first\n"       // 如果isFirstSwitch为0，则转到正常流程

    "MOVS R3, #0\n"            // 将isFirstSwitch设置为0
    "STRB R3, [R2]\n"          // 存储isFirstSwitch的值回内存
    "B load_context\n"         // 跳转到加载上下文的部分

    "not_first:\n"
    "MRS R0, PSP\n"            // 获取当前任务的PSP值
    "STMDB R0!, {R4-R11}\n"    // 将R4-R11寄存器的值压入当前任务的栈中

    "LDR R1, =currentTCB\n"    // 加载currentTCB的地址到R1
    "LDR R1, [R1]\n"           // 加载currentTCB的值到R1
    "STR R0, [R1]\n"           // 将更新后的PSP值存储到currentTCB中

    "load_context:\n"
    "LDR R1, =nextTCB\n"       // 加载nextTCB的地址到R1
    "LDR R1, [R1]\n"           // 加载nextTCB的值到R1
    "LDR R0, [R1]\n"           // 加载下一个任务的栈指针到R0

    "LDMIA R0!, {R4-R11}\n"    // 从下一个任务的栈中弹出R4-R11寄存器的值
    "MSR PSP, R0\n"            // 更新PSP寄存器

    "LDR R2, =currentTCB\n"    // 更新currentTCB为当前正在运行的任务的TCB
    "STR R1, [R2]\n"

    "ORR LR, LR, #0x04\n"      // 设置EXC_RETURN值，返回到线程模式并使用PSP
    "BX LR\n"                  // 返回到被切换的任务
  );
}





