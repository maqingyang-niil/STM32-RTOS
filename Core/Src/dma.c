#include "dma.h"


DMA_HandleTypeDef hdma_memtomem;

void MX_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();
    
    hdma_memtomem.Instance = DMA2_Stream0;
    hdma_memtomem.Init.Channel = DMA_CHANNEL_0;
    hdma_memtomem.Init.Direction = DMA_MEMORY_TO_MEMORY;
    hdma_memtomem.Init.PeriphInc = DMA_PINC_ENABLE;
    hdma_memtomem.Init.MemInc = DMA_MINC_ENABLE;
    hdma_memtomem.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_memtomem.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_memtomem.Init.Mode = DMA_NORMAL;
    hdma_memtomem.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_memtomem.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_memtomem.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_memtomem.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_memtomem.Init.PeriphBurst = DMA_PBURST_SINGLE;
    
    HAL_DMA_Init(&hdma_memtomem);
    
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}