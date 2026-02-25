#include "queue.h"
#include "schedule.h"
#include "semaphore.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

//初始化
void Queue_Init(Queue_t *queue,
                void *buffer,
                uint32_t item_size,
                uint32_t max_items,
                const char *name){

    queue->buffer=buffer;
    queue->item_size=item_size;
    queue->max_items=max_items;
    queue->head=0;
    queue->tail=0;
    queue->count=0;
    
    //还有多个空槽没写入
    Sem_Init(&queue->sem_empty,max_items,"QueueEmpty");
    //已写入多少数据
    Sem_Init(&queue->sem_full,0,"QueueFull");
    //是否允许进入临界区
    Sem_Init(&queue->sem_mutex,1,"QueueMutex");

    if (name){
        strncpy(queue->name,name,sizeof(queue->name)-1);
        queue->name[sizeof(queue->name)-1]='\0';
    }
    else{
        queue->name[0]='\0';
    }
}

//发送消息到队列，阻塞
//timeout_ms为0，表示永久等待
//          为非0，表示队列满了等待多久
bool Queue_Send(Queue_t *queue,const void *item,uint32_t timeout_ms){
    if (item==NULL){
        return false;
    }

    if (timeout_ms==0){
        Sem_wait(&queue->sem_empty);
    }
    else{
        if (!Sem_WaitTimeout(&queue->sem_empty,timeout_ms)){
            return false;
        }
    }

    Sem_Wait(&queue->sem_mutex);

    uint8_t *dest=(uint8_t*)queue->buffer+(queue->tail*queue->item_size);
    
    memcpy(dest,item,queue->item_size);

    queue->tail=(queue->tail+1)%queue->max_items;;

    queue->count++;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_full);
    return true;
}   


//从队列接收消息,阻塞

bool Queue_Receive(Queue_t *queue,void *item,uint32_t timeout_ms){
    if (item==NULL){
        return false;
    }
    
    if (timeout_ms==0){
        Sem_Wait(&queue->sem_full);
    }
    else{
        if (!Sem_WaitTimeout(&queue->sem_full,timeout_ms)){
            return false;
        }
    }

    Sem_Wait(&queue->sem_mutex);
    
    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    memcpy(item,src,queue->item_size);

    queue->head=(queue->head+1)%queue->max_items;
    queue->count--;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_empty);
    return true;
}


//发送消息，非阻塞
bool Queue_TrySend(Queue_t *queue,const void *item){
    if (item==NULL){
        return false;
    }

    if (!Sem_TryWait(&queue->sem_empty)){
        return false;
    }

    Sem_Wait(&queue->sem_mutex);

    uint8_t *dest=(uint8_t*)queue->buffer+(queue->tail*queue->item_size);
    memcpy(dest,item,queue->item_size);

    queue->tail=(queue->tail+1)%queue->max_items;
    queue->count++;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_full);

    return true;
}


//接收消息，非阻塞
bool Queue_TryReceive(Queue_t *queue,void *item){
    if (item==NULL){
        return false;
    }

    if (!Sem_TryWait(&queue->sem_full)){
        return false;
    }

    Sem_Wait(&queue->sem_mutex);

    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    memcpy(item,src,queue-<item_size);

    queue->head=(queue->head+1)%queue->max_items;
    queue->count--;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_empty);
    return true;
}

//查看队列头部的消息，但不移除
bool Queue_Peek(Queue_t *queue,void *item){
    if (item==NULL){
        return false;
    }

    Sem_Wait(&queue->sem_mutex);

    if (queue->count==0){
        Sem_Post(&queue->sem_mutex);
        return false;
    }

    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    memcpy(item,src,queue->item_size);
    Sem_Post(&queue->sem_mutex);
    return true;
}

//获取队列中消息中的数量
uint32_t Queue_GetCount(Queue_t *queue){
    Sem_Wait(&queue->sem_mutex);
    uint32_t count=queue->count;
    Sem_Post(&queue->sem_mutex);
    return count;
}

//获取队列中剩余空间的数量
uint32_t Queue_GetSpace(Queue_t *queue){
    Sem_Wait(&queue->sem_mutex);
    uint32_t space=queue->max_items-queue->count;
    Sem_Post(&queue->sem_mutex);
    return space;
}

//判断队列是否为空
bool Queue_IsEmpty(Queue_t *queue){
    return (Queue_GetCount(queue)==0);
}

//判断队列是否为满
bool Queue_IsFull(Queue_t *queue){
    return (Queue_GetCount(queue)==queue->max_items);
}

//清空队列
void Queue_Flush(Queue_t *queue){
    Sem_Wait(&queue->sem_mutex);
    queue->head=0;
    queue->tail=0;
    queue->count=0;

    Sem_Init(&queue->sem_empty, queue->max_items, "QueueEmpty");
    Sem_Init(&queue->sem_full, 0, "QueueFull");

    Sem_Post(&queue->sem_mutex);
}

void Queue_PrintStatus(Queue_t *queue) {
    Sem_Wait(&queue->sem_mutex);
    
    printf("\r\n========== Queue Status: %s ==========\r\n", queue->name);
    printf("Item size:    %lu bytes\r\n", queue->item_size);
    printf("Capacity:     %lu items\r\n", queue->max_items);
    printf("Current:      %lu items\r\n", queue->count);
    printf("Space left:   %lu items\r\n", queue->max_items - queue->count);
    printf("Head index:   %lu\r\n", queue->head);
    printf("Tail index:   %lu\r\n", queue->tail);
    float usage = (float)queue->count / queue->max_items * 100;
    printf("Usage:        %.1f%%\r\n", usage);
    
    // 可视化
    printf("Buffer: [");
    for(uint32_t i = 0; i < queue->max_items; i++) {
        if(queue->count == 0) {
            printf("_");
        } else if(queue->head < queue->tail) {
            if(i >= queue->head && i < queue->tail) {
                printf("#");
            } else {
                printf("_");
            }
        } else if(queue->head > queue->tail) {
            if(i >= queue->head || i < queue->tail) {
                printf("#");
            } else {
                printf("_");
            }
        } else {
            // head == tail
            if(queue->count == queue->max_items) {
                printf("#");
            } else {
                printf("_");
            }
        }
    }
    printf("]\r\n");
    printf("         ");
    for(uint32_t i = 0; i < queue->max_items; i++) {
        if(i == queue->head && i == queue->tail) {
            printf("^");  // head 和 tail 在同一位置
        } else if(i == queue->head) {
            printf("H");  // head
        } else if(i == queue->tail) {
            printf("T");  // tail
        } else {
            printf(" ");
        }
    }
    printf("\r\n");
    printf("==========================================\r\n\r\n");
    
    Sem_Post(&queue->sem_mutex);
}








