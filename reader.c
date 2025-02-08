#include "reader.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>


/* Update this to incorporate the end string*/
void refillBuf(reader* r) {
	switch (r->state) {
		case READER_DIR_HEAD:
			
		while ((r->dirEnt = readdir(r->dir)) != NULL) {
			case READER_HEAD:
			case READER_ENTRY:
			case READER_TAIL:
		}
		r->state = READER_DIR_TAIL;
		case READER_DIR_TAIL:
			break;
		case READER_FILE:
		// needs a closer look
			if (r->bufPos != r->buf->len) {
				break;
			}
			if (r->bufPos == r->buf->len && r->buf->len < r->buf->size) {
				r->state = READER_DONE;
			}
			r->bufPos = 0;
			int readCode = read(r->fd, r->buf->content, r->buf->size);
			r->buf->len = readCode;
			if (readCode < 0) {
				r->state = READER_DONE;
			}
			break;
			
		case READER_DONE:
			break;
	}
}

/* 0 on success
 * buf is assumed already initialized
 */
int initReader (reader* r, char* path, char* request) {
	
	r->path = path;
	r->request = request;
	// hack to make logic of refillBuf() easier
	r->buf->len = r->buf->size;
	r->bufPos = r->buf->size;

	int fd = open(path, O_RDONLY, 0);

	if (fd < 0) {
		close(fd);
		perror("bad request, file dont exist");
		return 1;
	}
	struct stat* sbuf = malloc(sizeof(struct stat));
	if (fstat(fd, sbuf) != 0) {
		free(sbuf);
		close(fd);
		perror("couldnt stat file");
		return 2;
	}

	int type = sbuf->st_mode & S_IFMT;

	free(sbuf);

	// switch for file or dir
	if (type == S_IFDIR) {
		close(fd);
		r->fd = -1;
		DIR* dir = fdopendir(fd);
		if (dir == NULL) {
			return 3;
		}
		r->dirEnt = NULL;
		r->dir = dir;
		r->state = READER_DIR_HEAD;
	} else {
		r->fd = fd;
		r->dir = NULL;
		r->dirEnt = NULL;
		r->state = READER_FILE;
	}

	return 0;
}

int isDone(reader* r) { return r->state == READER_DONE;}
