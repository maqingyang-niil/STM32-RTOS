#include "timer.h"

static Timer_t *timer_list[MAX_TASK_4_TIMER] = {0}; //已注册定时器列表
static uint8_t timer_count = 0; //定时器数量