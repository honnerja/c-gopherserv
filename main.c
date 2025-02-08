// includes
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "string.h"
#include "linkedList.h"
#include "gopherConn.h"
#include "serveRequest.h"
#include "resourceStack.h"

#define DEFAULT_PAGE_PATH "/test"
#define ERROR_PAGE_PATH "/error.test"
#define ERROR_ERROR_MSG "you even broke the error page, good job\n"
#define PAGE_READ_BUF_SIZE 1024
#define CONFIG_WORD_BUF_SIZE 256
#define MAX_THREADS 20
#define MAX_CONNECTION_BACKLOG 20
#define MAX_CONNS 500
#define MAX_EVENTS 10
/* currently this is also the max size of the dir text*/
#define CONN_FILE_BUF_SIZE 65536

// how long the epoll timeout is
#define EPOLL_TIMEOUT_MS 250

#define ACCEPT_TIMEOUT_MS 250

// period after which to end a conn for inactivity
#define CONN_INACTIVE_KICK_SECONDS .5

// number of seconds in a nanosecond
#define NANOSECOND 0.000000001

#define RSTACK_SIZE 500

/* the root dir of server*/
char* SERVER_ROOT = "../2gopherserv/";
/* this can be changed to grab the actual hostname later*/
char* HOSTNAME = "localhost";

char* PORT = "70";



int KEEP_RUNNING = 1;

void handleSigTerm(int sig) {
	if (sig != SIGINT) return;
	puts("exiting on sigint");
	KEEP_RUNNING = 0;
}


/* this is what is called on a conn deemed ready by epoll*/
void determineAction (int epollfd, gopherConn* conn) {
	if (conn -> state == GET_REQUEST) {
		getRequest(epollfd, conn);
		return;
	} else if (conn -> state == SEND_RESPONSE || conn->state == SEND_END_STRING) {
		sendResource(epollfd, conn);
	}
}

int connected = 0;
pthread_mutex_t connectedMut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ll2Mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rstackMut = PTHREAD_MUTEX_INITIALIZER;

/* connCount is used to count number of connections, decremented when connection completes
 * this function will wait on epollfd and perform the appropriate response */
void serveConns(int epollfd, struct epoll_event* events, size_t eventslen, int* connCount, linkedList* ll2, resourceStack* rstack) {
	int numConns = epoll_wait(epollfd, events, eventslen, EPOLL_TIMEOUT_MS);	
	for (int i = 0; i < numConns; i++) {
		determineAction(epollfd, (gopherConn* )events[i].data.ptr);
		enum connStates state = ((gopherConn* )(events[i].data.ptr)) -> state;
		// update interact time
		if (clock_gettime(CLOCK_MONOTONIC, &(((gopherConn* )(events[i].data.ptr))->lastInteract))) {
			perror("something gone wrong");
			exit(123);
		}
		if (state == DONE || state == ERROR) {
				
			gopherConnDestroy(epollfd, events[i].data.ptr);

			pthread_mutex_lock(&rstackMut);
			rstackPush(rstack, events[i].data.ptr);
			pthread_mutex_unlock(&rstackMut);

			pthread_mutex_lock(&connectedMut);
			*connCount -= 1;
			pthread_mutex_unlock(&connectedMut);

		 } else if (((gopherConn*)events[i].data.ptr)->parent != ll2) {	
			pthread_mutex_lock(&ll2Mut);
		 	llAppend(ll2, events[i].data.ptr);
			pthread_mutex_unlock(&ll2Mut);
		}
	}
}

typedef struct connHandleArg {
	struct epoll_event* events;
	size_t maxEvents;
	int* numConns;
	int epollfd;
	linkedList* ll1;
	linkedList* ll2;
	resourceStack* rstack;
} connHandleArg;

void timeoutConns(linkedList* ll1, linkedList* ll2, int epollfd, resourceStack* rstack) {
	struct timespec currentTime;
	if (clock_gettime(CLOCK_MONOTONIC, &currentTime)) {
		perror("failed to get time, shutting down");
		exit(123);
	}
	// conns to timeout are in ll1
	gopherConn* head = ll1->head;
	while (head != NULL) {
		gopherConn* next = head->next;
		double seconds = (double)(currentTime.tv_sec - head->lastInteract.tv_sec) + (currentTime.tv_nsec - head->lastInteract.tv_nsec) * NANOSECOND;
		if (seconds > CONN_INACTIVE_KICK_SECONDS) {
			gopherConnDestroy(epollfd, head);
			pthread_mutex_lock(&rstackMut);
			rstackPush(rstack, head);
			pthread_mutex_unlock(&rstackMut);
		} else {
			llAppend(ll2, head);
		}
		head = next;
	}
}

void* connHandleMain(void* argvoid) {
	connHandleArg* arg = argvoid;
	while (KEEP_RUNNING) {
		pthread_mutex_lock(&ll2Mut);
		timeoutConns(arg->ll1, arg->ll2, arg->epollfd, arg->rstack);
		linkedList* tmp = arg->ll1;
		arg->ll1 = arg->ll2;
		arg->ll2 = tmp;
		pthread_mutex_unlock(&ll2Mut);
		serveConns(arg->epollfd, arg->events, arg->maxEvents, arg->numConns, arg->ll2, arg->rstack);
	}
	return NULL;
}

struct epoll_event events[MAX_EVENTS];


int main(int argc, char** argv) {
	int epollfd = epoll_create1(0);
	if (argc != 2) {
		puts("usage: ./main port");
	}
	
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = handleSigTerm;
	act.sa_flags = 0;

	sigaction(SIGINT, &act, NULL);
	
	SERVER_ROOT = "../2gopherserv/";

	struct linkedList ll1;
	struct linkedList ll2;

	// declarations
	PORT = argv[1];
	int sock;
	struct addrinfo hints;
	int acceptSock;
	// initialization
	memset(&hints, 0, sizeof(struct sockaddr));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;		
	if ((sock = socket(AF_INET, SOCK_STREAM /*| SOCK_NONBLOCK*/, 0)) == -1) {
		perror("Failed to create socket");
		return 2;
	}

	struct addrinfo* res;
	// bind to an addr
	if (getaddrinfo(NULL, PORT, &hints, &res)) {
		perror("Failed to find valid local addr, shutting down");
		return 4;
	}
	struct addrinfo* save = res;
	do {
		if (!bind(sock, res->ai_addr, res->ai_addrlen)) {
			goto BIND_SUCCEED;
		}
		res = res->ai_next;
	} while (res  != NULL);

	perror("failed to bind to addr!");
	return 5;
	
BIND_SUCCEED:
	freeaddrinfo(save);
	if (listen(sock, MAX_CONNECTION_BACKLOG) == -1) {
		perror("Failed to listen on socket");
		return 3;
	}
	
	connHandleArg args;
	args.epollfd = epollfd;
	args.events = events;
	args.maxEvents = MAX_EVENTS;
	args.numConns = &connected;
	args.ll1 = &ll1;
	args.ll2 = &ll2;
	llReset(args.ll1);
	llReset(args.ll2);
	args.rstack = rstackAlloc(RSTACK_SIZE);

	pthread_t handleThread;
	pthread_create(&handleThread, NULL, connHandleMain, &args);

	struct pollfd acceptPoll = {sock, POLLIN, 0};
	int nfds = 1;

	while (KEEP_RUNNING) {
		int pollRet = poll(&acceptPoll, nfds, ACCEPT_TIMEOUT_MS);
		if (pollRet < 1) {
			continue;
		}
		// accept connection and add to epollfd if conns < maxconns
		if (connected < MAX_CONNS && ((acceptSock = accept(sock, NULL, NULL)) != -1)) {
			pthread_mutex_lock(&rstackMut);
			gopherConn* conn = rstackPop(args.rstack); 
			pthread_mutex_unlock(&rstackMut);
			pthread_mutex_lock(&ll2Mut);
			gopherConnInit(epollfd, acceptSock, conn, SERVER_ROOT, HOSTNAME, PORT);
			llAppend(args.ll2, conn);
			pthread_mutex_unlock(&ll2Mut);
			pthread_mutex_lock(&connectedMut);
			connected++;
			pthread_mutex_unlock(&connectedMut);
		}
		//serveConns(epollfd, events, MAX_EVENTS, &connected);
		printf("conns: %i\n", connected);
	}
	rstackFree(args.rstack);
	close(sock);
	close(epollfd);
	pthread_join(handleThread, NULL);
	return 0;
}
