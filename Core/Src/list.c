#include "list.h"

void List_Init(List_t *list){
    if (list==NULL) return;
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

void List_NodeInit(ListNode_t *node,void *owner){
    if (node==NULL) return;
    node->next = NULL;
    node->prev = NULL;
    node->value = 0;
    node->owner = owner;
}

void List_InsertHead(List_t *list,ListNode_t *node){
    if (list==NULL||node==NULL) return;
    node->prev=NULL;
    node->next=list->head;
    if (list->head){
        list->head->prev=node;
    }
    else{
        list->tail=node;
    }
    
    list->head=node;
    list->count++;
}

void List_InsertTail(List_t *list,ListNode_t *node){
    if (list==NULL||node==NULL) return;
    node->next = NULL;
    node->prev = list->tail;
    if (list->tail){
        list->tail->next=node;
    }
    else{
        list->head = node;
    }
    list->tail = node;
    list->count++;
}
void List_Remove(List_t *list,ListNode_t *node){
    if (list==NULL||node==NULL||list->count==0) return;

    if (node->prev!=NULL){
        node->prev->next=node->next;
    }
    else{
        list->head=node->next;
    }

    if (node->next!=NULL){
        node->next->prev=node->prev;
    }
    else{
        list->tail=node->prev;
    }
    node->next=NULL;
    node->prev=NULL;
    list->count--;
}

void List_InsertSorted(List_t *list,ListNode_t *node){
    if (list==NULL||node==NULL) return;
    if (list->head==NULL||node->value<list->head->value){
        List_InsertHead(list,node);
        return;
    }

    ListNode_t *curr=list->head->next;

    while(curr!=NULL&&curr->value<=node->value){
        curr=curr->next;
    }

    if (curr==NULL){
        List_InsertTail(list,node);
        return;
    }

    node->next=curr;
    node->prev=curr->prev;
    curr->prev->next=node;
    curr->prev=node;
    list->count++;
}

bool List_IsEmpty(List_t *list){
    if (list==NULL) return true;
    return list->count==0;
}

uint32_t List_Count(List_t *list){
    if (list==NULL) return 0;
    return list->count;
}