#ifndef LIST_H
#define LIST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct ListNode ListNode_t;
typedef struct List List_t;

struct ListNode {
    ListNode_t *next;
    ListNode_t *prev;
    uint32_t value;
    void *owner;              //指向拥有这个节点的对象，通常是TCB_t
    List_t *container;   //拥有这个节点的列表
};

struct List {
    ListNode_t *head;
    ListNode_t *tail;
    uint32_t count;
};

void List_Init(List_t *list);
void List_NodeInit(ListNode_t *node,void *owner);
void List_InsertHead(List_t *list,ListNode_t *node);
void List_InsertTail(List_t *list,ListNode_t *node);
void List_InsertSorted(List_t *list,ListNode_t *node);
void List_Remove(List_t *list,ListNode_t *node);
bool List_IsEmpty(List_t *list);
uint32_t List_Count(List_t *list);

#endif /* LIST_H */