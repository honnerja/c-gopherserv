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

#define MAX_REQUEST 2048
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

#define GOPHER_END_STRING ".\r\n"



/* the root dir of server*/
char* SERVER_ROOT = ".";
/* this can be changed to grab the actual hostname later*/
char* HOSTNAME = "localhost";

char* PORT = "70";

int KEEP_RUNNING = 1;

void handleSigTerm(int sig) {
	if (sig != SIGINT) return;
	puts("exiting on sigint");
	KEEP_RUNNING = 0;
}

typedef struct string {
	size_t size;
	size_t len;
	char content[];
} string;

string* strAlloc(size_t len) {
	string* s = malloc(sizeof(string) + len);
	if (len > 1000) puts("big str alloc");
	s->size = len;
	s->len = 0;
	return s;
}

void strFree(string* str) {
	free(str);
}

/* returns the amount of chars that could be appended */
size_t strAppend(string* str, char* content, size_t len) {
	if (len + str->len > str->size) {
		len = str->size - str->len;
	}
	memcpy(str->content + str->len, content, len);
	str->len += len;
	return len;
}

/* truncates string
 * returns 0 on success, -1 on fail
 */
int strTruncate(string* str, size_t len) {
	if (len > str->size) {
		return -1;
	}
	if (len < str->len) {
		str->len = len;
	}
	return 0;
}

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

struct linkedList;

/* the request buf is where the client request is read into
 * f is opened after request is read, it represents the file that is requested
 * f is null before open
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
} gopherConn;

/*
 * Memory allocation is handled by resourceStack
 */
void gopherConnInit(int epollfd, int sock, gopherConn* conn) {
	struct epoll_event event;
	event.data.ptr = (void*) conn;
	event.events = EPOLLIN;

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

typedef struct linkedList {
	gopherConn* head;
	gopherConn* tail;
} linkedList;

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

typedef struct resourceStack {
	int top;
	size_t size;
	gopherConn* resources[];
} resourceStack;

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
	puts("allocd conn!");
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


/*can be expanded as needed*/
char getTypeByHeuristic (string* path) {
	struct stat fileStat;
	if (stat(path->content, &fileStat)) {
		// if stat fails to get info
		return '0';
	}	
	int type = fileStat.st_mode & S_IFMT;
	if (type == S_IFDIR) return '1';
	return '0';
}

/* appends to string, on failure to read entire file, resets string to orig length*/
/* 0 on succeed, 1 on error*/
int file2String (int fd, string* str, size_t maxStrSize) {
	const int bufSize = 512;
	const char* infoLineCruft = "		null.host	1";
	size_t strOrigLen = str->len;
	// alloc during conn, avoid these, this could probably be avoided by using str as buffer
	char* buf = malloc(bufSize);
	int justBeenNewLine = 1;
	int rawLine = 0;
	while (1) {
		int readCode = read(fd, buf, bufSize);
		if (readCode == -1) {
			str->len = strOrigLen;
			return 1;
		}
		for (int i = 0; i < readCode; i++) {
			if (justBeenNewLine) {
				strAppend(str, "i", 1);
				justBeenNewLine = 0;
				if (i + 1 < readCode && buf[i] == '\t' && buf[i + 1] == '\t') {
					str->len -= 1;
					i += 1;
					rawLine = 1;
					continue;
				}
			}
			if (buf[i] == '\n') {
				if (!rawLine) strAppend(str, infoLineCruft, strlen(infoLineCruft));
				strAppend(str, "\r\n", 2);
				justBeenNewLine = 1;
				rawLine = 0;
				continue;
			}
			if (buf[i] == '\t') {
				buf[i] = ' ';
			}

			strAppend(str, buf + i, 1);

				
		}
		if (readCode < bufSize) {
			break;
		}
	}
	free(buf);
	return 0;
}

void header2str(int fd, string* str, int maxStrSize, char* type, string* title) {
	/* reading 1 char at a time is maybe a bad solution but will work for now*/
	char curChar;
	int code = read(fd, &curChar, 1);
	*type = curChar;
	if (code == -1 || code == 0) {
		strTruncate(title, 0);
		*type = '\0';
		return;
	}
	if (curChar == '\n') {
		*type = '\0';
	} else {
		while (1) {
			code = read(fd, &curChar, 1);
			if (code == -1 || code == 0) {
				strTruncate(title, 0);
				*type = '\0';
				return;
			}
			if (curChar == '\n') {
				break;
			}
		}
	}
	while (1) {
		code = read(fd, &curChar, 1);
		if (code == -1 || code == 0) {
			strTruncate(title, 0);
			return;
		}
		if (curChar == '\n') {
			break;
		}
		strAppend(title, &curChar, 1);
	}
	file2String(fd, str, maxStrSize);
}

void genEntry(struct dirent* dirEntry, string* path, string* entryBuf) {
	
	// adding head
	char type = '\0';
	string* title = strAlloc(512);

	string* headPath = strAlloc(256);
	strAppend(headPath, path->content, path->len);
	strAppend(headPath, "/", 1);
	strAppend(headPath, ".", 1);
	strAppend(headPath, dirEntry->d_name, strlen(dirEntry->d_name));
	strAppend(headPath, ".head\0", 7);
	
	int headFd;
	if ((headFd = open(headPath->content, O_RDONLY)) != -1) {
		header2str(headFd, entryBuf, CONN_FILE_BUF_SIZE, &type, title);
		//strAppend(entryBuf, "\n", 1);
		close(headFd);
		strTruncate(headPath, 0);
	}
	

	strAppend(path, "/", 1);
	strAppend(path, dirEntry->d_name, strlen(dirEntry->d_name));


	if (type == '\0') type = getTypeByHeuristic(path);
	strTruncate(path, path->len - (strlen(dirEntry->d_name) + 1));

	
	strAppend(entryBuf, &type, 1); 
	if (title->len > 0) {
		strAppend(entryBuf, title->content, title->len);
	} else {
		strAppend(entryBuf, dirEntry->d_name, strlen(dirEntry->d_name));
	}

	strFree(title);
	strAppend(entryBuf, "\t", 1);
	/* create full path */ 

	strAppend(entryBuf, path->content + 1, path->len - 1);
	strAppend(entryBuf, "/", 1);
	strAppend(entryBuf, dirEntry->d_name, strlen(dirEntry->d_name));

	strAppend(entryBuf, "\t", 1);
	strAppend(entryBuf, HOSTNAME, strlen(HOSTNAME));

	strAppend(entryBuf, "\t", 1);
	strAppend(entryBuf, PORT, strlen(PORT));
	strAppend(entryBuf, "\r\n", 2);	

	// adding tail
	strAppend(headPath, path->content, path->len);
	strAppend(headPath, "/", 1);
	strAppend(headPath, ".", 1);
	strAppend(headPath, dirEntry->d_name, strlen(dirEntry->d_name));
	strAppend(headPath, ".tail\0", 7);

	int tailFd;

	if ((tailFd = open(headPath->content, O_RDONLY)) != -1) {
		file2String(tailFd, entryBuf, CONN_FILE_BUF_SIZE);
		//strAppend(entryBuf, "\r\n", 2);
		close(tailFd);
	}
	strFree(headPath);
}

/* ret 0 on success, 1 on error*/
int getFileOrDir (char* path, string* buffer, gopherConn* conn) {
	int fd = open(path, O_RDONLY, 0);

	if (fd < 0) {
		perror("bad request, file dont exist");
		return 1;
	}
	struct stat* sbuf = malloc(sizeof(struct stat));
	if (fstat(fd, sbuf) != 0) {
		perror("couldnt stat file");
		return 1;
	}

	int type = sbuf->st_mode & S_IFMT;

	/* Make path into string*/
	string* sPath = strAlloc(256);
	strAppend(sPath, path, strlen(path));

	
	// switch for file or dir
	if (type == S_IFDIR) {
		// handle dir
		DIR* dir = fdopendir(fd);
		if (dir == NULL) {
			return 1;
		}
		struct dirent* dirEntry = NULL;
		// handling of head generation
		int headFd;
		string* headPath = strAlloc(256);
		strAppend(headPath, path, strlen(path));
		strAppend(headPath, "/.head\0", 7);
		if ((headFd = open(headPath->content, O_RDONLY)) != -1) {
			file2String(headFd, buffer, CONN_FILE_BUF_SIZE);
			close(headFd);
			//strAppend(buffer, "\r\n", 2);
			strTruncate(headPath, 0);
		}
		while ((dirEntry = readdir(dir)) != NULL) {
			if (dirEntry->d_name[0] != '.') {
				genEntry(dirEntry, sPath, buffer);
			}
		}

		// handling of tail generation
		int tailFd;
		strAppend(headPath, path, strlen(path));
		strAppend(headPath, "/.tail\0", 7);
		if ((tailFd = open(headPath->content, O_RDONLY)) != -1) {
			file2String(tailFd, buffer, CONN_FILE_BUF_SIZE);
			close(headFd);
		}
		strFree(headPath);
		closedir(dir);
		conn->contentState = CONTENT_STR;
		close(fd);
		
	} else {
		int bufSize = sbuf->st_size < CONN_FILE_BUF_SIZE ? sbuf->st_size : CONN_FILE_BUF_SIZE;
		conn->fileLen = sbuf->st_size;
		//strTruncate(conn->str, bufSize);
		conn->str->len = 0;
		conn->contentState = CONTENT_FILE;
		conn->fd = fd;
	}

	free(sbuf);
	strFree(sPath);
	return 0;

}

/* gets listing of directory contents at path or file contents if file
 * returns listing of dir requested, default page, or error page
*/
void getDirListing (char* request, size_t requestLen, string* str, gopherConn* conn) {
	
	char prevchar = '\0';
	// prevents access of hidden files, or of files outside server root
	for (int i = 0; i < requestLen && request[i] != '\0'; i++) {
		if ((prevchar == '\0' || prevchar == '/') && request[i] == '.') {
			goto DIR_ERROR;
		}
		prevchar = request[i];
	}
	string* fullPath = strAlloc(256);
	strAppend(fullPath, SERVER_ROOT, strlen(SERVER_ROOT));
	strAppend(fullPath, request, requestLen);
	strAppend(fullPath, "\0", 1);

	if (getFileOrDir(fullPath->content, str, conn)) {
DIR_ERROR:
		strTruncate(fullPath, 0);
		strAppend(fullPath, SERVER_ROOT, strlen(SERVER_ROOT));
		strAppend(fullPath, ERROR_PAGE_PATH, strlen(ERROR_PAGE_PATH));
		strAppend(fullPath, "\0", 1);

		strTruncate(str, 0);
		if (getFileOrDir(fullPath->content, str, conn)) {
			strTruncate(str, 0);
			strAppend(str, ERROR_ERROR_MSG, strlen(ERROR_ERROR_MSG));
		}
	}

	//puts("done dirlist!");
	strFree(fullPath);
	return;
}

void getRequest (int epollfd, gopherConn* conn) {
	int recvd;
	if ((recvd = recv(conn -> sock, conn -> requestBuf + conn -> requestRead, MAX_REQUEST - conn -> requestRead, 0)) < 1) {
		conn -> state = ERROR;
		return;
	}
	conn -> requestRead += recvd;
	if (conn -> requestRead >= 2 && conn -> requestBuf[conn -> requestRead - 1] == '\n' && conn -> requestBuf[conn -> requestRead - 2] == '\r') {
		conn -> requestBuf[conn -> requestRead - 2] = '\0';
		printf("Request: %s\n", conn->requestBuf);
		char* fname = NULL;
		if (conn -> requestRead == 2) {
			fname = DEFAULT_PAGE_PATH;
		} else {
			fname = conn -> requestBuf;
		}
		getDirListing(fname, strlen(fname), conn -> str, conn);	
		/* after request is recvd, we only care about sending data*/
		struct epoll_event event;
		event.data.ptr = (void*) conn;
		event.events = EPOLLOUT;
		epoll_ctl(epollfd, EPOLL_CTL_MOD, conn -> sock, &event);
		conn -> state = SEND_RESPONSE;
	}
}

void sendResource (int epollfd, gopherConn* conn) {
	if (conn->state == SEND_END_STRING) {
		if (conn->endStrSent == conn->endStrLen) {
			conn->state = DONE;
			return;
		}
		long sent = send(conn->sock, conn->endString + conn->endStrSent, conn->endStrLen - conn->endStrSent, MSG_DONTWAIT);
		if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			conn->state = ERROR;
			return;
		}
		conn->endStrSent += sent;
		if (conn->endStrSent == conn->endStrLen) {
			conn->state = DONE;
		}
		return;
	}
	if ((conn->contentState == CONTENT_STR && (conn -> responseSent >= conn -> str -> len)) || (conn->contentState == CONTENT_FILE && conn->responseSent >= conn->fileLen)) {
		conn -> state = SEND_END_STRING;
		return;
	}

	if (conn->contentState == CONTENT_STR) {
		long sent = send(conn -> sock, conn -> str -> content + conn -> responseSent, conn -> str -> len - conn->responseSent, MSG_DONTWAIT);
		if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			conn -> state = ERROR;
			return;
		}
		conn -> responseSent += sent;
		if (conn->responseSent >= conn->str->len) {
			conn->state = SEND_END_STRING;
			return;
		}
	} else {
			if (conn->responseSent >= conn->fileLen) {
				conn->state = SEND_END_STRING;
				return;
			}
			if (conn->str->len == 0) {
				int readCode = read(conn->fd, conn->str->content, conn->str->size);
				if (readCode < 0) {
					conn->state = ERROR;
					return;
				}
				conn->str->len = readCode;
				conn->bufferSent = 0;
			}
			long sent = send(conn -> sock, conn->str->content + conn->bufferSent, conn -> str -> len - conn->bufferSent, MSG_DONTWAIT);
			if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
				conn -> state = ERROR;
				return;
			}
			if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				return;
			}
			conn->responseSent += sent;
			conn->bufferSent += sent;
			if (conn->bufferSent >= conn->str->len) {
				strTruncate(conn->str, 0);
			}

	}
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
	/* 500 is the timeout in ms, needs to exist so server can still accept connection in a single-threaded manner*/
	/* the timeout of 1 is a terrible hack to negate the need for multithreading*/
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




/* TODO change accept socket to nonblocking io, continue work*/
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
	
	SERVER_ROOT = ".";

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
		// accept connection and spin off thread to handle if threads < max allowable
		if (connected < MAX_CONNS && ((acceptSock = accept(sock, NULL, NULL)) != -1)) {
			pthread_mutex_lock(&rstackMut);
			gopherConn* conn = rstackPop(args.rstack); 
			pthread_mutex_unlock(&rstackMut);
			pthread_mutex_lock(&ll2Mut);
			gopherConnInit(epollfd, acceptSock, conn);
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
