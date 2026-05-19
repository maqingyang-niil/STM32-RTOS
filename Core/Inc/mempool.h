#ifndef __MEMPOOL_H
#define __MEMPOOL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"

typedef struct{
    void *free_list;
    uint32_t block_size;
    uint32_t block_count;
    volatile uint32_t free_count;
    uint8_t *pool_mem;             //内存池起始地址
    char name[16];
}MemPool_t;

void MemPool_Init(MemPool_t *pool,
                  void *mem,
                  uint32_t block_size,
                  uint32_t block_count,
                  const char *name);

void* MemPool_Alloc(MemPool_t *pool);

bool MemPool_Free(MemPool_t *pool, void *block);

uint32_t MemPool_GetFreeCount(MemPool_t *pool);

bool MemPool_IsFromPool(MemPool_t *pool, void *block);

#endif /* __MEMPOOL_H */