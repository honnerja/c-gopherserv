#include "gopherConn.h"
#include "string.h"
#include "linkedList.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Memory allocation is handled by resourceStack
 */

void gopherConnInit(int epollfd, int sock, gopherConn* conn, char* serverRoot, char* hostname, char* port) {
	struct epoll_event event;
	event.data.ptr = (void*) conn;
	event.events = EPOLLIN;

	conn->serverRoot = serverRoot;
	conn->hostname = hostname;
	conn->port = port;

	conn -> state = GET_REQUEST;
	conn -> responseSent = 0;
	conn -> sock = sock;
	conn -> requestRead = 0;
	//conn -> str = strAlloc(CONN_FILE_BUF_SIZE);
	strTruncate(conn->str, 0);
	conn->contentState = CONTENT_STR;
	conn->fileLen = 0;
	conn->fd = -1;
	conn->bufferSent = 0;
	conn->next = NULL;
	conn->prev = NULL;
	conn->parent = NULL;
	conn->endString = GOPHER_END_STRING;
	conn->endStrSent = 0;
	conn->endStrLen = strlen(GOPHER_END_STRING);
	if (clock_gettime(CLOCK_MONOTONIC, &conn->lastInteract)) {
		/* better error handling later */
		perror("something has gone horribly wrong!");
		exit(23);
	}

	epoll_ctl(epollfd, EPOLL_CTL_ADD, conn -> sock, &event);
}

/* resourceStack handles memory destruction*/
void gopherConnDestroy (int epollfd, gopherConn* conn) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, conn -> sock, NULL);
	if (conn -> str != NULL) {
		//strFree(conn -> str);
	}
	conn -> state = DONE;
	close(conn -> sock);
	if (conn->fd != -1) {
	 close(conn->fd);
	}
	// handles linked list stuff
	if (conn->parent != NULL) {
		if (conn->prev != NULL) conn->prev->next = conn->next;
		if (conn->next != NULL) conn->next->prev = conn->prev;
		if (conn->parent->head == conn) conn->parent->head = conn->next;
		if (conn->parent->tail == conn) conn->parent->tail = conn->prev;
	}
	puts("conn done");
	//free(conn);
}

