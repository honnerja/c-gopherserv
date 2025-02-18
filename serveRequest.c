#include "serveRequest.h"
#include "gopherConn.h"
#include "string.h"
#include <string.h>
#include <errno.h>
#include <unistd.h> 
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>


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
int file2String (int fd, string* str) {
	const int bufSize = 512;
	const char* infoLineCruft = "		null.host	1";
	size_t strOrigLen = str->len;
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

void header2str(int fd, string* str, char* type, string* title) {
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
	file2String(fd, str);
}

void genEntry(struct dirent* dirEntry, char* request, string* path, string* entryBuf, char* port, char* hostname) {
	
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
		header2str(headFd, entryBuf, &type, title);
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

	strAppend(entryBuf, request, strlen(request));
	strAppend(entryBuf, "/", 1);
	strAppend(entryBuf, dirEntry->d_name, strlen(dirEntry->d_name));

	strAppend(entryBuf, "\t", 1);
	strAppend(entryBuf, hostname, strlen(hostname));

	strAppend(entryBuf, "\t", 1);
	strAppend(entryBuf, port, strlen(port));
	strAppend(entryBuf, "\r\n", 2);	

	// adding tail
	strAppend(headPath, path->content, path->len);
	strAppend(headPath, "/", 1);
	strAppend(headPath, ".", 1);
	strAppend(headPath, dirEntry->d_name, strlen(dirEntry->d_name));
	strAppend(headPath, ".tail\0", 7);

	int tailFd;

	if ((tailFd = open(headPath->content, O_RDONLY)) != -1) {
		file2String(tailFd, entryBuf);
		//strAppend(entryBuf, "\r\n", 2);
		close(tailFd);
	}
	strFree(headPath);
}

/* ret 0 on success, 1 on error*/
int getFileOrDir (char* path, char* request, string* buffer, gopherConn* conn) {
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
			file2String(headFd, buffer);
			close(headFd);
			//strAppend(buffer, "\r\n", 2);
			strTruncate(headPath, 0);
		}
		while ((dirEntry = readdir(dir)) != NULL) {
			if (dirEntry->d_name[0] != '.') {
				genEntry(dirEntry, request, sPath, buffer, conn->port, conn->hostname);
			}
		}

		// handling of tail generation
		int tailFd;
		strAppend(headPath, path, strlen(path));
		strAppend(headPath, "/.tail\0", 7);
		if ((tailFd = open(headPath->content, O_RDONLY)) != -1) {
			file2String(tailFd, buffer);
			close(headFd);
		}
		strFree(headPath);
		closedir(dir);
		conn->contentState = CONTENT_STR;
		close(fd);
		
	} else {
		//int bufSize = sbuf->st_size < CONN_FILE_BUF_SIZE ? sbuf->st_size : CONN_FILE_BUF_SIZE;
		//conn->fileLen = sbuf->st_size;
		//strTruncate(conn->str, bufSize);
		conn->str->len = 0;
		conn->contentState = CONTENT_FILE;
		conn->fd = fd;

		conn->fileReader.buf = conn->str;
		initReader(&conn->fileReader, path, request); 

	}

	free(sbuf);
	strFree(sPath);
	return 0;

}

/* gets listing of directory contents at path or file contents if file
 * returns listing of dir requested, default page, or error page
*/
void getDirListing (char* request, size_t requestLen, string* str, gopherConn* conn) {
	
	string* fullPath = strAlloc(256);
	char prevchar = '\0';
	// prevents access of hidden files, or of files outside server root
	for (int i = 0; i < requestLen && request[i] != '\0'; i++) {
		if ((prevchar == '\0' || prevchar == '/') && request[i] == '.') {
			goto DIR_ERROR;
		}
		prevchar = request[i];
	}
	strAppend(fullPath, conn->serverRoot, strlen(conn->serverRoot));
	strAppend(fullPath, request, requestLen);
	strAppend(fullPath, "\0", 1);

	if (getFileOrDir(fullPath->content, request, str, conn)) {
DIR_ERROR:
		strTruncate(fullPath, 0);
		strAppend(fullPath, conn->serverRoot, strlen(conn->serverRoot));
		strAppend(fullPath, ERROR_PAGE_PATH, strlen(ERROR_PAGE_PATH));
		strAppend(fullPath, "\0", 1);

		strTruncate(str, 0);
		if (getFileOrDir(fullPath->content, request, str, conn)) {
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
	if ((conn->contentState == CONTENT_STR && (conn -> responseSent >= conn -> str -> len)) || (conn->contentState == CONTENT_FILE && isDone(&conn->fileReader))) { 
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
		refillBuf(&conn->fileReader);
		if (isDone(&conn->fileReader)) {
			conn->state = SEND_END_STRING;
			return;
		}
		long sent = send(conn -> sock, conn->fileReader.buf->content + conn->fileReader.bufPos, conn->fileReader.buf->len - conn->fileReader.bufPos, MSG_DONTWAIT);
		if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			conn -> state = ERROR;
			return;
		}
		conn->fileReader.bufPos += sent;
	}
}
