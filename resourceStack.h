#pragma once
#define CONN_FILE_BUF_SIZE 65536
typedef struct gopherConn gopherConn;

typedef struct resourceStack {
	int top;
	size_t size;
	gopherConn* resources[];
} resourceStack;

resourceStack* rstackAlloc(int numItems);

gopherConn* rstackPop(resourceStack* s); 

/* saves allocd mem if space available, if not frees conn*/
void rstackPush(resourceStack* s, gopherConn* conn); 

void rstackFree(resourceStack* s);
