#ifndef LIST_H
#define LIST_H

#include <stdint.h>
#include <stdbool.h>

typedef struct ListNode {
    struct ListNode *next;
    struct ListNode *prev;
    uint32_t value;
    void *owner;
}ListNode_t;

typedef struct {
    ListNode_t *head;
    ListNode_t *tail;
    uint32_t count;
}List_t;

void List_Init(List_t *list);
void List_NodeInit(ListNode_t *node,void *owner);
void List_InsertHead(List_t *list,ListNode_t *node);
void List_InsertTail(List_t *list,ListNode_t *node);
void List_InsertSorted(List_t *list,ListNode_t *node);
void List_Remove(List_t *list,ListNode_t *node);
bool List_IsEmpty(List_t *list);
uint32_t List_Count(List_t *list);

#endif /* LIST_H */