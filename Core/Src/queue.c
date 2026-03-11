#include "queue.h"
#include "scheduler.h"
#include "semaphore.h"

//带DMA初始化
void Queue_Init_DMA(Queue_t *queue,
                    void *buffer,
                    uint32_t item_size,
                    uint32_t max_items,
                    DMA_HandleTypeDef *hdma,
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

    //DMA初始化
    queue->hdma=hdma;
    queue->dma_busy=false;
    queue->count=0;

    if (hdma!=NULL){
        Sem_Init(&queue->sem_dma_done,0,"QueueDMA");
        hdma->Parent=queue;//把队列指针保存在DMA句柄中，方便回调函数使用
    }

    if (name){
        strncpy(queue->name,name,sizeof(queue->name)-1);
        queue->name[sizeof(queue->name)-1]='\0';
    }
    else{
        queue->name[0]='\0';
    }
}

//不带DMA初始化
void Queue_Init(Queue_t *queue,
                void *buffer,
                uint32_t item_size,
                uint32_t max_items,
                const char *name){
    Queue_Init_DMA(queue,buffer,item_size,max_items,NULL,name);
}

//static表示文件内使用
static bool Queue_DMA_Transfer(Queue_t *queue,
                               void *dest,
                               const void *src,
                               uint32_t size){
    if (queue->hdma==NULL || queue->dma_busy){
        return false;//没有DMA支持、DMA繁忙
    }

    queue->dma_busy=true;

    //返回值是DMA是否启动成功
    HAL_StatusTypeDef status=HAL_DMA_Start_IT(queue->hdma,
                                              (uint32_t)src,
                                              (uint32_t)dest,
                                              size/4);//如果DMA配置为byte对齐，就不能除以4
    if (status!=HAL_OK){
        queue->dma_busy=false;
        return false;//启动失败
    }
    
    //******************************************************* */
    if (!Sem_WaitTimeout(&queue->sem_dma_done,1500)){
        //如果超时
        HAL_DMA_Abort(queue->hdma);
        queue->dma_busy=false;
        return false;
    }

    queue->dma_busy=false;
    return true;
}

//发送消息到队列，阻塞
//timeout_ms为0，表示永久等待
//          为非0，表示队列满了等待多久
bool Queue_Send(Queue_t *queue,const void *item,uint32_t timeout_ms){
    if (item==NULL){
        return false;
    }
    //等待空位
    if (timeout_ms==0){
        Sem_Wait(&queue->sem_empty);
    }
    else{
        if (!Sem_WaitTimeout(&queue->sem_empty,timeout_ms)){
            return false;
        }
    }

    //进入临界区
    Sem_Wait(&queue->sem_mutex);

    uint8_t *dest=(uint8_t*)queue->buffer+(queue->tail*queue->item_size);
    bool success=false;
    //支持DMA且数据量足够大
    if (queue->hdma!=NULL && queue->item_size>=QUEUE_DMA_THRESHOLD){
        //符合4字节对齐
        if (((uint32_t)dest%4==0)&&((uint32_t)item%4==0)&&(queue->item_size%4==0)){
            success=Queue_DMA_Transfer(queue,dest,item,queue->item_size);

            if (!success){
                //传输失败，用CPU传输
                memcpy(dest,item,queue->item_size);
                success=true;
            }
        }
        else{
            //地址不对齐，使用DMA
            memcpy(dest,item,queue->item_size);
            success=true;
        }
    }
    else{
        //使用CPU传输
        memcpy(dest,item,queue->item_size);
        success=true;
    }
    
    if (success){
        queue->tail=(queue->tail+1)%queue->max_items;
        queue->count++;
    }

    Sem_Post(&queue->sem_mutex);

    if (success){
        Sem_Post(&queue->sem_full);
    }
    else{
        Sem_Post(&queue->sem_empty);
    }

    return success;
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
    
    //进入临界区
    Sem_Wait(&queue->sem_mutex);
    
    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    bool success=false;

    if (queue->hdma!=NULL&&queue->item_size>=QUEUE_DMA_THRESHOLD){
        if (((uint32_t)item%4==0)&&((uint32_t)src%4==0)&&(queue->item_size%4==0)){
            success=Queue_DMA_Transfer(queue,item,src,queue->item_size);

            if (!success){
                //DMA失败，使用CPU传输
                memcpy(item,src,queue->item_size);
                success=true;
            }
        }
        else{
            //地址不对齐，使用CPU传输
            memcpy(item,src,queue->item_size);
            success=true;
        }
    }
    else{
        //未启用DMA或者数据量小，使用CPU传输
        memcpy(item,src,queue->item_size);
        success=true;
    }

    if (success){
        queue->head=(queue->head+1)%queue->max_items;
        queue->count--;
    }
    
    Sem_Post(&queue->sem_mutex);

    if (success){
        Sem_Post(&queue->sem_empty);
    }
    else{
        Sem_Post(&queue->sem_full);
    }

    return success;

}


//DMA传输完成回调
void Queue_DMA_CpltCallback(Queue_t *queue){
    Sem_Post(&queue->sem_dma_done);
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
    memcpy(item,src,queue->item_size);

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

/*
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

*/





