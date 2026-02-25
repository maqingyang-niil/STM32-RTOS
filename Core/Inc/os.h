#ifndef __OS_H
#define __OS_H






typedef struct{
  uint32_t *stackPtr;
}TCB_t;




extern volatile TCB_t *currentTCB;
extern volatile TCB_t *nextTCB;
extern volatile uint8_t isFirstSwitch;




#endif /* __OS_H */