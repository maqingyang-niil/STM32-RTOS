#include "mempool.h"

void MemPool_Init(MemPool_t *pool,
                  void *mem,
                  uint32_t block_size,
                  uint32_t block_count,
                  const char *name){
    if (pool==NULL||mem==NULL||block_count==0) return;

    if (block_size<sizeof(void*)){
        block_size=sizeof(void*);
    }

    pool->block_size  = block_size;
    pool->block_count = block_count;
    pool->free_count  = block_count;
    pool->pool_mem    = (uint8_t*)mem;

    for (uint32_t i=0;i<block_count-1;i++){
        void **block=(void**)(pool->pool_mem+i*block_size);
        *block=pool->pool_mem+(i+1)*block_size;
    }
    void **last=(void**)(pool->pool_mem+(block_count-1)*block_size);
    *last=NULL;

    pool->free_list=pool->pool_mem;

    if (name) {
        strncpy(pool->name, name, sizeof(pool->name) - 1);
        pool->name[sizeof(pool->name) - 1] = '\0';
    } else {
        pool->name[0] = '\0';
    }
}

void* MemPool_Alloc(MemPool_t *pool){
    if (pool==NULL||pool->free_list==NULL) return NULL;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    void *block=pool->free_list;
    pool->free_list=*(void**)block;
    pool->free_count--;

    __set_PRIMASK(primask);
    return block;
}

bool MemPool_IsFromPool(MemPool_t *pool, void *block){
    if (pool==NULL||block==NULL) return false;
    uint8_t *b=(uint8_t*)block;
    uint8_t *end=pool->pool_mem+pool->block_size*pool->block_count;
    if (b<pool->pool_mem||b>=end) return false;

    return ((b-pool->pool_mem)%pool->block_size)==0;
}

bool MemPool_Free(MemPool_t *pool, void *block){
    if (pool==NULL||block==NULL) return false;
    
    if (!MemPool_IsFromPool(pool,block)) return  false;

    uint32_t primask=__get_PRIMASK();
    __disable_irq();

    *(void**)block=pool->free_list;
    pool->free_list=block;
    pool->free_count++;

    __set_PRIMASK(primask);
    return true;
}

uint32_t MemPool_GetFreeCount(MemPool_t *pool){
    if (pool==NULL) return 0;
    return pool->free_count;
}

