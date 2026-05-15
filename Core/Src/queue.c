#include "queue.h"

/*
带DMA初始化
在任务启动前初始化完成
*/
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
    queue->dma_error=false;
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
    Queue_Init_DMA(queue,buffer,item_size,max_items,NULL,name);//为NULL，不初始化DMA相关的
}

/*
调用者依靠互斥量保护临界区
*/
static bool Queue_DMA_Transfer(Queue_t *queue,
                               void *dest,
                               const void *src,
                               uint32_t transfer_count){//传输次数
    if (queue->hdma==NULL || queue->dma_busy){
        return false;//没有DMA支持、DMA繁忙
    }

    queue->dma_busy=true;

    //返回值是DMA是否启动成功
    HAL_StatusTypeDef status=HAL_DMA_Start_IT(queue->hdma,
                                              (uint32_t)src,
                                              (uint32_t)dest,
                                              transfer_count);
    if (status!=HAL_OK){
        queue->dma_busy=false;
        return false;//启动失败
    }
    /*
    这个信号量的初值被设置成了0，启动DMA传输时，DMA硬件在后台交换数据，当CPU执行到这句话时，
    信号量记数为0，当前任务被阻塞，当DMA传输完毕，触发中断，进入DMA回调函数，在回调函数中调用Post将
    这个信号量的记数设置为1，然后触发调度，该任务被唤醒，继续执行
    */
    while(Sem_Wait(&queue->sem_dma_done)==false);

    bool success=!queue->dma_error;//传输成功与否取决于DMA错误标志

    //重置DMA标志
    queue->dma_error=false;
    queue->dma_busy=false;
    return success;
}

/*
发送消息
依靠mutex获取临界区
*/
bool Queue_Send(Queue_t *queue,const void *item){
    if (item==NULL||queue==NULL) return false;

    if (!Sem_Wait(&queue->sem_empty)){
        return false;

    }
    
    if (!Sem_Wait(&queue->sem_mutex)){
        Sem_Post(&queue->sem_empty);
        return false;
    }

    uint8_t *dest=(uint8_t*)queue->buffer+(queue->tail*queue->item_size);

    if (queue->hdma!=NULL
        &&queue->item_size>=QUEUE_DMA_THRESHOLD
        &&(uint32_t)dest%4==0
        &&(uint32_t)item%4==0
        &&queue->item_size%4==0){
        //满足使用DMA的条件，尝试使用DMA传输，如果失败了再使用CPU memcpy
        if (!Queue_DMA_Transfer(queue,dest,item,queue->item_size/4)){
            memcpy(dest,item,queue->item_size);
        }
    }
    else{
        memcpy(dest,item,queue->item_size);
    }
    queue->tail=(queue->tail+1)%queue->max_items;
    queue->count++;
    
    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_full);

    return true;
}   

/*
接收消息
依靠mutex获取临界区
*/
bool Queue_Receive(Queue_t *queue,void *item){
    if (item==NULL||queue==NULL) return false;
    
    if (!Sem_Wait(&queue->sem_full)){
        return false;
    }

    if (!Sem_Wait(&queue->sem_mutex)){
        Sem_Post(&queue->sem_full);
        return false;
    }
    
    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    if (queue->hdma!=NULL
        &&queue->item_size>=QUEUE_DMA_THRESHOLD
        &&(uint32_t)src%4==0
        &&(uint32_t)item%4==0
        &&queue->item_size%4==0){
        if (!Queue_DMA_Transfer(queue,item,src,queue->item_size/4)){
            memcpy(item,src,queue->item_size);
        }
    }
    else{
        memcpy(item,src,queue->item_size);
    }
    queue->head=(queue->head+1)%queue->max_items;
    queue->count--;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_empty);

    return true;
}

bool Queue_SendTimeout(Queue_t *queue,const void *item,uint32_t timeout_ms){
    if (item==NULL||queue==NULL) return false;
    
    
    if (!Sem_WaitTimeout(&queue->sem_empty,timeout_ms)){
        return false;
    }
    
    if (!Sem_Wait(&queue->sem_mutex)){
        Sem_Post(&queue->sem_empty);
        return false;
    }

    uint8_t *dest=(uint8_t*)queue->buffer+(queue->tail*queue->item_size);

    if (queue->hdma!=NULL
        &&queue->item_size>=QUEUE_DMA_THRESHOLD
        &&(uint32_t)dest%4==0
        &&(uint32_t)item%4==0
        &&queue->item_size%4==0){
        //满足使用DMA的条件，尝试使用DMA传输，如果失败了再使用CPU memcpy
        if (!Queue_DMA_Transfer(queue,dest,item,queue->item_size/4)){
            memcpy(dest,item,queue->item_size);
        }
    }
    else{
        memcpy(dest,item,queue->item_size);
    }
    queue->tail=(queue->tail+1)%queue->max_items;
    queue->count++;
    
    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_full);

    return true;
}

bool Queue_ReceiveTimeout(Queue_t *queue,void *item,uint32_t timeout_ms){
    if (item==NULL||queue==NULL) return false;

    if (!Sem_WaitTimeout(&queue->sem_full,timeout_ms)){
        return false;
    }

    if (!Sem_Wait(&queue->sem_mutex)){
        Sem_Post(&queue->sem_full);
        return false;
    }
    
    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    if (queue->hdma!=NULL
        &&queue->item_size>=QUEUE_DMA_THRESHOLD
        &&(uint32_t)src%4==0
        &&(uint32_t)item%4==0
        &&queue->item_size%4==0){
        if (!Queue_DMA_Transfer(queue,item,src,queue->item_size/4)){
            memcpy(item,src,queue->item_size);
        }
    }
    else{
        memcpy(item,src,queue->item_size);
    }
    queue->head=(queue->head+1)%queue->max_items;
    queue->count--;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_empty);

    return true;
}

/*
DMA传输完成回调函数，将该信号量设置为1
*/
void Queue_DMA_CpltCallback(Queue_t *queue){
    Sem_Post(&queue->sem_dma_done);
}

/*
发送消息，非阻塞式
Trywait的是空闲位置
*/
bool Queue_TrySend(Queue_t *queue,const void *item){
    if (item==NULL||queue==NULL){
        return false;
    }
    
    //获取不到空闲位置的信号量就直接退出了
    if (!Sem_TryWait(&queue->sem_empty)){
        return false;
    }
    //获取到了空闲位置的信号量
    if (!Sem_Wait(&queue->sem_mutex)){
        Sem_Post(&queue->sem_empty);
        return false;
    }

    uint8_t *dest=(uint8_t*)queue->buffer+(queue->tail*queue->item_size);

    if (queue->hdma!=NULL
        &&queue->item_size>=QUEUE_DMA_THRESHOLD
        &&(uint32_t)dest%4==0
        &&(uint32_t)item%4==0
        &&queue->item_size%4==0){
        if (!Queue_DMA_Transfer(queue,dest,item,queue->item_size/4)){
            memcpy(dest,item,queue->item_size);
        }
    }
    else{
        memcpy(dest,item,queue->item_size);
    }
    queue->tail=(queue->tail+1)%queue->max_items;
    queue->count++;
    
    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_full);

    return true;
}


//接收消息，非阻塞
bool Queue_TryReceive(Queue_t *queue,void *item){
    if (item==NULL||queue==NULL){
        return false;
    }

    if (!Sem_TryWait(&queue->sem_full)){
        return false;
    }

    if (!Sem_Wait(&queue->sem_mutex)){
        Sem_Post(&queue->sem_full);
        return false;
    }

    uint8_t *src=(uint8_t*)queue->buffer+(queue->head*queue->item_size);
    if (queue->hdma!=NULL
        &&queue->item_size>=QUEUE_DMA_THRESHOLD
        &&(uint32_t)src%4==0
        &&(uint32_t)item%4==0
        &&queue->item_size%4==0){
        if (!Queue_DMA_Transfer(queue,item,src,queue->item_size/4)){
            memcpy(item,src,queue->item_size);
        }
    }
    else{
        memcpy(item,src,queue->item_size);
    }
    queue->head=(queue->head+1)%queue->max_items;
    queue->count--;

    Sem_Post(&queue->sem_mutex);
    Sem_Post(&queue->sem_empty);

    return true;
}

/*
查看队列头部的消息，但不移除
不使用DMA
*/
bool Queue_Peek(Queue_t *queue,void *item){
    if (item==NULL||queue==NULL){
        return false;
    }

    if (!Sem_Wait(&queue->sem_mutex)){
        return false;
    }

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
    return Sem_GetValue(&queue->sem_full);
}

//获取队列中剩余空间的数量
uint32_t Queue_GetSpace(Queue_t *queue){
    return Sem_GetValue(&queue->sem_empty);
}

//判断队列是否为空
bool Queue_IsEmpty(Queue_t *queue){
    return (Queue_GetCount(queue)==0);
}

//判断队列是否为满
bool Queue_IsFull(Queue_t *queue){
    return (Queue_GetCount(queue)==queue->max_items);
}

/*
调用该函数时，确保没有任务在等待这个队列，which is really hard
*/
void Queue_Flush(Queue_t *queue){
    if (queue==NULL){
        return;
    }
    
    if (!Sem_Wait(&queue->sem_mutex)){
        return;
    }

    queue->head=0;
    queue->tail=0;
    queue->count=0;

    Sem_Reset(&queue->sem_empty,queue->max_items);
    Sem_Reset(&queue->sem_full,0);

    Sem_Post(&queue->sem_mutex);
}

