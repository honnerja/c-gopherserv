#include "string.h"
#include "resourceStack.h"
#include <stdlib.h>
resourceStack* rstackAlloc(int numItems) {
	resourceStack* s = malloc(numItems * sizeof(gopherConn*) + sizeof(resourceStack));
	s->top = 0;
	s->size = numItems;
	return s;
}

gopherConn* rstackPop(resourceStack* s) {
	if (s->top > 0) {
		return s->resources[--s->top];
	}
	gopherConn* conn = malloc(sizeof(gopherConn));
	if (conn == NULL) return NULL;
	conn->str = strAlloc(CONN_FILE_BUF_SIZE);
	if (conn->str == NULL) {
		free(conn);
		return NULL;
	}
	return conn;
}

/* saves allocd mem if space available, if not frees conn*/
void rstackPush(resourceStack* s, gopherConn* conn) {
	if (s->top < s->size) {
		s->resources[s->top++] = conn;
	} else {
		strFree(conn->str);
		free(conn);
	}
}

void rstackFree(resourceStack* s) {
	for (int i = 0; i < s->top; i++) {
		strFree(s->resources[i]->str);
		free(s->resources[i]);
	}
	free(s);
}
