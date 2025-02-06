#pragma once
#include <stddef.h>
typedef struct gopherConn gopherConn;

typedef struct linkedList {
	gopherConn* head;
	gopherConn* tail;
} linkedList;

void llReset(linkedList* l); 

/* 
 * also removes from any current list
 */
void llAppend (linkedList* list, gopherConn* item);
