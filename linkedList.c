#include "linkedList.h"
#include "gopherConn.h"

void llReset(linkedList* l) {
	l->head = NULL;
	l->tail = NULL;
}

/* 
 * also removes from any current list
 */
void llAppend (linkedList* list, gopherConn* item) {
	if (item->parent != NULL) {
		if (item == item->parent->head) item->parent->head = NULL;
		if (item == item->parent->tail) item->parent->tail= NULL;
	}
	if (list->tail == NULL && list->head == NULL) {
		if (item->prev != NULL) item->prev->next = item->next;
		if (item->next != NULL) item->next->prev = item->prev;
		item->next = NULL;
		item->prev = NULL;
		list->head = item;
		list->tail = item;
	}  else {
		if (item->prev != NULL) item->prev->next = item->next;
		if (item->next != NULL) item->next->prev = item->prev;
		item->next = NULL;
		item->prev = list->tail;
		list->tail->next = item;
		list->tail = item;
	}

	item->parent = list;
}
