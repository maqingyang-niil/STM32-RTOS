#ifndef __QUEUE_H
#define __QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "semaphore.h"


typedef struct{
    void *buffer;
    uint32_t item_size;        //单个元素的字节数
    uint32_t max_items;
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;   //队列中已有元素的数量

    Semaphore_t sem_empty;
    Semaphore_t sem_full;
    Semaphore_t sem_mutex;

    char name[16];
}Queue_t;


void Queue_Init(Queue_t *queue,
                void *buffer,
                uint32_t item_size,
                uint32_t max_items,
                const char *name);

bool Queue_Send(Queue_t *queue,const void *item,uint32_t timeout_ms);

bool Queue_Receive(Queue_t *queue,void *item,uint32_t timeout_ms);

bool Queue_TrySend(Queue_t *queue,const void *item);

bool Queue_TryReceive(Queue_t *queue,void *item);

bool Queue_Peek(Queue_t *queue,void *item);

uint32_t Queue_count(Queue_t *queue);

uint32_t Queue_GetSpace(Queue_t *queue);

bool Queue_IsEmpty(Queue_t *queue);

bool Queue_IsFull(Queue_t *queue);

void Queue_Flush(Queue_t *queue);

void Queue_PrintStatus(Queue_t *queue);



#endif /* __QUEUE_H */