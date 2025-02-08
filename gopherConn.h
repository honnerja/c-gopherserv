#pragma once
#include "reader.h"
#include <stddef.h>
#include <time.h>
struct linkedList;
typedef struct string string;

#define MAX_REQUEST 2048
#define GOPHER_END_STRING ".\r\n"

enum connStates {
	ERROR,
	DONE,
	GET_REQUEST,
	SEND_RESPONSE,
	SEND_END_STRING
};

enum contentStates {
	CONTENT_STR,
	CONTENT_FILE
};


/* the request buf is where the client request is read into
 */
typedef struct gopherConn {
	enum connStates state;
	enum contentStates contentState;
	string* str;
	char* endString;
	int endStrSent;
	int endStrLen;
	int fd;
	size_t responseSent;
	size_t fileLen;
	size_t bufferSent;
	int sock;
	char requestBuf[MAX_REQUEST];
	size_t requestRead;
	struct gopherConn* next;
	struct gopherConn* prev;
	struct linkedList* parent;
	/* last send or recv */
	struct timespec lastInteract;
	char* serverRoot;
	char* hostname;
	char* port;
	reader fileReader;
} gopherConn;

/*
 * Memory allocation is handled by resourceStack
 */
void gopherConnInit(int epollfd, int sock, gopherConn* conn, char* serverRoot, char* hostname, char* port); 


/* resourceStack handles memory destruction*/
void gopherConnDestroy (int epollfd, gopherConn* conn); 



